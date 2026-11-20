<#
.SYNOPSIS
  Sync U11 Starfighter asset binaries between the local assets\ folder and the shared folder.

.DESCRIPTION
  Asset binaries are not stored in git. assets.manifest lists each shared file's
  path, size, and SHA256. See ASSETS.md.

.PARAMETER Mode
  pull   - Copy missing or changed files from the share into local assets\.
           Never deletes local files.
  push   - Copy new or changed local assets to the share, update assets.manifest,
           and print what changed. Never deletes share files. Never overwrites a
           share file with an older or identical copy. When replacing a newer
           local version, keeps the previous share file as .prev-<timestamp>.
  verify - Report mismatches between local assets, the share, and the manifest.

.PARAMETER SharePath
  Root of the asset share (same layout as assets\). Default: \\HOME-SERVER\u11-assets
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0, Mandatory = $true)]
  [ValidateSet('pull', 'push', 'verify')]
  [string]$Mode,

  [string]$SharePath = '\\HOME-SERVER\u11-assets'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BinaryExtensions = @(
  '.png', '.jpg', '.jpeg', '.glb', '.gltf', '.bin',
  '.blend', '.blend1', '.wav', '.ogg', '.mp3', '.ttf', '.obj', '.mp4'
)

function Get-RepoRoot {
  $toolsDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  return (Resolve-Path (Join-Path $toolsDir '..')).Path
}

function ConvertTo-RepoRelPath([string]$FullPath, [string]$RepoRoot) {
  $full = [IO.Path]::GetFullPath($FullPath)
  $root = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + '\'
  if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside the repo: $FullPath"
  }
  return $full.Substring($root.Length) -replace '\\', '/'
}

function ConvertTo-SharePath([string]$RepoRelPath, [string]$ShareRoot) {
  if ($RepoRelPath -notmatch '^assets/') {
    throw "Manifest path is not under assets/: $RepoRelPath"
  }
  $under = $RepoRelPath.Substring('assets/'.Length) -replace '/', '\'
  return (Join-Path $ShareRoot $under)
}

function ConvertTo-LocalPath([string]$RepoRelPath, [string]$RepoRoot) {
  return (Join-Path $RepoRoot ($RepoRelPath -replace '/', '\'))
}

function Get-FileSha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-Manifest([string]$ManifestPath) {
  $entries = @()
  if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    return $entries
  }
  $lineNo = 0
  foreach ($raw in [IO.File]::ReadAllLines($ManifestPath)) {
    $lineNo++
    $line = $raw.Trim()
    if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }
    if ($line -notmatch '^([0-9a-fA-F]{64})\s+(\d+)\s+(\S.*)$') {
      throw "Invalid assets.manifest line ${lineNo}: $raw"
    }
    $entries += [PSCustomObject]@{
      Sha256 = $Matches[1].ToLowerInvariant()
      Size   = [int64]$Matches[2]
      Path   = ($Matches[3].Trim() -replace '\\', '/')
    }
  }
  return $entries
}

function Write-Manifest([string]$ManifestPath, $Entries) {
  $sorted = $Entries | Sort-Object Path, { $_.Path }
  $lines = New-Object System.Collections.Generic.List[string]
  [void]$lines.Add('# assets.manifest')
  [void]$lines.Add('# SHA256  SIZE_BYTES  PATH (relative to repo root)')
  [void]$lines.Add('# Updated by tools/assets-sync.ps1. Asset binaries are not in git. See ASSETS.md.')
  foreach ($e in $sorted) {
    [void]$lines.Add(('{0} {1} {2}' -f $e.Sha256, $e.Size, $e.Path))
  }
  $text = [string]::Join("`n", $lines.ToArray()) + "`n"
  [IO.File]::WriteAllText($ManifestPath, $text, (New-Object System.Text.UTF8Encoding $false))
}

function Test-IsExcludedAsset([IO.FileInfo]$File, [string]$AssetsDir) {
  $full = $File.FullName
  if ($full -match '[\\/]__pycache__[\\/]') { return $false }
  # Ignore share backup copies if they ever appear locally
  if ($File.Name -match '\.prev-\d{8}-\d{6}$') { return $false }
  $ext = $File.Extension.ToLowerInvariant()
  if ($BinaryExtensions -contains $ext) { return $true }
  if ($File.Length -ge 1MB) { return $true }
  return $false
}

