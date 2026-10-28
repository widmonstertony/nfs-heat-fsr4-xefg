@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore.ps1" %*
if errorlevel 1 echo Restore stopped. No unknown files should be manually deleted.
pause
