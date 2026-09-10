# Pulls the Applet LLC Interception fork's own signed build output
# (https://github.com/Applet-LLC/Interception, a separate repository — see that repo's
# packaging/Build-Release.ps1) into packaging\redist\, for stage-redist (sign.mak) to fold
# into Signed\redist\ and ultimately dist\OpenInputBridge.zip. Mirrors OpenInputBridge-Pro's
# Update-DriverPackage.ps1 (same "always copy a pre-built tree, never rebuild it here"
# pattern) — this repo doesn't build interception.dll itself: it's a third-party (LGPL-3.0)
# convenience redistributable, built and EV-signed in that other repo, on its own schedule.
#
# Usage:
#   .\Update-InterceptionRedist.ps1 -SourceDir "..\Interception\packaging\Signed"
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceDir
)

$ErrorActionPreference = "Stop"

$destDir = Join-Path $PSScriptRoot "redist"

if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "Source directory not found: $SourceDir"
}

if (Test-Path $destDir) {
    Remove-Item -Path $destDir -Recurse -Force
}
New-Item -ItemType Directory -Path $destDir -Force | Out-Null

Write-Host "Copying $SourceDir -> $destDir"
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $destDir -Recurse -Force

$required = @(
    "x64\interception.dll",
    "arm64\interception.dll",
    "THIRD-PARTY-NOTICES.txt",
    "LGPL-3.0.txt"
)
$missing = $required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $destDir $_)) }
if ($missing.Count -gt 0) {
    Write-Warning "redist\ is missing expected files:`n$($missing -join "`n")"
}
else {
    Write-Host "redist\ updated successfully."
}
