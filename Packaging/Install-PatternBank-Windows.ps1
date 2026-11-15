param(
    [string]$PayloadDirectory = (Join-Path $PSScriptRoot 'Pattern Bank'),
    [string]$MasterDirectory = (Join-Path $env:ProgramData 'Runnit\Pattern Bank'),
    [string[]]$FLStudioPath = @(),
    [switch]$NoAutoDetect,
    [switch]$CopyInsteadOfJunction,
    [switch]$SkipElevation,
    [switch]$SkipProcessCheck
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-FLInstallations {
    param([string[]]$ExplicitPaths, [bool]$AutoDetect)

    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($path in $ExplicitPaths) {
        if (-not [string]::IsNullOrWhiteSpace($path)) {
            $candidates.Add($path)
        }
    }

    if ($AutoDetect) {
        $imageLineRoot = Join-Path $env:ProgramFiles 'Image-Line'
        if (Test-Path -LiteralPath $imageLineRoot -PathType Container) {
            Get-ChildItem -LiteralPath $imageLineRoot -Directory -Filter 'FL Studio *' |
                ForEach-Object { $candidates.Add($_.FullName) }
        }

        $uninstallRoots = @(
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
        )
        foreach ($root in $uninstallRoots) {
            Get-ItemProperty -Path $root -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.DisplayName -like 'FL Studio*' -and
                    -not [string]::IsNullOrWhiteSpace($_.InstallLocation)
                } |
                ForEach-Object { $candidates.Add([string]$_.InstallLocation) }
        }
    }

    $valid = foreach ($candidate in $candidates) {
        $expanded = [Environment]::ExpandEnvironmentVariables($candidate.Trim().TrimEnd('\'))
        $effects = Join-Path $expanded 'Plugins\Fruity\Effects'
        if (Test-Path -LiteralPath $effects -PathType Container) {
            (Resolve-Path -LiteralPath $expanded).Path
        }
    }

    return @($valid | Sort-Object -Unique)
}

function Assert-Payload {
    param([string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw "Pattern Bank payload directory was not found: $Directory"
    }
    foreach ($name in @('Pattern Bank_x64.dll', 'Plugin.nfo')) {
        $file = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Pattern Bank payload is incomplete. Missing: $file"
        }
    }
}

function Install-MasterPayload {
    param([string]$Source, [string]$Destination)

    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null

    $staging = "$Destination.staging-$PID"
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    New-Item -ItemType Directory -Path $staging | Out-Null
    Get-ChildItem -LiteralPath $Source -Force |
        Copy-Item -Destination $staging -Recurse -Force
    Assert-Payload -Directory $staging

    $previous = "$Destination.previous"
    if (Test-Path -LiteralPath $previous) {
        Remove-Item -LiteralPath $previous -Recurse -Force
    }
    if (Test-Path -LiteralPath $Destination) {
        Move-Item -LiteralPath $Destination -Destination $previous
    }

    try {
        Move-Item -LiteralPath $staging -Destination $Destination
    }
    catch {
        if ((-not (Test-Path -LiteralPath $Destination)) -and
            (Test-Path -LiteralPath $previous)) {
            Move-Item -LiteralPath $previous -Destination $Destination
        }
        throw
    }

    Assert-Payload -Directory $Destination
}

function Install-FLLink {
    param(
        [string]$FLRoot,
        [string]$Master,
        [string]$BackupRoot,
        [bool]$UseCopy
    )

    $effects = Join-Path $FLRoot 'Plugins\Fruity\Effects'
    $destination = Join-Path $effects 'Pattern Bank'
    $installName = Split-Path -Leaf $FLRoot

    if (Test-Path -LiteralPath $destination) {
        $existing = Get-Item -LiteralPath $destination -Force
        $isReparsePoint = ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        if ($isReparsePoint -and -not $UseCopy) {
            $target = @($existing.Target)[0]
            if (-not [string]::IsNullOrWhiteSpace($target)) {
                $targetPath = if ([IO.Path]::IsPathRooted($target)) {
                    $target
                }
                else {
                    Join-Path $existing.Parent.FullName $target
                }
                $resolvedTarget = [IO.Path]::GetFullPath($targetPath)
                if ($resolvedTarget.TrimEnd('\') -ieq $Master.TrimEnd('\')) {
                    Assert-Payload -Directory $destination
                    return [pscustomobject]@{
                        FLStudio = $installName
                        Method = 'Junction (already current)'
                        Destination = $destination
                    }
                }
            }
        }

        New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null
        $safeInstallName = $installName -replace '[^A-Za-z0-9._-]', '_'
        $backup = Join-Path $BackupRoot ($safeInstallName + '-' +
            (Get-Date -Format 'yyyyMMdd-HHmmss'))
        Move-Item -LiteralPath $destination -Destination $backup
    }

    $method = 'Copy'
    if (-not $UseCopy) {
        try {
            New-Item -ItemType Junction -Path $destination -Target $Master |
                Out-Null
            $method = 'Junction'
        }
        catch {
            Write-Warning "Junction creation failed for $installName; using a copy instead. $($_.Exception.Message)"
            if (Test-Path -LiteralPath $destination) {
                Remove-Item -LiteralPath $destination -Force
            }
            Copy-Item -LiteralPath $Master -Destination $destination -Recurse
        }
    }
    else {
        Copy-Item -LiteralPath $Master -Destination $destination -Recurse
    }

    Assert-Payload -Directory $destination
    return [pscustomobject]@{
        FLStudio = $installName
        Method = $method
        Destination = $destination
    }
}

if ((-not $SkipElevation) -and (-not (Test-IsAdministrator))) {
    if ($PSBoundParameters.Keys.Count -gt 0) {
        throw 'Custom installer options require running this script from an Administrator PowerShell window.'
    }

    $quotedScript = '"' + $PSCommandPath + '"'
    $elevated = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $quotedScript)
    exit $elevated.ExitCode
}

if (-not $SkipProcessCheck) {
    $runningFL = @(Get-Process -Name 'FL64' -ErrorAction SilentlyContinue)
    if ($runningFL.Count -gt 0) {
        throw 'Close every running FL Studio window before installing Pattern Bank.'
    }
}

$payload = [IO.Path]::GetFullPath($PayloadDirectory)
$master = [IO.Path]::GetFullPath($MasterDirectory)
Assert-Payload -Directory $payload

$installations = @(Get-FLInstallations -ExplicitPaths $FLStudioPath `
    -AutoDetect (-not $NoAutoDetect))
if ($installations.Count -eq 0) {
    throw 'No valid FL Studio installations were found.'
}

Install-MasterPayload -Source $payload -Destination $master
$backupRoot = Join-Path (Split-Path -Parent $master) 'Backups'
$results = foreach ($installation in $installations) {
    Install-FLLink -FLRoot $installation -Master $master `
        -BackupRoot $backupRoot -UseCopy $CopyInsteadOfJunction
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath `
    (Join-Path $master 'Pattern Bank_x64.dll')).Hash

Write-Host ''
Write-Host 'Pattern Bank installation completed.' -ForegroundColor Green
Write-Host "Master payload: $master"
Write-Host "DLL SHA-256: $hash"
$results | Format-Table -AutoSize
Write-Host 'Restart FL Studio. Native plug-ins do not require a VST scan.'

