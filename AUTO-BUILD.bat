@echo off
rem Leave this window open (minimise it): it builds each update Claude marks ready, by itself.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AUTO-BUILD.ps1"
pause
