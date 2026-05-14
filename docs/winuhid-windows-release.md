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

## Test-Signed Driver Bundle

The packaged WinUHid driver bundle is test-signed, not Microsoft
attestation-signed or WHQL-signed.

The bundled public test-signing certificate is:

- Subject: `CN=WinUHid Steam Controller Test Signing`
- Thumbprint: `F1FF0896A2D804CF361A9E0E9FAF17B517F7446A`

The private key for this certificate must remain local to the signing machine.
Do not commit a `.pfx`, `.pvk`, `.key`, `.pem`, or any other private-key export
to this repository. The installer package contains only the public `.cer` file
and the signed `.cat` catalog.

`install-winuhid.ps1` verifies that the bundled `.cer` and `.cat` both match the
expected public thumbprint before importing the certificate or installing the
driver.

To rotate this test certificate, run
`scripts/sign_winuhid_driver_package.ps1 -ForceNewCertificate` from the Apollo
checkout. The script creates the code-signing certificate with
`KeyExportPolicy NonExportable`, exports only the public `.cer`, signs the
catalog, and updates this document plus the installer thumbprint guard.

## Side-by-Side Apollo Install

The Windows package is named `Apollo-WinUHid` so it can be installed beside the
upstream Apollo package. The side-by-side identity uses:

- Install directory: `C:\Program Files\Apollo-WinUHid`
- Service name: `Apollo-WinUHid`
- Service display name: `Apollo-WinUHid Service`
- Service state path: `%LOCALAPPDATA%\Apollo-WinUHid`
- Firewall rule name: `Apollo-WinUHid`
- Start Menu shortcut: `Apollo-WinUHid`
- Default base port: `48089`

The default port family is offset from upstream Apollo's default port family:

- GameStream HTTPS: `48084`
- GameStream HTTP / Moonlight host port: `48089`
- Web UI: `48090`
- RTSP setup: `48110`

The installer creates the `Apollo-WinUHid` service with auto start and starts it
after installation. It does not add the install directory to the global `PATH`.
This avoids taking over an installed upstream Apollo service while still letting
Apollo-WinUHid run as its own service by default.

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
