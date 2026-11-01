@echo off
setlocal
cd /d "%~dp0"
if exist ".venv\Scripts\pythonw.exe" (
  start "" ".venv\Scripts\pythonw.exe" "%~dp0SkinLab.pyw"
  exit /b
)
if exist ".tools\python\Scripts\pythonw.exe" (
  start "" ".tools\python\Scripts\pythonw.exe" "%~dp0SkinLab.pyw"
  exit /b
)
where pythonw.exe >nul 2>nul
if not errorlevel 1 (
  start "" pythonw.exe "%~dp0SkinLab.pyw"
  exit /b
)
where pyw.exe >nul 2>nul
if not errorlevel 1 (
  start "" pyw.exe -3 "%~dp0SkinLab.pyw"
  exit /b
)
echo Please install Python 3.10+ with Tkinter and add it to PATH.
pause
