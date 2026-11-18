@echo off
rem ---------------------------------------------------------------
rem  Windows launcher: double-click to start the C backend (in WSL)
rem  and open the dashboard in your browser.  Close this window or
rem  press Ctrl+C to stop.
rem ---------------------------------------------------------------
title Smart Vehicle Safety Dashboard (C backend)
cd /d "%~dp0"
wsl --cd "%CD%" -e make start
echo.
echo Server stopped.
pause
