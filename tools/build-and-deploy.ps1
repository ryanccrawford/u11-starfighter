param(
    [string]$Serial = 'U11000000000094435',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$drive = $projectRoot.Substring(0, 1).ToLowerInvariant()
$wslProjectRoot = "/mnt/$drive$($projectRoot.Substring(2).Replace('\', '/'))"

if (-not (Test-Path -LiteralPath $projectRoot) -or -not $wslProjectRoot.StartsWith('/mnt/')) {
    throw 'Could not resolve the project path for Ubuntu-24.04 WSL.'
}

# These builds write to separate output trees (out/windows-ninja and
# build-arm64), so they can consume the desktop and WSL CPU cores together.
$logDirectory = Join-Path $env:TEMP 'u11-shooter-build'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$windowsOutLog = Join-Path $logDirectory 'windows.stdout.log'
$windowsErrLog = Join-Path $logDirectory 'windows.stderr.log'
$arm64OutLog = Join-Path $logDirectory 'arm64.stdout.log'
$arm64ErrLog = Join-Path $logDirectory 'arm64.stderr.log'
$powerShell = (Get-Command pwsh -ErrorAction Stop).Source
$wsl = (Get-Command wsl -ErrorAction Stop).Source

$windowsBuild = Start-Process -FilePath $powerShell -ArgumentList @(
    '-NoProfile', '-File', "$PSScriptRoot\build-windows.ps1", '-Configuration', $Configuration
) -RedirectStandardOutput $windowsOutLog -RedirectStandardError $windowsErrLog -PassThru

$arm64Build = Start-Process -FilePath $wsl -ArgumentList @(
    '-d', 'Ubuntu-24.04', '--', 'bash', '-lc', "cd '$wslProjectRoot' && bash tools/build-arm64-wsl.sh"
) -RedirectStandardOutput $arm64OutLog -RedirectStandardError $arm64ErrLog -PassThru

Wait-Process -InputObject $windowsBuild, $arm64Build

if ($windowsBuild.ExitCode -ne 0) {
    Get-Content -LiteralPath $windowsOutLog, $windowsErrLog -Tail 80
    throw "Windows x64 build failed with exit code $($windowsBuild.ExitCode). Tablet deployment was not attempted."
}
if ($arm64Build.ExitCode -ne 0) {
    Get-Content -LiteralPath $arm64OutLog, $arm64ErrLog -Tail 80
    throw "Tablet ARM64 build failed with exit code $($arm64Build.ExitCode). Tablet deployment was not attempted."
}

& "$PSScriptRoot\sync-tablet.ps1" -Serial $Serial
if ($LASTEXITCODE -ne 0) { throw 'Tablet synchronization failed.' }

Write-Host 'Windows x64 and tablet ARM64 builds are current; the tablet is running the fresh title screen.'
