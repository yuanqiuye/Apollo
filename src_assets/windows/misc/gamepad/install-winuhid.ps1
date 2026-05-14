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
$expectedWinUHidSigningSubject = "CN=WinUHid Steam Controller Test Signing"
$expectedWinUHidSigningThumbprint = "F1FF0896A2D804CF361A9E0E9FAF17B517F7446A"

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

function Format-Thumbprint {
    param([Parameter(Mandatory)] [string]$Thumbprint)

    return ($Thumbprint -replace "\s", "").ToUpperInvariant()
}

function Get-CertificateFileThumbprint {
    param([Parameter(Mandatory)] [string]$Path)

    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($Path)
    try {
        return Format-Thumbprint $certificate.Thumbprint
    }
    finally {
        $certificate.Reset()
    }
}

function Assert-WinUHidSigningBundle {
    $expectedThumbprint = Format-Thumbprint $expectedWinUHidSigningThumbprint
    $certificateThumbprint = Get-CertificateFileThumbprint $certPath

    if ($certificateThumbprint -ne $expectedThumbprint) {
        throw "Unexpected WinUHid signing certificate thumbprint: $certificateThumbprint"
    }

    $signature = Get-AuthenticodeSignature -FilePath $catPath
    if ($null -eq $signature.SignerCertificate) {
        throw "WinUHid catalog does not contain a signer certificate."
    }

    $signerThumbprint = Format-Thumbprint $signature.SignerCertificate.Thumbprint
    if ($signerThumbprint -ne $expectedThumbprint) {
        throw "Unexpected WinUHid catalog signer thumbprint: $signerThumbprint"
    }

    Write-Information "WinUHid test-signing thumbprint verified: $expectedThumbprint"
}

function Remove-StaleWinUHidTestCertificates {
    param([Parameter(Mandatory)] [string]$StoreName)

    $expectedThumbprint = Format-Thumbprint $expectedWinUHidSigningThumbprint
    $storePath = "Cert:\LocalMachine\$StoreName"
    if (-not (Test-Path -LiteralPath $storePath)) {
        return
    }

    Get-ChildItem -LiteralPath $storePath |
        Where-Object {
            $_.Subject -eq $expectedWinUHidSigningSubject -and
            (Format-Thumbprint $_.Thumbprint) -ne $expectedThumbprint
        } |
        ForEach-Object {
            Write-Information "Removing stale WinUHid test-signing certificate from ${StoreName}: $($_.Thumbprint)"
            Remove-Item -LiteralPath $_.PSPath -Force
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
Assert-WinUHidSigningBundle

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

Remove-StaleWinUHidTestCertificates -StoreName "Root"
Remove-StaleWinUHidTestCertificates -StoreName "TrustedPublisher"

Invoke-Checked -FilePath "certutil.exe" -Arguments @("-addstore", "-f", "Root", $certPath)
Invoke-Checked -FilePath "certutil.exe" -Arguments @("-addstore", "-f", "TrustedPublisher", $certPath)

$signature = Get-AuthenticodeSignature -FilePath $catPath
if ($signature.Status -ne "Valid") {
    throw "WinUHid catalog signature is not trusted after certificate import: $($signature.Status)"
}

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
