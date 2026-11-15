@echo off
rem Copyright (c) 2026 OpenInputBridge Contributors
rem SPDX-License-Identifier: MIT
rem
rem Shows the OpenInputBridge control devices (\.\interceptionNN) via OibCtl.exe.
rem Relaunches itself elevated if needed (that opens a new window; its output and exit code do not
rem come back to the caller - run this from an elevated shell, or call OibCtl.exe directly, when
rem scripting). Set OIBCTL_NOPAUSE=1 to skip the final "pause".
setlocal
set "ACTION=show"
set "OIBCTL=%~dp0OibCtl.exe"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" if exist "%~dp0OibCtl-arm64.exe" set "OIBCTL=%~dp0OibCtl-arm64.exe"
if not exist "%OIBCTL%" (
    echo OibCtl.exe not found next to this script.
    pause
    exit /b 3
)
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)
"%OIBCTL%" %ACTION%
set "RC=%ERRORLEVEL%"
if not "%OIBCTL_NOPAUSE%"=="1" pause
exit /b %RC%
