/**
 * @file tools/sunshinesvc.cpp
 * @brief Handles launching Sunshine.exe into user sessions as SYSTEM
 */
#define WIN32_LEAN_AND_MEAN
#include <string>
#include <format>
#include <Windows.h>
#include <WtsApi32.h>

// PROC_THREAD_ATTRIBUTE_JOB_LIST is currently missing from MinGW headers
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
  #define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

SERVICE_STATUS_HANDLE service_status_handle;
SERVICE_STATUS service_status;
HANDLE stop_event;
HANDLE session_change_event;

constexpr DWORD NO_SESSION = 0xFFFFFFFF;

#ifndef APOLLO_WINDOWS_SERVICE_NAME
  #define APOLLO_WINDOWS_SERVICE_NAME "Apollo-WinUHid"
#endif

#define SERVICE_NAME APOLLO_WINDOWS_SERVICE_NAME

std::wstring WidenServiceName() {
  auto length = MultiByteToWideChar(CP_UTF8, 0, SERVICE_NAME, -1, nullptr, 0);
  if (length <= 1) {
    return L"sunshine";
  }

  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, SERVICE_NAME, -1, result.data(), length);
  result.resize(length - 1);
  return result;
}

void WriteServiceLog(HANDLE log_file_handle, const std::string &message) {
  if (log_file_handle == nullptr || log_file_handle == INVALID_HANDLE_VALUE) {
    return;
  }

  SYSTEMTIME now = {};
  GetLocalTime(&now);

  auto line = std::format(
    "[svc {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] {}\r\n",
    now.wYear,
    now.wMonth,
    now.wDay,
    now.wHour,
    now.wMinute,
    now.wSecond,
    now.wMilliseconds,
    message
  );

  DWORD written = 0;
  WriteFile(log_file_handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

DWORD WINAPI HandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
  switch (dwControl) {
    case SERVICE_CONTROL_INTERROGATE:
      return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE:
      // Restart Sunshine when the visible interactive desktop may have moved
      // between console/RDP sessions.
      if (dwEventType == WTS_CONSOLE_CONNECT ||
          dwEventType == WTS_CONSOLE_DISCONNECT ||
          dwEventType == WTS_REMOTE_CONNECT ||
          dwEventType == WTS_REMOTE_DISCONNECT ||
          dwEventType == WTS_SESSION_LOGON ||
          dwEventType == WTS_SESSION_LOGOFF ||
          dwEventType == WTS_SESSION_LOCK ||
          dwEventType == WTS_SESSION_UNLOCK) {
        SetEvent(session_change_event);
      }
      return NO_ERROR;

    case SERVICE_CONTROL_PRESHUTDOWN:
      // The system is shutting down
    case SERVICE_CONTROL_STOP:
      // Let SCM know we're stopping in up to 30 seconds
      service_status.dwCurrentState = SERVICE_STOP_PENDING;
      service_status.dwControlsAccepted = 0;
      service_status.dwWaitHint = 30 * 1000;
      SetServiceStatus(service_status_handle, &service_status);

      // Trigger ServiceMain() to start cleanup
      SetEvent(stop_event);
      return NO_ERROR;

    default:
      return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

HANDLE CreateJobObjectForChildProcess() {
  HANDLE job_handle = CreateJobObjectW(nullptr, nullptr);
  if (!job_handle) {
    return nullptr;
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limit_info = {};

  // Kill Sunshine.exe when the final job object handle is closed (which will happen if we terminate unexpectedly).
  // This ensures we don't leave an orphaned Sunshine.exe running with an inherited handle to our log file.
  job_limit_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

  // Allow Sunshine.exe to use CREATE_BREAKAWAY_FROM_JOB when spawning processes to ensure they can to live beyond
  // the lifetime of SunshineSvc.exe. This avoids unexpected user data loss if we crash or are killed.
  job_limit_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;

  if (!SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &job_limit_info, sizeof(job_limit_info))) {
    CloseHandle(job_handle);
    return nullptr;
  }

  return job_handle;
}

LPPROC_THREAD_ATTRIBUTE_LIST AllocateProcThreadAttributeList(DWORD attribute_count) {
  SIZE_T size;
  InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &size);

  auto list = (LPPROC_THREAD_ATTRIBUTE_LIST) HeapAlloc(GetProcessHeap(), 0, size);
  if (list == nullptr) {
    return nullptr;
  }

  if (!InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
    HeapFree(GetProcessHeap(), 0, list);
    return nullptr;
  }

  return list;
}

bool SessionHasLoggedOnUser(DWORD session_id) {
  LPWSTR user_name = nullptr;
  DWORD bytes = 0;

  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id, WTSUserName, &user_name, &bytes)) {
    return false;
  }

  bool has_logged_on_user = user_name != nullptr && user_name[0] != L'\0';
  WTSFreeMemory(user_name);
  return has_logged_on_user;
}

