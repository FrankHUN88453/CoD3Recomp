@echo off
rem Rebuilds only CoD3.exe (after a change to the port's own code), then puts it in the game folder.
echo Rebuilding CoD3.exe... output goes to build-log.txt. Do not close this window.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BUILD-CONTROLLER-FIX.ps1" -Quick > "%~dp0build-log.txt" 2>&1
findstr /C:"Version" /C:"Done." /C:"STOPPED" /C:"error:" "%~dp0build-log.txt"
echo.
pause
