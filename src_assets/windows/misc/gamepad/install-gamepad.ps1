$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path

# Check if a compatible version of ViGEmBus is already installed (1.17 or later).
$installViGEm = $true
try {
    $vigemBusPath = "$env:SystemRoot\System32\drivers\ViGEmBus.sys"
    $fileVersion = (Get-Item $vigemBusPath).VersionInfo.FileVersion

    if ($fileVersion -ge [System.Version]"1.17") {
        Write-Information "The installed ViGEmBus version is 1.17 or later; skipping ViGEmBus install."
        $installViGEm = $false
    }
}
catch {
    Write-Information "ViGEmBus driver not found or inaccessible, proceeding with installation."
}

if ($installViGEm) {
    $installerPath = Join-Path $scriptPath "vigembus_installer.exe"
    $process = Start-Process `
        -FilePath $installerPath `
        -ArgumentList "/passive", "/promptrestart" `
        -Wait `
        -PassThru

    if ($process.ExitCode -ne 0) {
        exit $process.ExitCode
    }
}

$winUHidInstaller = Join-Path $scriptPath "install-winuhid.ps1"
if (Test-Path -LiteralPath $winUHidInstaller) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $winUHidInstaller
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
else {
    Write-Warning "WinUHid installer script not found: $winUHidInstaller"
}
