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
rem
rem The Windows version/architecture check below is checked again, independently, inside
rem OpenInputBridgeSetup.exe itself (see common.h's IsSupportedWindowsEnvironment) — this
rem batch-level copy exists only so an obviously unsupported system fails fast, before ever
rem showing a UAC prompt, instead of self-elevating first and then failing four times over
rem (once per OpenInputBridgeSetup.exe call below).

for /f %%A in ('powershell -NoProfile -Command "if ([Environment]::Is64BitOperatingSystem -and [System.Environment]::OSVersion.Version.Build -ge 22000) { 'OK' } else { 'NG' }"') do set OIB_VERCHECK=%%A
if not "%OIB_VERCHECK%"=="OK" (
    echo This is the wrong Windows version. It's for Windows 11.
    pause
    exit /b 1
)

rem Driver installation (SetupInstallServicesFromInfSectionW, called from install.cpp) refuses to
rem run from a process that isn't native to the host CPU architecture (ERROR_IN_WOW64) — an x64
rem OpenInputBridgeSetup.exe running under Windows' x64-on-ARM64 emulation cannot complete
rem installation on an ARM64 host, confirmed on real hardware (see docs/DECISIONS.md's ARM64
rem entry). [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture reports the
rem actual OS architecture regardless of this script's own (possibly emulated) process
rem architecture, unlike %PROCESSOR_ARCHITECTURE%.
for /f %%A in ('powershell -NoProfile -Command "[System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture"') do set OIB_OSARCH=%%A
if "%OIB_OSARCH%"=="Arm64" (
    set OIB_EXE=OpenInputBridgeSetup-arm64.exe
) else (
    set OIB_EXE=OpenInputBridgeSetup.exe
)

net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"

echo Installing OpenInputBridge...
%OIB_EXE%

echo.
echo Enabling audit-log (Security event log auditing of control device access)...
%OIB_EXE% --enable-audit-log

echo.
echo Enabling toast notifications...
%OIB_EXE% --enable-toast

echo.
echo Verifying installation...
%OIB_EXE% --verify-install

echo.
echo Done. A reboot is required before the driver and the audit-log SACL take effect.
pause
