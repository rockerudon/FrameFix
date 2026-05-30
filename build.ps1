# Build script for the FrameFix Ashita plugin.
#
# Requirements:
#   - Visual Studio 2022 with the C++ x86 (32-bit) build tools.
#   - The Ashita v4 plugin SDK headers.
#
# Set $sdk below to the folder that contains Ashita.h (normally your
# Ashita/plugins/sdk directory). FFXI is a 32-bit process, so this builds x86.

$ErrorActionPreference = 'Stop'

$root     = $PSScriptRoot
$src      = Join-Path $root 'src\FrameFix.cpp'
$buildDir = Join-Path $root 'build'
$distDir  = Join-Path $root 'dist'
$outDll   = Join-Path $distDir 'FrameFix.dll'
$obj      = Join-Path $buildDir 'FrameFix.obj'

# ---------------------------------------------------------------------------
# Point this at your Ashita plugin SDK headers (the folder containing Ashita.h).
# ---------------------------------------------------------------------------
$sdk = 'C:\Path\To\Ashita\plugins\sdk'

if (-not (Test-Path (Join-Path $sdk 'Ashita.h'))) {
    throw "Ashita SDK not found at '$sdk'. Edit the `$sdk path at the top of build.ps1."
}

$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat'
)

$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    throw 'Could not find Visual Studio vcvars32.bat. Install the Visual Studio C++ x86 build tools.'
}

New-Item -ItemType Directory -Force $buildDir | Out-Null
New-Item -ItemType Directory -Force $distDir  | Out-Null

$clArgs = @(
    '/nologo',
    '/std:c++20',
    '/EHa',
    '/O2',
    '/MT',
    '/LD',
    '/W3',
    '/D_CRT_SECURE_NO_WARNINGS',
    "/I`"$sdk`"",
    "/Fe:`"$outDll`"",
    "/Fo:`"$obj`"",
    "`"$src`""
) -join ' '

$command = "`"$vcvars`" >nul && cl.exe $clArgs"
cmd.exe /c $command
if ($LASTEXITCODE -ne 0) {
    throw "FrameFix build failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $outDll"
Write-Host 'Copy it to Ashita/plugins/ and load it in game with: /load framefix'
