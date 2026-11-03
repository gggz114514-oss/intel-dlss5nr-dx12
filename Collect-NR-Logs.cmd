@echo off
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0tools\intel-nr\Launch-Package.ps1" -CollectLogs %*
if errorlevel 1 (echo FAILED) else (echo DONE)
pause
