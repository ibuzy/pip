@echo off
rem The panel (http://localhost:8465) and the web simulator (http://localhost:8466) on this PC.
rem They run while this console window is open; closing it stops both. Needs Python (tools\setup-windows.ps1 has it).
chcp 65001 >nul
title Pip: панель и симулятор (закрой окно, чтобы остановить)
cd /d "%~dp0"
if not exist "sim\web\pip.wasm" echo   Симулятор не собран: tools\sim.ps1 web-build
where py >nul 2>nul && (py -3 tools\serve_local.py & goto :done)
where python >nul 2>nul && (python tools\serve_local.py & goto :done)
echo   Python не найден. Установи его (tools\setup-windows.ps1) или с python.org
:done
pause
