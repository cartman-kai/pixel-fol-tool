param(
    [Parameter(Mandatory = $true)]
    [string]$CliPath,

    [string]$InputFol = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$cli = Resolve-Path (Join-Path $repoRoot $CliPath)
$runRoot = Join-Path $repoRoot "tmp\run-roundtrip"
$sourceWorkspace = Join-Path $runRoot "source-workspace"
$sourceFol = Join-Path $runRoot "source.fol"
$workspace = Join-Path $runRoot "workspace"
$repacked = Join-Path $runRoot "repacked.fol"
$verify = Join-Path $runRoot "verify"

if (Test-Path $runRoot) {
    Remove-Item -LiteralPath $runRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $workspace | Out-Null
New-Item -ItemType Directory -Force -Path $verify | Out-Null

if ($InputFol) {
    $inputPath = if ([System.IO.Path]::IsPathRooted($InputFol)) {
        $InputFol
    }
    else {
        Join-Path $repoRoot $InputFol
    }
    $input = Resolve-Path $inputPath
}
else {
    New-Item -ItemType Directory -Force -Path (Join-Path $sourceWorkspace "nested") | Out-Null

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllBytes(
        (Join-Path $sourceWorkspace "alpha.txt"),
        $utf8NoBom.GetBytes("synthetic fixture`n")
    )
    [System.IO.File]::WriteAllBytes(
        (Join-Path $sourceWorkspace "nested\odd.bin"),
        [byte[]](0, 1, 2, 3, 4, 250, 251)
    )

    & $cli "pack" $sourceWorkspace $sourceFol
    if ($LASTEXITCODE -ne 0) {
        throw "Synthetic fixture pack failed with exit code $LASTEXITCODE"
    }
    $input = Resolve-Path $sourceFol
}

& $cli "unpack" $input $workspace
if ($LASTEXITCODE -ne 0) {
    throw "Unpack failed with exit code $LASTEXITCODE"
}

$target = Get-ChildItem -LiteralPath $workspace -Recurse -File | Select-Object -First 1
if (-not $target) {
    throw "No extracted files found under $workspace"
}

$marker = [System.Text.Encoding]::UTF8.GetBytes("`nROUNDTRIP_MARKER=shared-core`n")
$stream = [System.IO.File]::Open($target.FullName, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write)
try {
    $stream.Write($marker, 0, $marker.Length)
}
finally {
    $stream.Dispose()
}

& $cli "pack" $workspace $repacked
if ($LASTEXITCODE -ne 0) {
    throw "Pack failed with exit code $LASTEXITCODE"
}

& $cli "unpack" $repacked $verify
if ($LASTEXITCODE -ne 0) {
    throw "Verification unpack failed with exit code $LASTEXITCODE"
}

$relative = $target.FullName.Substring($workspace.Length).TrimStart('\', '/')
$verifiedPath = Join-Path $verify $relative
if (-not (Test-Path $verifiedPath)) {
    throw "Verified file missing: $verifiedPath"
}

$expectedBytes = [System.IO.File]::ReadAllBytes($target.FullName)
$actualBytes = [System.IO.File]::ReadAllBytes($verifiedPath)

if ($expectedBytes.Length -ne $actualBytes.Length) {
    throw "Length mismatch for $relative"
}

for ($i = 0; $i -lt $expectedBytes.Length; $i++) {
    if ($expectedBytes[$i] -ne $actualBytes[$i]) {
        throw "Content mismatch for $relative at byte offset $i"
    }
}

Write-Host "Round-trip verification passed for $relative"
