@echo off
rem Double-click this to build CoD3.exe with the controller fix.
echo Building... this window stays quiet; everything goes to build-log.txt beside this file.
echo Do not close this window. It says Done or STOPPED at the end.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BUILD-CONTROLLER-FIX.ps1" > "%~dp0build-log.txt" 2>&1
findstr /C:"Done." /C:"STOPPED" /C:"Command failed" /C:"error:" "%~dp0build-log.txt"
echo.
pause
