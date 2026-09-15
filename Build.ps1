param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$CoreOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildDirectory = Join-Path $projectRoot ('build-' + $Configuration.ToLowerInvariant())
$vsDevShell = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'

if (-not (Test-Path -LiteralPath $vsDevShell)) {
    throw 'Visual Studio 2026 Build Tools were not found.'
}

& $vsDevShell -Arch amd64 -HostArch amd64

$configureArguments = @(
    '-S', $projectRoot,
    '-B', $buildDirectory,
    '-G', 'Ninja',
    ('-DCMAKE_BUILD_TYPE=' + $Configuration)
)

if ($CoreOnly) {
    $configureArguments += '-DSTEPSHAPER_BUILD_FL_PLUGIN=OFF'
    $configureArguments += '-DSTEPSHAPER_BUILD_PREVIEW=OFF'
}
else {
    $configureArguments += '-DSTEPSHAPER_BUILD_FL_PLUGIN=ON'
    $configureArguments += '-DSTEPSHAPER_BUILD_PREVIEW=ON'
}

cmake @configureArguments
cmake --build $buildDirectory
ctest --test-dir $buildDirectory --output-on-failure
