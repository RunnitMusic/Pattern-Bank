param(
    [ValidateSet('2024', '2025', '2026')]
    [string]$FLVersion = '2026',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build-release')
)

$ErrorActionPreference = 'Stop'
$packageDirectory = Join-Path $BuildDirectory 'package\Pattern Bank'
$flDirectory = 'C:\Program Files\Image-Line\FL Studio ' + $FLVersion
$installer = Join-Path $PSScriptRoot 'Packaging\Install-PatternBank-Windows.ps1'

if (-not (Test-Path -LiteralPath (Join-Path $packageDirectory 'Pattern Bank_x64.dll'))) {
    throw 'Build the Release package before installing.'
}
if (-not (Test-Path -LiteralPath (Join-Path $flDirectory 'Plugins\Fruity\Effects'))) {
    throw ('FL Studio ' + $FLVersion + ' was not found.')
}

& $installer -PayloadDirectory $packageDirectory -FLStudioPath $flDirectory -NoAutoDetect
