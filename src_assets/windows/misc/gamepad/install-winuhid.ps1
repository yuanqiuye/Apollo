param(
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverDir = Join-Path $scriptPath "winuhid"
$infPath = Join-Path $driverDir "WinUHidDriver.inf"
$catPath = Join-Path $driverDir "WinUHidDriver.cat"
$dllPath = Join-Path $driverDir "WinUHidDriver.dll"
$certPath = Join-Path $driverDir "WinUHidSteamControllerTestSigning.cer"

function Assert-File {
    param([Parameter(Mandatory)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required WinUHid installer file is missing: $Path"
    }
}

function Test-IsAdministrator {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [int[]]$AllowedExitCodes = @(0)
    )

    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($AllowedExitCodes -notcontains $exitCode) {
        throw "$FilePath failed with exit code $exitCode"
    }
}

function Get-WinUHidDevices {
    return @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -eq "WinUHid Virtual HID Enumerator" -or
            ($_.Name -like "*WinUHid*" -and $_.DeviceID -like "ROOT\*")
        })
}

function Test-WinUHidInterface {
    try {
        $stream = [System.IO.File]::Open(
            "\\.\WinUHid",
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::ReadWrite)
        $stream.Dispose()
        return $true
    }
    catch {
        return $false
    }
}

function Add-SetupApiHelper {
    if ("WinUHidSetupApi" -as [type]) {
        return
    }

    Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class WinUHidSetupApi
{
    private const int DICD_GENERATE_ID = 0x00000001;
    private const int SPDRP_HARDWAREID = 0x00000001;
    private const int DIF_REGISTERDEVICE = 0x00000019;
    private const int INSTALLFLAG_FORCE = 0x00000001;

    [StructLayout(LayoutKind.Sequential)]
    private struct SP_DEVINFO_DATA
    {
        public int cbSize;
        public Guid ClassGuid;
        public int DevInst;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid ClassGuid, IntPtr hwndParent);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SetupDiCreateDeviceInfo(
        IntPtr DeviceInfoSet,
        string DeviceName,
        ref Guid ClassGuid,
        string DeviceDescription,
        IntPtr hwndParent,
        int CreationFlags,
        ref SP_DEVINFO_DATA DeviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SetupDiSetDeviceRegistryProperty(
        IntPtr DeviceInfoSet,
        ref SP_DEVINFO_DATA DeviceInfoData,
        int Property,
        byte[] PropertyBuffer,
        int PropertyBufferSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiCallClassInstaller(
        int InstallFunction,
        IntPtr DeviceInfoSet,
        ref SP_DEVINFO_DATA DeviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

    [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool UpdateDriverForPlugAndPlayDevices(
        IntPtr hwndParent,
        string HardwareId,
        string FullInfPath,
        int InstallFlags,
        out bool RebootRequired);

    public static void CreateRootDevice()
    {
        Guid systemClass = new Guid("4d36e97d-e325-11ce-bfc1-08002be10318");
        IntPtr set = SetupDiCreateDeviceInfoList(ref systemClass, IntPtr.Zero);
        if (set == IntPtr.Zero || set.ToInt64() == -1)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfoList failed");
        }

        try
        {
            SP_DEVINFO_DATA data = new SP_DEVINFO_DATA();
            data.cbSize = Marshal.SizeOf(typeof(SP_DEVINFO_DATA));

            if (!SetupDiCreateDeviceInfo(
                set,
                "Root\\WinUHid",
                ref systemClass,
                "WinUHid Virtual HID Enumerator",
                IntPtr.Zero,
                DICD_GENERATE_ID,
                ref data))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfo failed");
            }

            byte[] hardwareIds = Encoding.Unicode.GetBytes("Root\\WinUHid\0\0");
            if (!SetupDiSetDeviceRegistryProperty(
                set,
                ref data,
                SPDRP_HARDWAREID,
                hardwareIds,
                hardwareIds.Length))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiSetDeviceRegistryProperty failed");
            }

            if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, ref data))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed");
            }
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(set);
        }
    }

    public static bool UpdateDriver(string infPath, out bool rebootRequired)
    {
        return UpdateDriverForPlugAndPlayDevices(
            IntPtr.Zero,
            "Root\\WinUHid",
            infPath,
            INSTALLFLAG_FORCE,
            out rebootRequired);
    }
}
'@
}

Assert-File $infPath
Assert-File $catPath
Assert-File $dllPath
Assert-File $certPath

$signature = Get-AuthenticodeSignature -FilePath $catPath
Write-Information "WinUHid catalog signature status: $($signature.Status)"

if ($CheckOnly) {
    Add-SetupApiHelper
    Write-Host "WinUHid installer files are present in $driverDir"
    exit 0
}

if (-not (Test-IsAdministrator)) {
    throw "WinUHid driver installation requires an elevated PowerShell session."
}

Invoke-Checked -FilePath "certutil.exe" -Arguments @("-addstore", "-f", "Root", $certPath)
Invoke-Checked -FilePath "certutil.exe" -Arguments @("-addstore", "-f", "TrustedPublisher", $certPath)

$devices = @(Get-WinUHidDevices)
if ($devices.Count -eq 0) {
    Add-SetupApiHelper
    [WinUHidSetupApi]::CreateRootDevice()
    Start-Sleep -Seconds 1
}

Invoke-Checked -FilePath "pnputil.exe" -Arguments @("/add-driver", $infPath, "/install") -AllowedExitCodes @(0, 259)

Add-SetupApiHelper
$rebootRequired = $false
if (-not [WinUHidSetupApi]::UpdateDriver($infPath, [ref]$rebootRequired)) {
    throw "UpdateDriverForPlugAndPlayDevices failed with Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

Start-Sleep -Seconds 1
if (-not (Test-WinUHidInterface)) {
    throw "WinUHid driver installed, but \\.\WinUHid is not accessible."
}

if ($rebootRequired) {
    Write-Warning "WinUHid driver requested a reboot."
}
else {
    Write-Information "WinUHid driver installed and \\.\WinUHid is accessible."
}
