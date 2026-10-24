@echo off
rem Optional: starts the auto-builder (minimised) every time you log in to Windows.
rem Run it once. To undo it, delete "CoD3 auto-build" from your Startup folder (Win+R, shell:startup).
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Startup')+'\CoD3 auto-build.lnk'); $s.TargetPath='%~dp0AUTO-BUILD.bat'; $s.WorkingDirectory='%~dp0'; $s.WindowStyle=7; $s.Save()"
echo The auto-builder will now start minimised whenever you log in. Starting it now too.
start "" /min "%~dp0AUTO-BUILD.bat"
pause
