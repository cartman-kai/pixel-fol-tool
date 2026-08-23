param(
  [string]$TagName = "",
  [string]$Configuration = "Release",
  [string]$PackageDir = "release_zip",
  [string]$ReleaseNotesPath = "release_notes.md",
  [string]$ArchivePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-PackageVersion {
  param([string]$VersionRc)

  $line = Get-Content -LiteralPath $VersionRc -Encoding UTF8 |
    Where-Object { $_ -match '^\s*FILEVERSION\s+' } |
    Select-Object -First 1

  if (-not $line) {
    throw "Missing FILEVERSION in $VersionRc"
  }
  if ($line -match '^\s*FILEVERSION\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)') {
    return "{0}.{1}.{2}.{3}" -f $matches[1], $matches[2], $matches[3], $matches[4]
  }
  throw "Unparseable FILEVERSION line: $line"
}

function Get-ChangelogSection {
  param(
    [string]$ChangelogPath,
    [string]$TagName
  )

  if ([string]::IsNullOrWhiteSpace($TagName)) {
    throw "TagName is required to extract release notes"
  }
  if (-not (Test-Path -LiteralPath $ChangelogPath)) {
    return $null
  }

  $lines = Get-Content -LiteralPath $ChangelogPath -Encoding UTF8
  $headingPattern = '^(#{1,6})\s+' + [regex]::Escape($TagName) + '\s*$'
  $startIndex = -1
  $headingLevel = 0
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match $headingPattern) {
      $startIndex = $i
      $headingLevel = $matches[1].Length
      break
    }
  }

  if ($startIndex -lt 0) {
    return $null
  }

  $endIndex = $lines.Count
  for ($i = $startIndex + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^(#{1,' + $headingLevel + '})\s+') {
      $endIndex = $i
      break
    }
  }

  return @($lines[$startIndex..($endIndex - 1)])
}

function Copy-Payload {
  param(
    [string]$SourceDir,
    [string]$DestinationDir
  )

  $targets = @(
    "FolToolG.exe",
    "FolToolG.pdb",
    "FolToolCli.exe",
    "FolToolCli.pdb"
  )

  if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "Build output not found: $SourceDir"
  }

  New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
  foreach ($target in $targets) {
    $src = Join-Path $SourceDir $target
    if (Test-Path -LiteralPath $src) {
      Copy-Item -LiteralPath $src -Destination $DestinationDir -Force
    }
  }
}

$packageVersion = Get-PackageVersion -VersionRc (Join-Path $repoRoot "gui\FolToolWin.rc")
if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
  $ArchivePath = "pixel-fol-tool_${packageVersion}_win.zip"
}
$ArchivePath = Join-Path $repoRoot $ArchivePath

if (-not [string]::IsNullOrWhiteSpace($TagName)) {
  $expectedTag = "v" + ($packageVersion -replace '\.0$', '')
  if ($TagName -ne $expectedTag) {
    Write-Warning "Tag '$TagName' does not match package version '$packageVersion' (expected tag '$expectedTag')."
  }
}

$releaseNoteLines = Get-ChangelogSection -ChangelogPath (Join-Path $repoRoot "CHANGELOG.md") -TagName $TagName
if ($null -eq $releaseNoteLines) {
  $releaseNoteLines = @(
    "# pixel-fol-tool $TagName",
    "",
    "本次发布对应的变更记录见 [CHANGELOG.md](CHANGELOG.md) 中 $TagName 章节（若存在）。"
  )
}
$releaseNoteLines | Set-Content -LiteralPath (Join-Path $repoRoot $ReleaseNotesPath) -Encoding UTF8

if (Test-Path -LiteralPath $PackageDir) {
  Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

Copy-Payload -SourceDir (Join-Path $repoRoot "bin\x64\$Configuration") -DestinationDir (Join-Path $PackageDir "win-x64")
Copy-Payload -SourceDir (Join-Path $repoRoot "bin\Win32\$Configuration") -DestinationDir (Join-Path $PackageDir "win-x86")

Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $ArchivePath -Force

Write-Output "VERSION=$packageVersion"
Write-Output "ARCHIVE_PATH=$ArchivePath"
Write-Output "RELEASE_NOTES_PATH=$(Join-Path $repoRoot $ReleaseNotesPath)"
Write-Output "PACKAGE_DIR=$PackageDir"
