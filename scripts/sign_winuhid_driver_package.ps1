param(
    [string]$Subject = "CN=WinUHid Steam Controller Test Signing",
    [string]$DriverPackageDir,
    [string]$InstallerScript,
    [string]$ReleaseDoc,
    [switch]$ForceNewCertificate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param([Parameter(Mandatory)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required path was not found: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
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

function Update-FileText {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Pattern,
        [Parameter(Mandatory)] [string]$Replacement,
        [Parameter(Mandatory)] [string]$Description
    )

    $text = Get-Content -LiteralPath $Path -Raw
    $updated = [regex]::Replace($text, $Pattern, $Replacement)
    if ($updated -eq $text) {
        throw "Unable to update $Description in $Path"
    }

    Set-Content -LiteralPath $Path -Value $updated -NoNewline
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($DriverPackageDir)) {
    $DriverPackageDir = Join-Path $repoRoot "src_assets\windows\misc\gamepad\winuhid"
}

if ([string]::IsNullOrWhiteSpace($InstallerScript)) {
    $InstallerScript = Join-Path $repoRoot "src_assets\windows\misc\gamepad\install-winuhid.ps1"
}

if ([string]::IsNullOrWhiteSpace($ReleaseDoc)) {
    $ReleaseDoc = Join-Path $repoRoot "docs\winuhid-windows-release.md"
}

$driverPackagePath = Resolve-RequiredPath $DriverPackageDir
$installerScriptPath = Resolve-RequiredPath $InstallerScript
$releaseDocPath = Resolve-RequiredPath $ReleaseDoc
$catPath = Resolve-RequiredPath (Join-Path $driverPackagePath "WinUHidDriver.cat")
$cerPath = Join-Path $driverPackagePath "WinUHidSteamControllerTestSigning.cer"

Import-Module Microsoft.PowerShell.Security -ErrorAction Stop
Import-Module PKI -ErrorAction Stop

$certificate = $null
if (-not $ForceNewCertificate) {
    $certificate = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
}

if (-not $certificate) {
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears(3)
}

$thumbprint = Format-Thumbprint $certificate.Thumbprint
Export-Certificate -Cert $certificate -FilePath $cerPath | Out-Null

$windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$signTool = Get-ChildItem $windowsKitsRoot -Recurse -Filter "signtool.exe" |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if (-not $signTool) {
    throw "signtool.exe was not found under $windowsKitsRoot"
}

& $signTool.FullName sign /v /fd SHA256 /sha1 $thumbprint $catPath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Update-FileText `
    -Path $installerScriptPath `
    -Pattern '(\$expectedWinUHidSigningThumbprint\s*=\s*")[0-9A-Fa-f\s]+(")' `
    -Replacement "`${1}$thumbprint`${2}" `
    -Description "installer expected WinUHid signing thumbprint"

Update-FileText `
    -Path $releaseDocPath `
    -Pattern '(- Thumbprint: `)[0-9A-Fa-f\s]+(`)' `
    -Replacement "`${1}$thumbprint`${2}" `
    -Description "release document WinUHid signing thumbprint"

$certificateFileThumbprint = Get-CertificateFileThumbprint $cerPath
if ($certificateFileThumbprint -ne $thumbprint) {
    throw "Public certificate thumbprint mismatch: expected $thumbprint but got $certificateFileThumbprint"
}

$signature = Get-AuthenticodeSignature -FilePath $catPath
if ($null -eq $signature.SignerCertificate) {
    throw "Signed catalog does not contain a signer certificate."
}

$signerThumbprint = Format-Thumbprint $signature.SignerCertificate.Thumbprint
if ($signerThumbprint -ne $thumbprint) {
    throw "Catalog signer thumbprint mismatch: expected $thumbprint but got $signerThumbprint"
}

Write-Host "Signed WinUHid catalog: $catPath"
Write-Host "Exported public certificate: $cerPath"
Write-Host "Updated installer and release docs with thumbprint: $thumbprint"
Write-Host "Private key remains in the CurrentUser certificate store and is not exported by this script."
