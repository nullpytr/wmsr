@echo off
setlocal

set CONFIGURATION=%1
set PLATFORM=%2

if "%CONFIGURATION%"=="" set CONFIGURATION=Release
if "%PLATFORM%"=="" set PLATFORM=x64

set SCRIPT_DIR=%~dp0
set DRIVER_DIR=%SCRIPT_DIR%..\driver

echo Building msr.sys  configuration=%CONFIGURATION%  platform=%PLATFORM%

msbuild "%DRIVER_DIR%\msr.vcxproj" ^
    /p:Configuration=%CONFIGURATION% ^
    /p:Platform=%PLATFORM% ^
    /nologo ^
    /v:minimal

echo Output: %DRIVER_DIR%\%PLATFORM%\%CONFIGURATION%\msr.sys

endlocal
