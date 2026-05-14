/**
 * @file src/platform/windows/steam_controller_winuhid.cpp
 * @brief WinUHidDevs-backed virtual Steam Controller support for Windows.
 */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "steam_controller_winuhid.h"

#include "steam_controller_protocol.h"
#include "src/logging.h"

namespace platf {
  using namespace std::literals;

  namespace {
    constexpr wchar_t WINUHID_DLL_NAME[] = L"WinUHid.dll";
    constexpr wchar_t WINUHID_DEVS_DLL_NAME[] = L"WinUHidDevs.dll";
    constexpr wchar_t SC_DIRECT_BT_INSTANCE_ID[] = L"FXA99613035A2";

    typedef struct _WINUHID_PRESET_DEVICE_INFO {
      USHORT VendorID;
      USHORT ProductID;
      USHORT VersionNumber;
      GUID ContainerId;
      PCWSTR InstanceID;
      PCWSTR HardwareIDs;
    } WINUHID_PRESET_DEVICE_INFO, *PWINUHID_PRESET_DEVICE_INFO;
    typedef const WINUHID_PRESET_DEVICE_INFO *PCWINUHID_PRESET_DEVICE_INFO;

    typedef enum _WINUHID_STEAM_CONTROLLER_RAW_HID_REPORT_TYPE {
      WinUHidSteamControllerRawHidReportTypeOutput = 1,
      WinUHidSteamControllerRawHidReportTypeFeature = 2,
    } WINUHID_STEAM_CONTROLLER_RAW_HID_REPORT_TYPE;

    typedef struct _WINUHID_STEAM_CONTROLLER *PWINUHID_STEAM_CONTROLLER;

    typedef struct _WINUHID_STEAM_CONTROLLER_INFO {
      PCWINUHID_PRESET_DEVICE_INFO BasicInfo;
      PCSTR SerialNumber;
      ULONG FirmwareBuildTime;
      ULONG BoardRevision;
    } WINUHID_STEAM_CONTROLLER_INFO, *PWINUHID_STEAM_CONTROLLER_INFO;
    typedef const WINUHID_STEAM_CONTROLLER_INFO *PCWINUHID_STEAM_CONTROLLER_INFO;

    typedef void WINUHID_STEAM_CONTROLLER_RAW_HID_CB(PVOID CallbackContext,
                                                     WINUHID_STEAM_CONTROLLER_RAW_HID_REPORT_TYPE ReportType,
                                                     const UCHAR *Report,
                                                     ULONG ReportLength);
    typedef WINUHID_STEAM_CONTROLLER_RAW_HID_CB *PWINUHID_STEAM_CONTROLLER_RAW_HID_CB;

    using winuhid_get_driver_interface_version_t = DWORD (*)();
    using winuhid_steam_controller_create_t = PWINUHID_STEAM_CONTROLLER (*)(PCWINUHID_STEAM_CONTROLLER_INFO,
                                                                           PWINUHID_STEAM_CONTROLLER_RAW_HID_CB,
                                                                           PVOID);
    using winuhid_steam_controller_report_raw_input_t = BOOL (*)(PWINUHID_STEAM_CONTROLLER, LPCVOID, ULONG);
    using winuhid_steam_controller_destroy_t = void (*)(PWINUHID_STEAM_CONTROLLER);

