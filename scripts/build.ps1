<#
.SYNOPSIS
    Configure, build and optionally test/run BrokenLiquid from a plain PowerShell window.

.DESCRIPTION
    Enters the Visual Studio x64 developer environment, puts vcpkg's Ninja on PATH, then runs
    the CMake preset. Machine-specific settings (VCPKG_ROOT etc.) can live in the untracked
    scripts/env.local.ps1, which is dot-sourced if present.

.EXAMPLE
    ./scripts/build.ps1                  # configure + build preset dev-local (or dev)
    ./scripts/build.ps1 -Test            # also run unit tests
    ./scripts/build.ps1 -Run             # build, then launch the app
    ./scripts/build.ps1 -Preset release
#>
param(
    [string]$Preset = '',
    [switch]$Test,
    [switch]$Run,
    [switch]$Fresh,
    [switch]$KeepGoing   # keep compiling after errors (ninja -k 0) to see them all at once
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$envLocal = Join-Path $PSScriptRoot 'env.local.ps1'
if (Test-Path $envLocal) { . $envLocal }

if (-not $Preset) {
    $Preset = if (Test-Path (Join-Path $root 'CMakeUserPresets.json')) { 'dev-local' } else { 'dev' }
}

# --- Visual Studio developer environment ----------------------------------------------------------
if (-not $env:VSCMD_VER) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 (or newer) with the C++ x64 tools was not found.' }
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH  # VsDevCmd calls vswhere by name
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# --- vcpkg + Ninja --------------------------------------------------------------------------------
if (-not $env:VCPKG_ROOT) {
    $userPresets = Join-Path $root 'CMakeUserPresets.json'
    if (Test-Path $userPresets) {
        $env:VCPKG_ROOT = ((Get-Content $userPresets -Raw | ConvertFrom-Json).configurePresets |
            Where-Object { $_.environment.VCPKG_ROOT } | Select-Object -First 1).environment.VCPKG_ROOT
    }
}
if (-not $env:VCPKG_ROOT) { throw 'Set VCPKG_ROOT (or create scripts/env.local.ps1).' }
$ninja = & (Join-Path $env:VCPKG_ROOT 'vcpkg.exe') fetch ninja 2>$null | Select-Object -Last 1
if (-not (Test-Path $ninja)) { throw "Could not fetch Ninja via vcpkg: $ninja" }
$env:PATH = (Split-Path $ninja) + ';' + $env:PATH

# Resolve a preset's binaryDir (user presets first, then shared presets; follows "inherits").
function Get-BinaryDir([string]$Name) {
    $all = @()
    foreach ($f in 'CMakeUserPresets.json', 'CMakePresets.json') {
        $p = Join-Path $root $f
        if (Test-Path $p) { $all += (Get-Content $p -Raw | ConvertFrom-Json).configurePresets }
    }
    $current = $Name
    while ($current) {
        $preset = $all | Where-Object { $_.name -eq $current } | Select-Object -First 1
        if (-not $preset) { break }
        if ($preset.binaryDir) {
            return $preset.binaryDir.Replace('${sourceDir}', $root).Replace('${presetName}', $Name)
        }
        $current = @($preset.inherits)[0]
    }
    return Join-Path $root "build/$Name"
}

# Keep big temporary files off the system drive when a local temp dir is configured.
if ($env:BL_TEMP) { $env:TEMP = $env:BL_TEMP; $env:TMP = $env:BL_TEMP }

# --- Configure / build / test ---------------------------------------------------------------------
Push-Location $root
try {
    $configureArgs = @('--preset', $Preset)
    if ($Fresh) { $configureArgs += '--fresh' }
    cmake @configureArgs
    if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }

    $buildArgs = @('--build', '--preset', $Preset)
    if ($KeepGoing) { $buildArgs += @('--', '-k', '0') }
    cmake @buildArgs
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
    }

    if ($Run) {
        & (Join-Path (Get-BinaryDir $Preset) 'bin/BrokenLiquid.exe')
    }
}
finally {
    Pop-Location
}