struct SessionSelection {
  DWORD session_id = NO_SESSION;
  bool console_fallback = false;
};

SessionSelection GetActiveInteractiveSession() {
  PWTS_SESSION_INFOW sessions = nullptr;
  DWORD session_count = 0;

  if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &session_count)) {
    for (DWORD i = 0; i < session_count; i++) {
      const auto &session = sessions[i];
      if (session.SessionId == 0 || session.State != WTSActive) {
        continue;
      }

      if (!SessionHasLoggedOnUser(session.SessionId)) {
        continue;
      }

      auto selected_session_id = session.SessionId;
      WTSFreeMemory(sessions);
      return {selected_session_id, false};
    }

    WTSFreeMemory(sessions);
  }

  auto console_session_id = WTSGetActiveConsoleSessionId();
  if (console_session_id != NO_SESSION && console_session_id != 0) {
    return {console_session_id, true};
  }

  return {};
}

HANDLE DuplicateTokenForSession(DWORD session_id) {
  HANDLE current_token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &current_token)) {
    return nullptr;
  }

  // Duplicate our own LocalSystem token
  HANDLE new_token;
  if (!DuplicateTokenEx(current_token, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &new_token)) {
    CloseHandle(current_token);
    return nullptr;
  }

  CloseHandle(current_token);

  // Change the duplicated token to the target interactive session ID
  if (!SetTokenInformation(new_token, TokenSessionId, &session_id, sizeof(session_id))) {
    CloseHandle(new_token);
    return nullptr;
  }

  return new_token;
}

HANDLE OpenLogFileHandle() {
  WCHAR module_path[MAX_PATH];
  std::wstring log_file_name;

  if (GetModuleFileNameW(nullptr, module_path, _countof(module_path)) != 0) {
    std::wstring install_dir = module_path;
    auto tools_separator = install_dir.find_last_of(L"\\/");
    if (tools_separator != std::wstring::npos) {
      install_dir.resize(tools_separator);
      auto root_separator = install_dir.find_last_of(L"\\/");
      if (root_separator != std::wstring::npos) {
        install_dir.resize(root_separator);
        auto config_dir = install_dir + L"\\config";
        CreateDirectoryW(config_dir.c_str(), nullptr);
        log_file_name = config_dir + L"\\sunshinesvc.log";
      }
    }
  }

  if (log_file_name.empty()) {
    WCHAR temp_path[MAX_PATH];
    GetTempPathW(_countof(temp_path), temp_path);
    log_file_name = temp_path;
    log_file_name += WidenServiceName();
    log_file_name += L"-sunshine.log";
  }

  // The file handle must be inheritable for our child process to use it
  SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), nullptr, TRUE};

  // Overwrite the old service-wrapper log
  return CreateFileW(log_file_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security_attributes, CREATE_ALWAYS, 0, nullptr);
}

bool RunTerminationHelper(HANDLE console_token, DWORD pid) {
  WCHAR module_path[MAX_PATH];
  GetModuleFileNameW(nullptr, module_path, _countof(module_path));
  std::wstring command;

  command += L'"';
  command += module_path;
  command += L'"';
  command += std::format(L" --terminate {}", pid);

  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.lpDesktop = (LPWSTR) L"winsta0\\default";

  // Execute ourselves as a detached process in the user session with the --terminate argument.
  // This will allow us to attach to Sunshine's console and send it a Ctrl-C event.
  PROCESS_INFORMATION process_info;
  if (!CreateProcessAsUserW(console_token, module_path, (LPWSTR) command.c_str(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS, nullptr, nullptr, &startup_info, &process_info)) {
    return false;
  }

  // Wait for the termination helper to complete
  WaitForSingleObject(process_info.hProcess, INFINITE);

  // Check the exit status of the helper process
  DWORD exit_code;
  GetExitCodeProcess(process_info.hProcess, &exit_code);

  // Cleanup handles
  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);

  // If the helper process returned 0, it succeeded
  return exit_code == 0;
}

VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv) {
  service_status_handle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, HandlerEx, nullptr);
  if (service_status_handle == nullptr) {
    // Nothing we can really do here but terminate ourselves
    ExitProcess(GetLastError());
    return;
  }

  // Tell SCM we're starting
  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwWaitHint = 0;
  service_status.dwControlsAccepted = 0;
  service_status.dwCheckPoint = 0;
  service_status.dwCurrentState = SERVICE_START_PENDING;
  SetServiceStatus(service_status_handle, &service_status);

  // Create a manual-reset stop event
  stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (stop_event == nullptr) {
    // Tell SCM we failed to start
    service_status.dwWin32ExitCode = GetLastError();
    service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(service_status_handle, &service_status);
    return;
  }

  // Create an auto-reset session change event
  session_change_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (session_change_event == nullptr) {
    // Tell SCM we failed to start
    service_status.dwWin32ExitCode = GetLastError();
    service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(service_status_handle, &service_status);
    return;
  }

  auto log_file_handle = OpenLogFileHandle();
  if (log_file_handle == INVALID_HANDLE_VALUE) {
    // Tell SCM we failed to start
    service_status.dwWin32ExitCode = GetLastError();
    service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(service_status_handle, &service_status);
    return;
  }

  // We can use a single STARTUPINFOEXW for all the processes that we launch
  STARTUPINFOEXW startup_info = {};
  startup_info.StartupInfo.cb = sizeof(startup_info);
  startup_info.StartupInfo.lpDesktop = (LPWSTR) L"winsta0\\default";
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup_info.StartupInfo.hStdInput = nullptr;
  startup_info.StartupInfo.hStdOutput = log_file_handle;
  startup_info.StartupInfo.hStdError = log_file_handle;

  // Allocate an attribute list with space for 2 entries
  startup_info.lpAttributeList = AllocateProcThreadAttributeList(2);
  if (startup_info.lpAttributeList == nullptr) {
    // Tell SCM we failed to start
    service_status.dwWin32ExitCode = GetLastError();
    service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(service_status_handle, &service_status);
    return;
  }

  // Only allow Sunshine.exe to inherit the log file handle, not all inheritable handles
  UpdateProcThreadAttribute(startup_info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &log_file_handle, sizeof(log_file_handle), nullptr, nullptr);

  // Tell SCM we're running (and stoppable now)
  service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
  service_status.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(service_status_handle, &service_status);

  std::string last_service_status;
  auto log_status_once = [&](const std::string &status) {
    if (status == last_service_status) {
      return;
    }

    WriteServiceLog(log_file_handle, status);
    last_service_status = status;
  };

  // Loop every 3 seconds until the stop event is set or Sunshine.exe is running
  while (WaitForSingleObject(stop_event, 3000) != WAIT_OBJECT_0) {
    auto active_session = GetActiveInteractiveSession();
    if (active_session.session_id == NO_SESSION) {
      log_status_once("Waiting for an active user session or console session");
      continue;
    }

    log_status_once(std::format(
      "Launching Sunshine.exe in session {} ({})",
      active_session.session_id,
      active_session.console_fallback ? "console fallback" : "active logged-on user"
    ));

    auto session_token = DuplicateTokenForSession(active_session.session_id);
    if (session_token == nullptr) {
      log_status_once(std::format("Failed to duplicate token for session {} [0x{:08X}]", active_session.session_id, GetLastError()));
      continue;
    }

    // Job objects cannot span sessions, so we must create one for each process
    auto job_handle = CreateJobObjectForChildProcess();
    if (job_handle == nullptr) {
      log_status_once(std::format("Failed to create child process job object [0x{:08X}]", GetLastError()));
      CloseHandle(session_token);
      continue;
    }

    // Start Sunshine.exe inside our job object
    UpdateProcThreadAttribute(startup_info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_handle, sizeof(job_handle), nullptr, nullptr);

    PROCESS_INFORMATION process_info;
    if (!CreateProcessAsUserW(session_token, L"Sunshine.exe", nullptr, nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, (LPSTARTUPINFOW) &startup_info, &process_info)) {
      log_status_once(std::format("Failed to launch Sunshine.exe in session {} [0x{:08X}]", active_session.session_id, GetLastError()));
      CloseHandle(session_token);
      CloseHandle(job_handle);
      continue;
    }

    WriteServiceLog(log_file_handle, std::format("Sunshine.exe started as pid {} in session {}", process_info.dwProcessId, active_session.session_id));

    bool still_running;
    do {
      // Wait for the stop event to be set, Sunshine.exe to terminate, or the console session to change
      const HANDLE wait_objects[] = {stop_event, process_info.hProcess, session_change_event};
      switch (WaitForMultipleObjects(_countof(wait_objects), wait_objects, FALSE, INFINITE)) {
        case WAIT_OBJECT_0 + 2:
          if (GetActiveInteractiveSession().session_id == active_session.session_id) {
            // The active interactive user session did not change. Let Sunshine keep running.
            still_running = true;
            continue;
          }
          WriteServiceLog(log_file_handle, "Session target changed; restarting Sunshine.exe");
          // Fall-through to terminate Sunshine.exe and start it again.
        case WAIT_OBJECT_0:
          // The service is shutting down, so try to gracefully terminate Sunshine.exe.
          // If it doesn't terminate in 20 seconds, we will forcefully terminate it.
          if (!RunTerminationHelper(session_token, process_info.dwProcessId) ||
              WaitForSingleObject(process_info.hProcess, 20000) != WAIT_OBJECT_0) {
            // If it won't terminate gracefully, kill it now
            TerminateProcess(process_info.hProcess, ERROR_PROCESS_ABORTED);
          }
          still_running = false;
          break;

        case WAIT_OBJECT_0 + 1:
          {
            // Sunshine terminated itself.

            DWORD exit_code = 0;
            if (GetExitCodeProcess(process_info.hProcess, &exit_code) && exit_code == ERROR_SHUTDOWN_IN_PROGRESS) {
              // Sunshine is asking for us to shut down, so gracefully stop ourselves.
              SetEvent(stop_event);
            } else {
              WriteServiceLog(log_file_handle, std::format("Sunshine.exe exited from session {} [0x{:08X}]", active_session.session_id, exit_code));
            }
            still_running = false;
            break;
          }
      }
    } while (still_running);

    last_service_status.clear();

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(session_token);
    CloseHandle(job_handle);
  }

  // Let SCM know we've stopped
  service_status.dwCurrentState = SERVICE_STOPPED;
  SetServiceStatus(service_status_handle, &service_status);
}

