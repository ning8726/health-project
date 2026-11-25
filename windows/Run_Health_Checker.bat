@echo off
setlocal
cd /d "%~dp0.."

where py >nul 2>&1
if errorlevel 1 (
  echo Python 3 was not found. Install it from https://www.python.org/downloads/windows/
  pause
  exit /b 1
)

py -3 -c "import serial" >nul 2>&1
if errorlevel 1 (
  echo Installing the Windows serial library pyserial...
  py -3 -m pip install --user -r "%~dp0requirements.txt"
  if errorlevel 1 (
    echo Installation failed. Check the network connection and try again.
    pause
    exit /b 1
  )
)

set "VIEWER_ARGS="
if /i "%~1"=="--phone" set "VIEWER_ARGS=--host 0.0.0.0"

py -3 "%~dp0check_health.py" --usb %VIEWER_ARGS%
if errorlevel 1 pause
