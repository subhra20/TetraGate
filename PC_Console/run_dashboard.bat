@echo off
REM Starts the LoRa dashboard bridge and opens the browser.
REM Port is auto-detected; override with:  run_dashboard.bat COM7
cd /d "%~dp0"
if "%~1"=="" (
    python lora_bridge.py
) else (
    python lora_bridge.py --port %1
)
pause
