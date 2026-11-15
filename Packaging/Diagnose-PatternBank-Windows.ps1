$ErrorActionPreference = 'Continue'

$master = Join-Path $env:ProgramData 'Runnit\Pattern Bank'
$imageLineRoot = Join-Path $env:ProgramFiles 'Image-Line'
$failures = 0

Write-Host "Windows: $([Environment]::OSVersion.VersionString)"
Write-Host "Master payload: $master"

$masterDll = Join-Path $master 'Pattern Bank_x64.dll'
if (Test-Path -LiteralPath $masterDll -PathType Leaf) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $masterDll).Hash
    Write-Host "[OK] Master DLL present ($hash)"
}
else {
    Write-Host '[FAIL] Master DLL is missing.' -ForegroundColor Red
    $failures++
}

$flRoots = @()
if (Test-Path -LiteralPath $imageLineRoot -PathType Container) {
    $flRoots = @(Get-ChildItem -LiteralPath $imageLineRoot -Directory `
        -Filter 'FL Studio *')
}

foreach ($fl in $flRoots) {
    $destination = Join-Path $fl.FullName 'Plugins\Fruity\Effects\Pattern Bank'
    if (-not (Test-Path -LiteralPath $destination)) {
        Write-Host "[FAIL] $($fl.Name): Pattern Bank is not installed." -ForegroundColor Red
        $failures++
        continue
    }

    $item = Get-Item -LiteralPath $destination -Force
    $isLink = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    $dll = Join-Path $destination 'Pattern Bank_x64.dll'
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
        Write-Host "[FAIL] $($fl.Name): link/folder exists but DLL is missing." -ForegroundColor Red
        $failures++
        continue
    }

    $kind = if ($isLink) { 'junction' } else { 'copy' }
    Write-Host "[OK] $($fl.Name): $kind -> $destination"
}

if ($flRoots.Count -eq 0) {
    Write-Host '[FAIL] No default FL Studio installations were found.' -ForegroundColor Red
    $failures++
}

if ($failures -gt 0) {
    Write-Host "Diagnostics completed with $failures failure(s)." -ForegroundColor Red
    exit 1
}

Write-Host 'Diagnostics passed.' -ForegroundColor Green

