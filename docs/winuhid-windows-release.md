# Windows x64 WinUHid Release Build

This fork builds Apollo with WinUHid-backed Steam Controller support and can
publish a Windows x64 NSIS installer from GitHub Actions.

## Build Shape

The workflow is `.github/workflows/windows-x64-installer.yml`.

It performs these steps on `windows-2022`:

1. Check out Apollo with submodules.
2. Check out the configured WinUHid repository.
3. Build `WinUHid.dll` and `WinUHidDevs.dll` from WinUHid source in Release x64.
4. Prepare a WinUHid runtime package directory with:
   - `WinUHid.dll`
   - `WinUHidDevs.dll`
   - `WinUHidDriver.cat`
   - `WinUHidDriver.dll`
   - `WinUHidDriver.inf`
   - `WinUHidSteamControllerTestSigning.cer`
5. Configure Apollo with `-DWINUHID_PACKAGE_DIR=<runtime package dir>`.
6. Build `web-ui`, `sunshine`, and `sunshinesvc`.
7. Run CPack with the NSIS generator and upload the installer artifact.

The signed driver bundle is still taken from Apollo's packaged
`src_assets/windows/misc/gamepad/winuhid` directory unless the workflow is
extended with a production driver signing step. The user-mode WinUHid DLLs are
rebuilt from source by CI.

## Manual Release

Run the `Windows x64 Installer` workflow manually and set:

- `winuhid_repository`: the fork containing the WinUHid Steam Controller changes
- `winuhid_ref`: the branch, tag, or commit to build
- `release_tag`: the release tag to publish, for example `apollo-winuhid-2026.05.14`
- `publish_release`: `true`

The workflow uploads:

- `Apollo-WinUHid-<tag>-windows-x64.exe`
- `WinUHid-runtime-<tag>-windows-x64.zip`

## Tag Release

Pushing a tag matching `v*` or `apollo-winuhid-*` also publishes the release:

```powershell
git tag apollo-winuhid-2026.05.14
git push origin apollo-winuhid-2026.05.14
```

## Local Packaging

Build WinUHid Release x64, then prepare a runtime package:

```powershell
.\scripts\prepare_winuhid_runtime.ps1 `
  -WinUHidRoot ..\WinUHid `
  -OutputDir .\build\winuhid-runtime `
  -DriverPackageDir .\src_assets\windows\misc\gamepad\winuhid `
  -Configuration Release `
  -Platform x64
```

Configure Apollo with that directory:

```powershell
& C:\msys64\ucrt64\bin\cmake.exe `
  -S . `
  -B build\apollo-windows-x64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTS=OFF `
  -DWINUHID_PACKAGE_DIR="$PWD\build\winuhid-runtime"
```

Then build and package with:

```powershell
& C:\msys64\ucrt64\bin\cmake.exe --build build\apollo-windows-x64 --target web-ui --parallel 4
& C:\msys64\ucrt64\bin\cmake.exe --build build\apollo-windows-x64 --target sunshine --parallel 4
& C:\msys64\ucrt64\bin\cmake.exe --build build\apollo-windows-x64 --target sunshinesvc --parallel 4
& C:\msys64\ucrt64\bin\cpack.exe -G NSIS --config build\apollo-windows-x64\CPackConfig.cmake
```
