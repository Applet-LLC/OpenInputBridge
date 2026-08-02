@echo off
call "K:\BuildEnv\SetupBuildEnv.cmd" amd64
if errorlevel 1 exit /b 1
msbuild /t:rebuild "%~dp0keyboard\keyboard.vcxproj"
if errorlevel 1 exit /b 1
msbuild /t:rebuild "%~dp0mouse\mouse.vcxproj"
if errorlevel 1 exit /b 1
