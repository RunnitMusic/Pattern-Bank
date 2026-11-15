@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-PatternBank-Windows.ps1"
if errorlevel 1 (
  echo.
  echo Pattern Bank installation failed. Review the message above.
  pause
  exit /b 1
)
echo.
pause

