param(
    [Parameter(Mandatory)] [string]$WinUHidRoot,
    [Parameter(Mandatory)] [string]$OutputDir,
    [string]$DriverPackageDir,
    [ValidateSet("Debug", "Release")] [string]$Configuration = "Release",
    [ValidateSet("x86", "x64", "ARM64", "ARM64EC")] [string]$Platform = "x64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param([Parameter(Mandatory)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file was not found: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$DestinationDirectory
    )

    $resolved = Resolve-RequiredPath $Source
    Copy-Item -LiteralPath $resolved -Destination $DestinationDirectory -Force
}

$winUHidRootPath = (Resolve-Path -LiteralPath $WinUHidRoot).Path
$outputPath = New-Item -ItemType Directory -Force -Path $OutputDir

$winUHidDll = Join-Path $winUHidRootPath "WinUHid\build\$Configuration\$Platform\WinUHid.dll"
$winUHidDevsDll = Join-Path $winUHidRootPath "WinUHidDevs\build\$Configuration\$Platform\WinUHidDevs.dll"

Copy-RequiredFile -Source $winUHidDll -DestinationDirectory $outputPath.FullName
Copy-RequiredFile -Source $winUHidDevsDll -DestinationDirectory $outputPath.FullName

if ([string]::IsNullOrWhiteSpace($DriverPackageDir)) {
    $DriverPackageDir = Join-Path $PSScriptRoot "..\src_assets\windows\misc\gamepad\winuhid"
}

$driverPackagePath = (Resolve-Path -LiteralPath $DriverPackageDir).Path
$driverFiles = @(
    "WinUHidDriver.cat",
    "WinUHidDriver.dll",
    "WinUHidDriver.inf",
    "WinUHidSteamControllerTestSigning.cer"
)

foreach ($file in $driverFiles) {
    Copy-RequiredFile -Source (Join-Path $driverPackagePath $file) -DestinationDirectory $outputPath.FullName
}

$expectedFiles = @(
    "WinUHid.dll",
    "WinUHidDevs.dll",
    "WinUHidDriver.cat",
    "WinUHidDriver.dll",
    "WinUHidDriver.inf",
    "WinUHidSteamControllerTestSigning.cer"
)

foreach ($file in $expectedFiles) {
    Resolve-RequiredPath (Join-Path $outputPath.FullName $file) | Out-Null
}

Write-Host "Prepared WinUHid runtime package at $($outputPath.FullName)"
