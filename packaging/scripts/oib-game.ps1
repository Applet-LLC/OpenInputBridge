# Copyright (c) 2026 OpenInputBridge Contributors
# SPDX-License-Identifier: MIT
#
# Hides the OpenInputBridge control devices, runs a game/launcher, and shows them again when it
# exits - even if this script is interrupted (Ctrl+C) or the program fails to start.
# Run from an elevated PowerShell.
#
#   .\oib-game.ps1 -Exe "C:\Games\Game\Game.exe" -Arguments "-windowed"
#
# For launchers that hand off to a separate game process and exit (EA app, ...), use
# "OibCtl.exe guard <process name>" instead.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string[]]$Arguments = @()
)

$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run this script from an elevated (administrator) PowerShell.'
    exit 2
}

$oibctl = Join-Path $PSScriptRoot 'OibCtl.exe'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -and (Test-Path (Join-Path $PSScriptRoot 'OibCtl-arm64.exe'))) {
    $oibctl = Join-Path $PSScriptRoot 'OibCtl-arm64.exe'
}
if (-not (Test-Path $oibctl)) {
    Write-Error "OibCtl.exe not found next to this script."
    exit 3
}

& $oibctl hide
if ($LASTEXITCODE -ne 0) {
    if ($LASTEXITCODE -eq 1) {
        Write-Error 'Control devices are in use. Close Interception clients (AutoHotkey, kanata, ...) first. The game was not started.'
    } else {
        Write-Error "OibCtl hide failed (exit code $LASTEXITCODE). The game was not started."
    }
    exit $LASTEXITCODE
}

try {
    $startArgs = @{ FilePath = $Exe; Wait = $true }
    if ($Arguments.Count -gt 0) { $startArgs.ArgumentList = $Arguments }
    Start-Process @startArgs
}
finally {
    & $oibctl show
}
