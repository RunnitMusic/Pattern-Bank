$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdministrator) {
    throw 'Run this installer from an Administrator PowerShell window.'
}

$bundleName = 'Pattern Bank VST3 Output Probe.vst3'
$source = Join-Path $PSScriptRoot $bundleName
$destinationRoot = 'C:\Program Files\Common Files\VST3'
$destination = Join-Path $destinationRoot $bundleName

if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "The VST3 bundle was not found beside this script: $source"
}

New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null

if (Test-Path -LiteralPath $destination) {
    $backupName = "$bundleName.previous"
    $backup = Join-Path $destinationRoot $backupName
    if (Test-Path -LiteralPath $backup) {
        Remove-Item -LiteralPath $backup -Recurse -Force
    }
    Move-Item -LiteralPath $destination -Destination $backup
}

Copy-Item -LiteralPath $source -Destination $destinationRoot -Recurse

Write-Host "Installed: $destination"
Write-Host 'Next: open FL Studio Plugin Manager and choose Find installed plugins.'
