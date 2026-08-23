param(
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $repoRoot "bin\$Platform\$Configuration"
$releaseDir = Join-Path $repoRoot "release"

$targets = @(
    "FolToolG.exe",
    "FolToolCli.exe"
)

if (-not (Test-Path $sourceDir)) {
    throw "Build output not found: $sourceDir. Run 'msbuild pixel-fol-tool.sln /p:Configuration=$Configuration /p:Platform=$Platform' first."
}

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
Get-ChildItem -LiteralPath $releaseDir -Filter "*.pdb" -File |
    Remove-Item -Force

$published = 0
foreach ($target in $targets) {
    $src = Join-Path $sourceDir $target
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination $releaseDir -Force
        $published++
        Write-Host "[+] $target -> $releaseDir"
    }
}

if ($published -eq 0) {
    throw "No publishable files found in $sourceDir"
}

Write-Host "Publish completed: $published file(s) in $releaseDir"
