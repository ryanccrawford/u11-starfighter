param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$buildDirectory = Join-Path $projectRoot 'out\windows-ninja'

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Missing the Visual Studio Build Tools x64 environment at $vcvars."
}

$configure = "call `"$vcvars`" && `"$cmake`" -S `"$projectRoot`" -B `"$buildDirectory`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration"
$build = "call `"$vcvars`" && `"$cmake`" --build `"$buildDirectory`" --parallel"

cmd.exe /d /s /c $configure
if ($LASTEXITCODE -ne 0) { throw 'Windows CMake configuration failed.' }

cmd.exe /d /s /c $build
if ($LASTEXITCODE -ne 0) { throw 'Windows CMake build failed.' }

$executable = Join-Path $buildDirectory 'shooter.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Windows build did not produce $executable."
}

Write-Host "Windows build ready: $executable"