    std::wstring get_module_directory() {
      std::vector<wchar_t> buffer(MAX_PATH);

      for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
          return {};
        }
        if (length < buffer.size() - 1) {
          std::wstring path(buffer.data(), length);
          const auto slash = path.find_last_of(L"\\/");
          if (slash == std::wstring::npos) {
            return {};
          }
          return path.substr(0, slash);
        }
        buffer.resize(buffer.size() * 2);
      }
    }

    HMODULE load_dll_from_module_directory(const wchar_t *name) {
      const auto module_dir = get_module_directory();
      if (!module_dir.empty()) {
        const auto path = module_dir + L"\\" + name;
        HMODULE dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (dll) {
          return dll;
        }
      }

      return LoadLibraryW(name);
    }

    FARPROC get_required_proc(HMODULE dll, const char *name) {
      auto proc = GetProcAddress(dll, name);
      if (!proc) {
        BOOST_LOG(warning) << "Missing required WinUHidDevs export: "sv << name;
      }
      return proc;
    }

    struct winuhid_devs_api_t {
      HMODULE winuhid_dll = nullptr;
      HMODULE winuhid_devs_dll = nullptr;

      winuhid_get_driver_interface_version_t get_driver_interface_version = nullptr;
      winuhid_steam_controller_create_t create = nullptr;
      winuhid_steam_controller_report_raw_input_t report_raw_input = nullptr;
      winuhid_steam_controller_destroy_t destroy = nullptr;

      ~winuhid_devs_api_t() {
        if (winuhid_devs_dll) {
          FreeLibrary(winuhid_devs_dll);
        }
        if (winuhid_dll) {
          FreeLibrary(winuhid_dll);
        }
      }

      bool load() {
        if (create && report_raw_input && destroy) {
          return true;
        }

        winuhid_dll = load_dll_from_module_directory(WINUHID_DLL_NAME);
        if (!winuhid_dll) {
          BOOST_LOG(warning) << "WinUHid.dll was not found; Steam Controller emulation is disabled"sv;
          return false;
        }

        winuhid_devs_dll = load_dll_from_module_directory(WINUHID_DEVS_DLL_NAME);
        if (!winuhid_devs_dll) {
          BOOST_LOG(warning) << "WinUHidDevs.dll was not found; Steam Controller emulation is disabled"sv;
          return false;
        }

        get_driver_interface_version = reinterpret_cast<winuhid_get_driver_interface_version_t>(
          get_required_proc(winuhid_dll, "WinUHidGetDriverInterfaceVersion"));
        create = reinterpret_cast<winuhid_steam_controller_create_t>(
          get_required_proc(winuhid_devs_dll, "WinUHidSteamControllerCreate"));
        report_raw_input = reinterpret_cast<winuhid_steam_controller_report_raw_input_t>(
          get_required_proc(winuhid_devs_dll, "WinUHidSteamControllerReportRawInput"));
        destroy = reinterpret_cast<winuhid_steam_controller_destroy_t>(
          get_required_proc(winuhid_devs_dll, "WinUHidSteamControllerDestroy"));

        return get_driver_interface_version && create && report_raw_input && destroy;
      }

      DWORD query_driver_version() const {
        return get_driver_interface_version ? get_driver_interface_version() : 0;
      }
    };

    std::vector<unsigned char> make_report(unsigned char report_id, const void *payload, size_t payload_length) {
      std::vector<unsigned char> report(1 + payload_length);
      report[0] = report_id;
      if (payload && payload_length) {
        std::memcpy(&report[1], payload, payload_length);
      }
      return report;
    }

    sc_i16 clamp_i16(float value) {
      value = std::clamp(value, static_cast<float>(std::numeric_limits<sc_i16>::min()), static_cast<float>(std::numeric_limits<sc_i16>::max()));
      return static_cast<sc_i16>(std::lround(value));
    }

    sc_i16 normalized_to_pad_axis(float value) {
      value = std::clamp(value, 0.0f, 1.0f);
      return clamp_i16((value * 2.0f - 1.0f) * 32767.0f);
    }

    sc_i16 trigger_to_i16(std::uint8_t trigger) {
      return static_cast<sc_i16>((static_cast<int>(trigger) * std::numeric_limits<sc_i16>::max()) / 255);
    }

    std::uint16_t pressure_to_u16(float pressure) {
      pressure = std::clamp(pressure, 0.0f, 1.0f);
      return static_cast<std::uint16_t>(std::lround(pressure * 65535.0f));
    }

    std::uint32_t triton_buttons_from_apollo(const gamepad_state_t &state, bool right_pad_touched) {
      const auto flags = state.buttonFlags;
      std::uint32_t buttons = 0;

      if (flags & A) {
        buttons |= SC_TRITON_BUTTON_A;
      }
      if (flags & B) {
        buttons |= SC_TRITON_BUTTON_B;
      }
      if (flags & X) {
        buttons |= SC_TRITON_BUTTON_X;
      }
      if (flags & Y) {
        buttons |= SC_TRITON_BUTTON_Y;
      }
      if (flags & LEFT_BUTTON) {
        buttons |= SC_TRITON_BUTTON_L;
      }
      if (flags & RIGHT_BUTTON) {
        buttons |= SC_TRITON_BUTTON_R;
      }
      if (flags & LEFT_STICK) {
        buttons |= SC_TRITON_BUTTON_L3;
      }
      if (flags & RIGHT_STICK) {
        buttons |= SC_TRITON_BUTTON_R3;
      }
      if (flags & DPAD_UP) {
        buttons |= SC_TRITON_BUTTON_DPAD_UP;
      }
      if (flags & DPAD_DOWN) {
        buttons |= SC_TRITON_BUTTON_DPAD_DOWN;
      }
      if (flags & DPAD_LEFT) {
        buttons |= SC_TRITON_BUTTON_DPAD_LEFT;
      }
      if (flags & DPAD_RIGHT) {
        buttons |= SC_TRITON_BUTTON_DPAD_RIGHT;
      }
      if (flags & START) {
        buttons |= SC_TRITON_BUTTON_VIEW;
      }
      if (flags & BACK) {
        buttons |= SC_TRITON_BUTTON_MENU;
      }
      if (flags & HOME) {
        buttons |= SC_TRITON_BUTTON_STEAM;
      }
      if (flags & PADDLE1) {
        buttons |= SC_TRITON_BUTTON_L4;
      }
      if (flags & PADDLE2) {
        buttons |= SC_TRITON_BUTTON_R4;
      }
      if (flags & PADDLE3) {
        buttons |= SC_TRITON_BUTTON_L5;
      }
      if (flags & PADDLE4) {
        buttons |= SC_TRITON_BUTTON_R5;
      }
      if (flags & MISC_BUTTON) {
        buttons |= SC_TRITON_BUTTON_QAM;
      }
      if (flags & TOUCHPAD_BUTTON) {
        buttons |= SC_TRITON_CLICK_RIGHT_PAD;
      }
      if (state.lt > 0) {
        buttons |= SC_TRITON_CLICK_LEFT_TRIGGER;
      }
      if (state.rt > 0) {
        buttons |= SC_TRITON_CLICK_RIGHT_TRIGGER;
      }
      if (right_pad_touched) {
        buttons |= SC_TRITON_TOUCH_RIGHT_PAD;
      }

      return buttons;
    }
  }  // namespace

  struct steam_controller_winuhid_t::impl_t {
    struct slot_t {
      PWINUHID_STEAM_CONTROLLER controller = nullptr;
      winuhid_devs_api_t *api = nullptr;
      feedback_queue_t feedback_queue;
      std::uint8_t client_relative_index = 0;
      sc_triton_state_no_quat_t state {};
      sc_triton_battery_status_t battery {};
      std::atomic_uint raw_input_submit_count {0};
      std::atomic_uint raw_feedback_report_count {0};
      std::atomic_uint normalized_input_ignored_after_raw_count {0};
      bool right_pad_touched = false;

      bool active() const {
        return controller != nullptr;
      }
    };

    impl_t() {
      slots.resize(MAX_GAMEPADS);
    }

    ~impl_t() {
      for (int i = 0; i < slots.size(); ++i) {
        free_gamepad(i);
      }
    }

    int init() {
      available = api.load() && api.query_driver_version() != 0;
      if (!available) {
        BOOST_LOG(warning) << "WinUHidDevs Steam Controller preset is unavailable; Steam Controller emulation is disabled"sv;
      } else {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller preset is available"sv;
      }
      return 0;
    }

    bool owns_gamepad(int nr) const {
      return nr >= 0 && nr < slots.size() && slots[nr] && slots[nr]->active();
    }

    bool ignore_normalized_input_after_raw(slot_t &slot, const char *kind) {
      if (slot.raw_input_submit_count.load(std::memory_order_acquire) == 0) {
        return false;
      }

      const auto count = ++slot.normalized_input_ignored_after_raw_count;
      if (count <= 20 || (count % 100) == 0) {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller normalized "
                         << kind
                         << " ignored after raw HID input #"
                         << count
                         << ", client="sv << static_cast<int>(slot.client_relative_index);
      }
      return true;
    }

    int alloc_gamepad(const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
      if (!available) {
        BOOST_LOG(warning) << "Cannot create Steam Controller because WinUHidDevs is unavailable"sv;
        return -1;
      }

      if (id.globalIndex < 0 || id.globalIndex >= slots.size()) {
        return -1;
      }

      free_gamepad(id.globalIndex);

      auto slot = std::make_unique<slot_t>();
      slot->api = &api;
      slot->client_relative_index = id.clientRelativeIndex;
      slot->feedback_queue = std::move(feedback_queue);
      slot->battery.charge_state = 1;
      slot->battery.battery_level = 100;

      std::wstring instance_id = SC_DIRECT_BT_INSTANCE_ID;
      if (id.globalIndex > 0) {
        instance_id += L"_";
        instance_id += std::to_wstring(id.globalIndex);
      }

      WINUHID_PRESET_DEVICE_INFO basic_info {};
      basic_info.InstanceID = instance_id.c_str();

      WINUHID_STEAM_CONTROLLER_INFO controller_info {};
      controller_info.BasicInfo = &basic_info;

      slot->controller = api.create(&controller_info, &raw_hid_callback, slot.get());
      if (!slot->controller) {
        const DWORD last_error = GetLastError();
        SetLastError(last_error);
        BOOST_LOG(warning) << "Failed to create WinUHidDevs Steam Controller, error="sv << last_error;
        return -1;
      }

      slots[id.globalIndex] = std::move(slot);

      auto &created = *slots[id.globalIndex];
      submit_connected(created);
      submit_state(created);
      submit_battery(created);

      if (metadata.capabilities & LI_CCAP_ACCEL) {
        created.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(created.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
      }
      if (metadata.capabilities & LI_CCAP_GYRO) {
        created.feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(created.client_relative_index, LI_MOTION_TYPE_GYRO, 100));
      }

      return 0;
    }

    void free_gamepad(int nr) {
      if (nr < 0 || nr >= slots.size() || !slots[nr]) {
        return;
      }

      auto &slot = *slots[nr];
      if (slot.active()) {
        submit_disconnected(slot);
        api.destroy(slot.controller);
        slot.controller = nullptr;
      }

      slots[nr].reset();
    }

    void update(int nr, const gamepad_state_t &gamepad_state) {
      if (!owns_gamepad(nr)) {
        return;
      }

      auto &slot = *slots[nr];
      if (ignore_normalized_input_after_raw(slot, "update")) {
        return;
      }

      slot.state.buttons = triton_buttons_from_apollo(gamepad_state, slot.right_pad_touched);
      slot.state.trigger_left = trigger_to_i16(gamepad_state.lt);
      slot.state.trigger_right = trigger_to_i16(gamepad_state.rt);
      slot.state.left_stick_x = gamepad_state.lsX;
      slot.state.left_stick_y = gamepad_state.lsY;
      slot.state.right_stick_x = gamepad_state.rsX;
      slot.state.right_stick_y = gamepad_state.rsY;

      submit_state(slot);
    }

    void touch(const gamepad_touch_t &touch) {
      if (!owns_gamepad(touch.id.globalIndex)) {
        return;
      }

      auto &slot = *slots[touch.id.globalIndex];
      if (ignore_normalized_input_after_raw(slot, "touch")) {
        return;
      }

      switch (touch.eventType) {
        case LI_TOUCH_EVENT_DOWN:
        case LI_TOUCH_EVENT_MOVE:
        case LI_TOUCH_EVENT_HOVER:
          slot.right_pad_touched = true;
          slot.state.right_pad_x = normalized_to_pad_axis(touch.x);
          slot.state.right_pad_y = normalized_to_pad_axis(1.0f - touch.y);
          slot.state.pressure_right = pressure_to_u16(touch.pressure);
          slot.state.buttons |= SC_TRITON_TOUCH_RIGHT_PAD;
          break;
        case LI_TOUCH_EVENT_UP:
        case LI_TOUCH_EVENT_CANCEL:
        case LI_TOUCH_EVENT_CANCEL_ALL:
        case LI_TOUCH_EVENT_HOVER_LEAVE:
          slot.right_pad_touched = false;
          slot.state.pressure_right = 0;
          slot.state.buttons &= ~(SC_TRITON_TOUCH_RIGHT_PAD | SC_TRITON_CLICK_RIGHT_PAD);
          break;
        default:
          return;
      }

      submit_state(slot);
    }

    void motion(const gamepad_motion_t &motion) {
      if (!owns_gamepad(motion.id.globalIndex)) {
        return;
      }

      auto &slot = *slots[motion.id.globalIndex];
      if (ignore_normalized_input_after_raw(slot, "motion")) {
        return;
      }

      slot.state.imu.timestamp = GetTickCount();

      switch (motion.motionType) {
        case LI_MOTION_TYPE_ACCEL:
          slot.state.imu.accel_x = clamp_i16((motion.x / 9.80665f) * 8192.0f);
          slot.state.imu.accel_y = clamp_i16((motion.y / 9.80665f) * 8192.0f);
          slot.state.imu.accel_z = clamp_i16((motion.z / 9.80665f) * 8192.0f);
          break;
        case LI_MOTION_TYPE_GYRO:
          slot.state.imu.gyro_x = clamp_i16(motion.x * 16.0f);
          slot.state.imu.gyro_y = clamp_i16(motion.y * 16.0f);
          slot.state.imu.gyro_z = clamp_i16(motion.z * 16.0f);
          break;
        default:
          return;
      }

      submit_state(slot);
    }

    void battery(const gamepad_battery_t &battery) {
      if (!owns_gamepad(battery.id.globalIndex)) {
        return;
      }

      auto &slot = *slots[battery.id.globalIndex];
      slot.battery.charge_state = battery.state;
      if (battery.percentage != LI_BATTERY_PERCENTAGE_UNKNOWN) {
        slot.battery.battery_level = battery.percentage;
      }

      submit_battery(slot);
    }

    void raw_hid(const gamepad_raw_hid_report_t &report) {
      if (!owns_gamepad(report.id.globalIndex)) {
        return;
      }

      if (report.report_type != gamepad_raw_hid_report_type_e::input || report.report_length == 0) {
        return;
      }

      auto &slot = *slots[report.id.globalIndex];

      const unsigned char *raw_report = report.report.data();
      DWORD raw_report_length = report.report_length;
      std::vector<unsigned char> report_with_id;

      if (report.report_length == SC_TRITON_STATE_NO_QUAT_BYTES) {
        report_with_id = make_report(SC_REPORT_ID_TRITON_CONTROLLER_STATE_BLE, report.report.data(), report.report_length);
        raw_report = report_with_id.data();
        raw_report_length = static_cast<DWORD>(report_with_id.size());
      } else if (report.report_length == SC_TRITON_STATE_NO_QUAT_BYTES + 1 &&
                 (report.report[0] == SC_REPORT_ID_TRITON_CONTROLLER_STATE ||
                  report.report[0] == SC_REPORT_ID_TRITON_CONTROLLER_STATE_BLE)) {
        raw_report = report.report.data();
        raw_report_length = report.report_length;
      } else {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller raw HID input ignored, len="sv << static_cast<int>(report.report_length);
        return;
      }

      if (!slot.api->report_raw_input(slot.controller, raw_report, raw_report_length)) {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller raw HID submit failed, error="sv << GetLastError();
        return;
      }

      const auto count = ++slot.raw_input_submit_count;
      if (count <= 20 || (count % 100) == 0) {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller raw HID input accepted #"
                         << count
                         << ", gamepad="sv << report.id.globalIndex
                         << ", len="sv << static_cast<int>(raw_report_length)
                         << ", report="sv << util::hex(raw_report[0]).to_string_view()
                         << ", payload_first="sv << util::hex(raw_report[1]).to_string_view();
      }
    }

    bool submit_report(slot_t &slot, unsigned char report_id, const void *payload, size_t payload_length) {
      if (!slot.active()) {
        return false;
      }

      const auto report = make_report(report_id, payload, payload_length);
      if (!slot.api->report_raw_input(slot.controller, report.data(), static_cast<ULONG>(report.size()))) {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller submit report failed, report="sv << util::hex(report_id).to_string_view() << ", error="sv << GetLastError();
        return false;
      }
      return true;
    }

    bool submit_connected(slot_t &slot) {
      sc_triton_wireless_status_t status {};
      status.state = SC_TRITON_WIRELESS_CONNECT;
      return submit_report(slot, SC_REPORT_ID_TRITON_WIRELESS_STATUS, &status, sizeof(status));
    }

    bool submit_disconnected(slot_t &slot) {
      sc_triton_wireless_status_t status {};
      status.state = SC_TRITON_WIRELESS_DISCONNECT;
      return submit_report(slot, SC_REPORT_ID_TRITON_WIRELESS_STATUS, &status, sizeof(status));
    }

    bool submit_state(slot_t &slot) {
      slot.state.seq_num++;
      return submit_report(slot, SC_REPORT_ID_TRITON_CONTROLLER_STATE_BLE, &slot.state, sizeof(slot.state));
    }

    bool submit_battery(slot_t &slot) {
      return submit_report(slot, SC_REPORT_ID_TRITON_BATTERY_STATUS, &slot.battery, sizeof(slot.battery));
    }

    static void raw_hid_callback(PVOID callback_context,
                                 WINUHID_STEAM_CONTROLLER_RAW_HID_REPORT_TYPE report_type,
                                 const UCHAR *report,
                                 ULONG report_length) {
      auto slot = static_cast<slot_t *>(callback_context);
      if (!slot || !slot->feedback_queue || !report || report_length == 0) {
        return;
      }

      gamepad_raw_hid_report_type_e moonlight_report_type;
      switch (report_type) {
        case WinUHidSteamControllerRawHidReportTypeOutput:
          moonlight_report_type = gamepad_raw_hid_report_type_e::output;
          break;
        case WinUHidSteamControllerRawHidReportTypeFeature:
          moonlight_report_type = gamepad_raw_hid_report_type_e::feature;
          break;
        default:
          return;
      }

      const auto final_length = static_cast<std::uint8_t>(std::min<ULONG>(report_length, MAX_RAW_HID_REPORT_SIZE));
      auto msg = gamepad_feedback_msg_t::make_raw_hid_report(slot->client_relative_index, moonlight_report_type, report, final_length);
      slot->feedback_queue->raise(msg);
      slot->feedback_queue->mail->wake();

      const auto count = ++slot->raw_feedback_report_count;
      if (count <= 20 || (count % 100) == 0) {
        BOOST_LOG(debug) << "WinUHidDevs Steam Controller raw HID feedback #"
                         << count
                         << ", client="sv << static_cast<int>(slot->client_relative_index)
                         << ", type="sv << static_cast<int>(moonlight_report_type)
                         << ", len="sv << static_cast<int>(final_length)
                         << ", first="sv << util::hex(report[0]).to_string_view();
      }
    }

    winuhid_devs_api_t api;
    bool available = false;
    std::vector<std::unique_ptr<slot_t>> slots;
  };

  steam_controller_winuhid_t::steam_controller_winuhid_t():
      impl(std::make_unique<impl_t>()) {
  }

  steam_controller_winuhid_t::~steam_controller_winuhid_t() = default;

  int steam_controller_winuhid_t::init() {
    return impl->init();
  }

  bool steam_controller_winuhid_t::is_available() const {
    return impl->available;
  }

  bool steam_controller_winuhid_t::owns_gamepad(int nr) const {
    return impl->owns_gamepad(nr);
  }

  int steam_controller_winuhid_t::alloc_gamepad(const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    return impl->alloc_gamepad(id, metadata, std::move(feedback_queue));
  }

  void steam_controller_winuhid_t::free_gamepad(int nr) {
    impl->free_gamepad(nr);
  }

  void steam_controller_winuhid_t::update(int nr, const gamepad_state_t &gamepad_state) {
    impl->update(nr, gamepad_state);
  }

  void steam_controller_winuhid_t::touch(const gamepad_touch_t &touch) {
    impl->touch(touch);
  }

  void steam_controller_winuhid_t::motion(const gamepad_motion_t &motion) {
    impl->motion(motion);
  }

  void steam_controller_winuhid_t::battery(const gamepad_battery_t &battery) {
    impl->battery(battery);
  }

  void steam_controller_winuhid_t::raw_hid(const gamepad_raw_hid_report_t &report) {
    impl->raw_hid(report);
  }
}  // namespace platf
