param(
    [string]$Serial = 'U11000000000094435',
    # Build the bundle only (no adb push / install / launch). Prints where the bundle is.
    [switch]$BundleOnly,
    # Install but leave the game stopped (default: launch the title screen after installing).
    [switch]$NoLaunch,
    # Test hook: make the direct install step fail on purpose so the u11install.sh fallback runs.
    [switch]$TestFailDirect
)

# 'Stop' is for PowerShell's own errors only. adb writes progress / warnings to stderr, and under
# Windows PowerShell 5.1 a redirected native stderr line becomes a terminating error with 'Stop',
# which used to abort the script before the fallback could run. Every adb call goes through Adb.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$parentDirectory = Split-Path -Parent $projectRoot
$projectName = Split-Path -Leaf $projectRoot
$binary = Join-Path $projectRoot 'shooter-arm64'
$bundle = Join-Path $env:TEMP 'shooter-tablet-sync.tar.gz'

if (-not (Test-Path -LiteralPath $binary)) {
    throw "ARM64 binary not found at $binary. Run tools/build-arm64-wsl.sh from Ubuntu-24.04 WSL first."
}

# Screenshots live only in the project root. bsdtar's '*' also matches '/', so a pattern like
# "$projectName/*.png" would drop every PNG in assets/ too. Exclude the root PNGs by exact path instead.
$rootPngExcludes = Get-ChildItem -LiteralPath $projectRoot -File -Filter *.png |
    ForEach-Object { "--exclude=$projectName/$($_.Name)" }

Push-Location $parentDirectory
try {
    tar --exclude="$projectName/build" --exclude="$projectName/build-arm64" --exclude="$projectName/out" --exclude="$projectName/tools/arm64-raylib" --exclude="$projectName/tools/arm64-json" --exclude="$projectName/tools/arm64-gl" --exclude="$projectName/shooter" --exclude="$projectName/shooter-arm64" @rootPngExcludes --exclude="$projectName/*.xwd" --exclude="$projectName/*.log" -czf $bundle $projectName
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

if ($BundleOnly) {
    Write-Host "Bundle only: $bundle (nothing pushed to the tablet)."
    return
}

$adb = (Get-Command adb -ErrorAction Stop).Source

# Run adb; return its stdout+stderr as plain strings and never throw on stderr. Exit code in $script:AdbExit.
function Adb {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & $adb -s $Serial @args 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $prev }
    $script:AdbExit = $LASTEXITCODE
    return $out
}

# Root commands: never pass a quoted command line through `adb shell` (PowerShell 5.1 strips the
# nested quotes, so only the first word ran as root). Write the commands to a file, push it, run it
# with `su 0 sh <file>`: every argument is a plain word.
function Invoke-RootScript([string]$name, [string]$body) {
    $local = Join-Path $env:TEMP $name
    [System.IO.File]::WriteAllText($local, ($body -replace "`r", '') + "`n", (New-Object System.Text.UTF8Encoding($false)))
    Adb push $local "/data/local/tmp/$name" | Out-Null
    if ($script:AdbExit -ne 0) { throw "adb push of $name failed (exit $script:AdbExit)" }
    return (Adb shell su 0 sh "/data/local/tmp/$name")
}

Adb push $bundle /data/local/tmp/shooter-tablet-sync.tar.gz | Select-Object -Last 1 | Write-Host
if ($script:AdbExit -ne 0) { throw "adb push of the bundle failed (exit $script:AdbExit)" }
Adb push $binary /data/local/tmp/shooter-arm64 | Select-Object -Last 1 | Write-Host
if ($script:AdbExit -ne 0) { throw "adb push of the binary failed (exit $script:AdbExit)" }

$expected = (Get-FileHash -LiteralPath $binary -Algorithm MD5).Hash.ToLower()
function Get-InstalledMd5 {
    $line = Adb shell su 0 md5sum /data/linux/root/shooter/shooter | Where-Object { $_ -match '^[0-9a-f]{32}\s' } | Select-Object -First 1
    if ($line) { ($line -split '\s+')[0].Trim().ToLower() } else { '' }
}

$failHook = if ($TestFailDirect) { 'echo TEST_FAIL_DIRECT; false && ' } else { '' }
$install = 'mkdir -p /data/linux/root && tar xzf /data/local/tmp/shooter-tablet-sync.tar.gz -C /data/linux/root && install -m 0755 /data/local/tmp/shooter-arm64 /data/linux/root/shooter/shooter && touch /data/linux/root/shooter/shooter && echo INSTALLED'
$launch = if ($NoLaunch) { 'pkill -x shooter || true' } else { 'pkill -x shooter || true; sleep 1; sh /data/local/run-shooter.sh; echo RUN_EXIT=$?' }

# 1) Direct root install. INSTALLED is printed only if the extract and install both succeed.
$out = Invoke-RootScript 'u11direct.sh' "$failHook$install; $launch"
$out | Where-Object { $_ -match 'INSTALLED|TEST_FAIL|denied|rror|RUN_EXIT' } | ForEach-Object { Write-Host "  direct: $_" }
$ok = (($out | Out-String) -match '(?m)^INSTALLED') -and ((Get-InstalledMd5) -eq $expected)
$via = 'direct root install'

# 2) Fallback (su over Wi-Fi often fails silently): the detached /data/local/tmp/u11install.sh method.
if (-not $ok) {
    Write-Warning 'Direct root install did not confirm; trying the /data/local/tmp/u11install.sh fallback.'
    $via = 'u11install.sh fallback'
    $fbLocal = Join-Path $env:TEMP 'u11install.sh'
    [System.IO.File]::WriteAllText($fbLocal, "$install; $launch`n", (New-Object System.Text.UTF8Encoding($false)))
    Adb push $fbLocal /data/local/tmp/u11install.sh | Out-Null
    Adb shell rm -f /data/local/tmp/u11install.out | Out-Null
    Invoke-RootScript 'u11go.sh' 'nohup sh /data/local/tmp/u11install.sh > /data/local/tmp/u11install.out 2>&1 &' | Out-Null
    for ($i = 0; $i -lt 60 -and -not $ok; $i++) {
        Start-Sleep -Seconds 2
        $fb = Adb shell cat /data/local/tmp/u11install.out
        if (($fb | Out-String) -match '(?m)^INSTALLED') {
            # u11install.sh prints INSTALLED only after extract && install succeed. Compare the md5 too when su can read it.
            $m = Get-InstalledMd5
            $ok = (-not $m) -or ($m -eq $expected)
            if ($m -and -not $ok) { throw "Installed shooter md5 $m does not match expected $expected" }
        }
    }
}

if (-not $ok) {
    throw "Install NOT confirmed on the tablet (expected shooter md5 $expected). Nothing was reported as success."
}
$state = if ($NoLaunch) { 'game left stopped' } else { 'latest game launched at the title screen' }
Write-Host "Tablet synchronized via $via (shooter md5 $expected); $state."
