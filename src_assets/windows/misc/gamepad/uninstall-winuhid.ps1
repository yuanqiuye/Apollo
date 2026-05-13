Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-WinUHidDevices {
    return @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -eq "WinUHid Virtual HID Enumerator" -or
            ($_.Name -like "*WinUHid*" -and $_.DeviceID -like "ROOT\*")
        })
}

function Get-WinUHidDriverStorePackages {
    $packages = @()
    $current = @{}

    foreach ($line in (& pnputil.exe /enum-drivers)) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($current.Count -gt 0) {
                $packages += [pscustomobject]$current
                $current = @{}
            }
            continue
        }

        if ($line -match "^\s*([^:]+):\s*(.*)$") {
            $current[$Matches[1].Trim()] = $Matches[2].Trim()
        }
    }

    if ($current.Count -gt 0) {
        $packages += [pscustomobject]$current
    }

    return @($packages | Where-Object {
        $_."Original Name" -ieq "winuhiddriver.inf" -or
        $_."Provider Name" -eq "WinUHid Project"
    })
}

if (-not (Test-IsAdministrator)) {
    throw "WinUHid driver uninstall requires an elevated PowerShell session."
}

foreach ($device in (Get-WinUHidDevices)) {
    Write-Information "Removing WinUHid device $($device.DeviceID)"
    & pnputil.exe /remove-device $device.DeviceID
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to remove WinUHid device $($device.DeviceID); continuing."
    }
}

foreach ($package in (Get-WinUHidDriverStorePackages)) {
    $publishedName = $package."Published Name"
    if (-not $publishedName) {
        continue
    }

    Write-Information "Deleting WinUHid driver package $publishedName"
    & pnputil.exe /delete-driver $publishedName /uninstall /force
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to delete WinUHid driver package $publishedName; continuing."
    }
}