// This will run in a child process in the user session
int DoGracefulTermination(DWORD pid) {
  // Attach to Sunshine's console
  if (!AttachConsole(pid)) {
    return GetLastError();
  }

  // Disable our own Ctrl-C handling
  SetConsoleCtrlHandler(nullptr, TRUE);

  // Send a Ctrl-C event to Sunshine
  if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
    return GetLastError();
  }

  return 0;
}

int main(int argc, char *argv[]) {
  static const SERVICE_TABLE_ENTRY service_table[] = {
    {(LPSTR) SERVICE_NAME, ServiceMain},
    {nullptr, nullptr}
  };

  // Check if this is a reinvocation of ourselves to send Ctrl-C to Sunshine.exe
  if (argc == 3 && strcmp(argv[1], "--terminate") == 0) {
    return DoGracefulTermination(atol(argv[2]));
  }

  // By default, services have their current directory set to %SYSTEMROOT%\System32.
  // We want to use the directory where Sunshine.exe is located instead of system32.
  // This requires stripping off 2 path components: the file name and the last folder
  WCHAR module_path[MAX_PATH];
  GetModuleFileNameW(nullptr, module_path, _countof(module_path));
  for (auto i = 0; i < 2; i++) {
    auto last_sep = wcsrchr(module_path, '\\');
    if (last_sep) {
      *last_sep = 0;
    }
  }
  SetCurrentDirectoryW(module_path);

  // Trigger our ServiceMain()
  return StartServiceCtrlDispatcher(service_table);
}
