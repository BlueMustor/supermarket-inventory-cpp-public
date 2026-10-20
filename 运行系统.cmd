@echo off
chcp 65001 >nul
pushd "%~dp0"
if not exist "bin\supermarket.exe" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1"
    if errorlevel 1 goto failed
)
"bin\supermarket.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
echo The program could not start. Read the error above and README.md.
popd
pause
exit /b 1
