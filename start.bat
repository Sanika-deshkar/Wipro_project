@echo off
rem ======================================================================
rem  Smart Vehicle Safety Dashboard - Windows launcher (double-click me)
rem
rem  1. uses the ready-made dist\svsm.exe  (no install, no WSL needed)
rem  2. else builds it with gcc if MinGW is installed
rem  3. else falls back to WSL (Linux) if available
rem  The server opens your browser by itself. Close this window to stop.
rem  Set NO_BROWSER=1 to skip opening the browser.
rem ======================================================================
title Smart Vehicle Safety Dashboard - C backend
cd /d "%~dp0"

set "OPEN=--open"
if defined NO_BROWSER set "OPEN="

if exist "dist\svsm.exe" goto run_exe

where gcc >nul 2>nul
if errorlevel 1 goto try_wsl
echo dist\svsm.exe not found - building it with gcc...
if not exist dist mkdir dist
gcc -std=gnu11 -O2 -Iinclude -D__USE_MINGW_ANSI_STDIO=1 src\*.c -o dist\svsm.exe -lws2_32 -lshell32
if exist "dist\svsm.exe" goto run_exe

:try_wsl
where wsl >nul 2>nul
if errorlevel 1 goto nothing
echo Using WSL (Linux)...
wsl --cd "%CD%" -e make start
goto done

:nothing
echo.
echo  Could not start: dist\svsm.exe is missing and neither gcc nor WSL is installed.
echo  Fix: copy the "dist" folder from the project, or install MinGW / WSL.
goto done

:run_exe
echo Starting the C backend...
"dist\svsm.exe" --serve 8080 %OPEN%

:done
echo.
echo Server stopped.
pause
