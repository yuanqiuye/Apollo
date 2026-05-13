$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$winUHidUninstaller = Join-Path $scriptPath "uninstall-winuhid.ps1"
if (Test-Path -LiteralPath $winUHidUninstaller) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $winUHidUninstaller
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Use Get-CimInstance to find and uninstall Virtual Gamepad
$product = Get-CimInstance -ClassName Win32_Product -Filter "Name='ViGEm Bus Driver'"
if ($product) {
    Invoke-CimMethod -InputObject $product -MethodName Uninstall
    Write-Information "ViGEm Bus Driver uninstalled successfully"
} else {
    Write-Warning "ViGEm Bus Driver not found"
}
