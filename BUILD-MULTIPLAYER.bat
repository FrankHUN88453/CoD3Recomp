@echo off
rem Builds CoD3MP.exe, the multiplayer program. Output goes to build-mp-log.txt.
echo Building multiplayer... this window stays quiet; everything goes to build-mp-log.txt.
echo Do not close this window. It says Done or STOPPED at the end.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BUILD-MULTIPLAYER.ps1" > "%~dp0build-mp-log.txt" 2>&1
findstr /C:"Done." /C:"STOPPED" /C:"error:" "%~dp0build-mp-log.txt"
echo.
pause
