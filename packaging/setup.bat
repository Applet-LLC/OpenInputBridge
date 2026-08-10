@echo off
rem Copyright (c) 2026 OpenInputBridge Contributors
rem SPDX-License-Identifier: MIT
rem Licensed under the MIT License. See LICENSE file in the project root for full license text.
rem
rem One-click install: runs OpenInputBridgeSetup.exe to install both drivers, then
rem --enable-audit-log and --enable-toast so the audit-log/toast-notification feature
rem (installer/auditlog.cpp, installer/toastsetup.cpp) is on by default for anyone who just
rem runs this script, rather than something only CLI-aware users would discover and opt into.
rem Run --disable-audit-log / --disable-toast afterwards (see README.md) to opt back out.
rem
rem Self-elevates so double-clicking this script (which Explorer runs non-elevated, since a
rem .bat file has no manifest of its own to trigger UAC the way OpenInputBridgeSetup.exe's own
rem does) still works: without this, a non-elevated cmd.exe can't even launch an exe whose
rem manifest requires administrator — CreateProcess fails outright rather than prompting, unlike
rem ShellExecute (Explorer double-click) on the exe directly, which does prompt.

net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"

echo Installing OpenInputBridge...
OpenInputBridgeSetup.exe

echo.
echo Enabling audit-log (Security event log auditing of control device access)...
OpenInputBridgeSetup.exe --enable-audit-log

echo.
echo Enabling toast notifications...
OpenInputBridgeSetup.exe --enable-toast

echo.
echo Done. A reboot is required before the driver and the audit-log SACL take effect.
pause