function Get-LocalExcludedAssets([string]$RepoRoot) {
  $assetsDir = Join-Path $RepoRoot 'assets'
  if (-not (Test-Path -LiteralPath $assetsDir -PathType Container)) {
    return @()
  }
  $list = @()
  Get-ChildItem -LiteralPath $assetsDir -Recurse -File -Force | ForEach-Object {
    if (Test-IsExcludedAsset $_ $assetsDir) {
      $rel = ConvertTo-RepoRelPath $_.FullName $RepoRoot
      $list += [PSCustomObject]@{
        Path     = $rel
        FullName = $_.FullName
        Size     = $_.Length
        Utc      = $_.LastWriteTimeUtc
      }
    }
  }
  return $list
}

function Test-ShareReachable([string]$ShareRoot) {
  return (Test-Path -LiteralPath $ShareRoot -PathType Container)
}

function Copy-FileTo([string]$Source, [string]$Destination) {
  $dir = Split-Path -Parent $Destination
  if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
  }
  Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Get-ShareFileList([string]$ShareRoot) {
  if (-not (Test-Path -LiteralPath $ShareRoot -PathType Container)) { return @() }
  $list = @()
  Get-ChildItem -LiteralPath $ShareRoot -Recurse -File -Force -ErrorAction Stop | ForEach-Object {
    if ($_.Name -match '\.prev-\d{8}-\d{6}$') { return }
    $fullShare = [IO.Path]::GetFullPath($ShareRoot).TrimEnd('\', '/') + '\'
    $rel = $_.FullName.Substring($fullShare.Length) -replace '\\', '/'
    $list += [PSCustomObject]@{
      Path     = 'assets/' + $rel
      FullName = $_.FullName
      Size     = $_.Length
    }
  }
  return $list
}

$RepoRoot = Get-RepoRoot
$ManifestPath = Join-Path $RepoRoot 'assets.manifest'
$ShareRoot = $SharePath

if (-not (Test-ShareReachable $ShareRoot)) {
  Write-Error "Asset share is not reachable: $ShareRoot"
  exit 1
}

switch ($Mode) {
  'pull' {
    $manifest = @(Read-Manifest $ManifestPath)
    if ($manifest.Count -eq 0) {
      Write-Error "assets.manifest is missing or empty: $ManifestPath"
      exit 1
    }
    $copied = 0
    $ok = 0
    $missingShare = @()
    foreach ($e in $manifest) {
      $local = ConvertTo-LocalPath $e.Path $RepoRoot
      $remote = ConvertTo-SharePath $e.Path $ShareRoot
      $need = $false
      if (-not (Test-Path -LiteralPath $local -PathType Leaf)) {
        $need = $true
      } else {
        $localHash = Get-FileSha256 $local
        if ($localHash -ne $e.Sha256) { $need = $true }
      }
      if (-not $need) { $ok++; continue }
      if (-not (Test-Path -LiteralPath $remote -PathType Leaf)) {
        $missingShare += $e.Path
        Write-Warning "Missing on share, cannot pull: $($e.Path)"
        continue
      }
      Copy-FileTo $remote $local
      $copied++
      Write-Output "pulled  $($e.Path)"
    }
    Write-Output ""
    Write-Output ("pull complete: {0} copied, {1} already matched manifest, {2} missing on share" -f $copied, $ok, $missingShare.Count)
    if ($missingShare.Count -gt 0) { exit 1 }
  }

  'push' {
    $localAssets = @(Get-LocalExcludedAssets $RepoRoot)
    $manifest = @(Read-Manifest $ManifestPath)
    $manifestMap = @{}
    foreach ($e in $manifest) { $manifestMap[$e.Path] = $e }

    $newFiles = @()
    $changed = @()
    $skippedIdentical = 0
    $skippedOlder = @()
    $backedUp = @()

    foreach ($loc in $localAssets) {
      $hash = Get-FileSha256 $loc.FullName
      $remote = ConvertTo-SharePath $loc.Path $ShareRoot
      $onShare = Test-Path -LiteralPath $remote -PathType Leaf

      if (-not $onShare) {
        Copy-FileTo $loc.FullName $remote
        $newFiles += $loc.Path
        Write-Output "new     $($loc.Path)"
        continue
      }

      $shareHash = Get-FileSha256 $remote
      if ($shareHash -eq $hash) {
        $skippedIdentical++
        continue
      }

      $shareUtc = (Get-Item -LiteralPath $remote).LastWriteTimeUtc
      if ($loc.Utc -lt $shareUtc) {
        $skippedOlder += $loc.Path
        Write-Warning ("skipped older local file (share is newer): {0}" -f $loc.Path)
        continue
      }

      $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
      $prev = $remote + '.prev-' + $stamp
      Copy-Item -LiteralPath $remote -Destination $prev -Force
      Copy-FileTo $loc.FullName $remote
      $changed += $loc.Path
      $backedUp += $prev
      Write-Output "updated $($loc.Path)  (previous kept as $(Split-Path $prev -Leaf))"
    }

    # Rebuild manifest from local excluded files (keep entries for local-missing
    # files that are still on the share / in the old manifest; never drop them).
    $newManifest = @()
    $seen = @{}
    foreach ($loc in $localAssets) {
      $hash = Get-FileSha256 $loc.FullName
      $newManifest += [PSCustomObject]@{ Sha256 = $hash; Size = $loc.Size; Path = $loc.Path }
      $seen[$loc.Path] = $true
    }
    $keptMissingLocal = @()
    foreach ($e in $manifest) {
      if ($seen.ContainsKey($e.Path)) { continue }
      $newManifest += $e
      $keptMissingLocal += $e.Path
    }
    foreach ($m in $keptMissingLocal) {
      Write-Warning "local file missing; left manifest entry and share file alone: $m"
    }

    Write-Manifest $ManifestPath $newManifest

    Write-Output ""
    Write-Output ("push complete: {0} new, {1} updated, {2} identical, {3} skipped (older local), {4} local-missing kept in manifest" -f $newFiles.Count, $changed.Count, $skippedIdentical, $skippedOlder.Count, $keptMissingLocal.Count)
    Write-Output "assets.manifest updated. Commit assets.manifest (do not commit binaries)."
  }

  'verify' {
    $manifest = @(Read-Manifest $ManifestPath)
    if ($manifest.Count -eq 0) {
      Write-Error "assets.manifest is missing or empty: $ManifestPath"
      exit 1
    }

    $mismatches = New-Object System.Collections.Generic.List[string]
    $ok = 0

    foreach ($e in $manifest) {
      $local = ConvertTo-LocalPath $e.Path $RepoRoot
      $remote = ConvertTo-SharePath $e.Path $ShareRoot
      $localExists = Test-Path -LiteralPath $local -PathType Leaf
      $shareExists = Test-Path -LiteralPath $remote -PathType Leaf

      if (-not $localExists) {
        [void]$mismatches.Add("missing local:  $($e.Path)")
      }
      if (-not $shareExists) {
        [void]$mismatches.Add("missing share:  $($e.Path)")
      }
      if ($localExists) {
        $lh = Get-FileSha256 $local
        $ls = (Get-Item -LiteralPath $local).Length
        if ($lh -ne $e.Sha256) { [void]$mismatches.Add("local SHA256 != manifest: $($e.Path)") }
        if ($ls -ne $e.Size) { [void]$mismatches.Add("local size != manifest: $($e.Path)") }
      }
      if ($shareExists) {
        $sh = Get-FileSha256 $remote
        $ss = (Get-Item -LiteralPath $remote).Length
        if ($sh -ne $e.Sha256) { [void]$mismatches.Add("share SHA256 != manifest: $($e.Path)") }
        if ($ss -ne $e.Size) { [void]$mismatches.Add("share size != manifest: $($e.Path)") }
      }
      if ($localExists -and $shareExists) {
        # counts as ok only when both match the manifest (already flagged above)
        if ((Get-FileSha256 $local) -eq $e.Sha256 -and (Get-FileSha256 $remote) -eq $e.Sha256) {
          $ok++
        }
      }
    }

    $manifestPaths = @{}
    foreach ($e in $manifest) { $manifestPaths[$e.Path] = $true }

    foreach ($loc in @(Get-LocalExcludedAssets $RepoRoot)) {
      if (-not $manifestPaths.ContainsKey($loc.Path)) {
        [void]$mismatches.Add("local file not in manifest: $($loc.Path)")
      }
    }
    foreach ($sf in @(Get-ShareFileList $ShareRoot)) {
      if (-not $manifestPaths.ContainsKey($sf.Path)) {
        [void]$mismatches.Add("share file not in manifest: $($sf.Path)")
      }
    }

    Write-Output ("verify: {0} manifest entries, {1} matched, {2} mismatches" -f $manifest.Count, $ok, $mismatches.Count)
    foreach ($m in $mismatches) { Write-Output "MISMATCH  $m" }
    if ($mismatches.Count -ne 0) { exit 1 }
    Write-Output "0 mismatches"
  }
}
