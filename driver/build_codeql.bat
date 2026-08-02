@echo off
call "K:\BuildEnv\SetupBuildEnv.cmd" amd64
if errorlevel 1 exit /b 1
msbuild /t:rebuild "%~dp0keyboard\oib_kbd.vcxproj"
if errorlevel 1 exit /b 1
msbuild /t:rebuild "%~dp0mouse\oib_mou.vcxproj"
if errorlevel 1 exit /b 1
