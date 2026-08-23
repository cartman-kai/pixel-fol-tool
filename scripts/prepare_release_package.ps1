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
    if ($matches[4] -ne "0") {
      throw "FILEVERSION revision must be 0 for a semantic release: $line"
    }
    return "{0}.{1}.{2}" -f $matches[1], $matches[2], $matches[3]
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
  $headingPattern = '^(#{1,6})\s+' + [regex]::Escape($TagName) + '(?:\s+-\s+.+)?\s*$'
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
    "FolToolCli.exe"
  )

  if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "Build output not found: $SourceDir"
  }

  New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
  foreach ($target in $targets) {
    $src = Join-Path $SourceDir $target
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
      throw "Required release file not found: $src"
    }
    Copy-Item -LiteralPath $src -Destination $DestinationDir -Force
  }
}

$packageVersion = Get-PackageVersion -VersionRc (Join-Path $repoRoot "gui\FolToolWin.rc")
$expectedTag = "v$packageVersion"
if ([string]::IsNullOrWhiteSpace($TagName)) {
  $TagName = $expectedTag
}
elseif ($TagName -ne $expectedTag) {
  throw "Tag '$TagName' does not match package version '$packageVersion' (expected '$expectedTag')."
}

if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
  $ArchivePath = "pixel-fol-tool_${packageVersion}_win.zip"
}
$ArchivePath = if ([System.IO.Path]::IsPathRooted($ArchivePath)) {
  $ArchivePath
}
else {
  Join-Path $repoRoot $ArchivePath
}

$releaseNoteLines = Get-ChangelogSection -ChangelogPath (Join-Path $repoRoot "CHANGELOG.md") -TagName $TagName
if ($null -eq $releaseNoteLines) {
  throw "Missing '$TagName' section in CHANGELOG.md"
}
$releaseNotesFullPath = if ([System.IO.Path]::IsPathRooted($ReleaseNotesPath)) {
  $ReleaseNotesPath
}
else {
  Join-Path $repoRoot $ReleaseNotesPath
}
$releaseNoteLines | Set-Content -LiteralPath $releaseNotesFullPath -Encoding UTF8

$packageFullPath = if ([System.IO.Path]::IsPathRooted($PackageDir)) {
  [System.IO.Path]::GetFullPath($PackageDir)
}
else {
  [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PackageDir))
}
$repoPrefix = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $packageFullPath.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "PackageDir must be inside the repository: $packageFullPath"
}
if (Test-Path -LiteralPath $packageFullPath) {
  Remove-Item -LiteralPath $packageFullPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageFullPath | Out-Null

Copy-Payload -SourceDir (Join-Path $repoRoot "bin\x64\$Configuration") -DestinationDir (Join-Path $packageFullPath "win-x64")
Copy-Payload -SourceDir (Join-Path $repoRoot "bin\Win32\$Configuration") -DestinationDir (Join-Path $packageFullPath "win-x86")
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $packageFullPath -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "README.md") -Destination $packageFullPath -Force

Compress-Archive -Path (Join-Path $packageFullPath "*") -DestinationPath $ArchivePath -Force
$archiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumPath = "$ArchivePath.sha256"
"$archiveHash  $([System.IO.Path]::GetFileName($ArchivePath))" | Set-Content -LiteralPath $checksumPath -Encoding ascii

Write-Output "VERSION=$packageVersion"
Write-Output "TAG_NAME=$TagName"
Write-Output "ARCHIVE_PATH=$ArchivePath"
Write-Output "CHECKSUM_PATH=$checksumPath"
Write-Output "RELEASE_NOTES_PATH=$releaseNotesFullPath"
Write-Output "PACKAGE_DIR=$packageFullPath"
