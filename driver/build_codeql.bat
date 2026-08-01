@echo off
call "K:\BuildEnv\SetupBuildEnv.cmd" amd64
if errorlevel 1 exit /b 1
msbuild /t:rebuild "%~dp0OpenInputBridge.vcxproj"
