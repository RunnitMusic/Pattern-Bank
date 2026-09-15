param(
    [ValidateSet('2024', '2025', '2026')]
    [string]$FLVersion = '2026',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build-release')
)

$ErrorActionPreference = 'Stop'
$packageDirectory = Join-Path $BuildDirectory 'package\Pattern Bank'
$flEffectsDirectory = 'C:\Program Files\Image-Line\FL Studio ' + $FLVersion + '\Plugins\Fruity\Effects'
$destination = Join-Path $flEffectsDirectory 'Pattern Bank'

if (-not (Test-Path -LiteralPath (Join-Path $packageDirectory 'Pattern Bank_x64.dll'))) {
    throw 'Build the Release package before installing.'
}
if (-not (Test-Path -LiteralPath $flEffectsDirectory)) {
    throw ('FL Studio ' + $FLVersion + ' was not found.')
}

New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath (Join-Path $packageDirectory 'Pattern Bank_x64.dll') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $packageDirectory 'Plugin.nfo') -Destination $destination -Force
Write-Output ('Installed Pattern Bank for FL Studio ' + $FLVersion + ' at ' + $destination)
