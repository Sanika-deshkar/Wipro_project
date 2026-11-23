@echo off
rem ======================================================================
rem  ONE-LINE LAUNCHER (Windows cmd):  run.bat
rem
rem  Works on any Windows 10/11 PC:
rem    * has MinGW gcc  -> builds a fresh build\svsm.exe from source
rem    * no compiler    -> uses the ready-made dist\svsm.exe
rem    * has Python     -> installs scikit-learn once and enables the ML panel
rem    * no Python      -> runs with rule-based monitoring only
rem
rem     run.bat          interactive terminal dashboard (with ML panel)
rem     run.bat demo     40-second scripted drive
rem     run.bat web      web dashboard in your browser
rem     run.bat noml     rule-based monitoring only
rem  Tip: use Windows Terminal or a maximised window for the full layout.
rem ======================================================================
setlocal enabledelayedexpansion
title Smart Vehicle Safety Monitoring System with ML-Based Risk Prediction
cd /d "%~dp0"

rem ---- 1. program ------------------------------------------------------
set "EXE="
where gcc >nul 2>nul
if not errorlevel 1 (
    echo [1/3] Building the C program with gcc...
    if not exist build mkdir build
    gcc -std=gnu11 -O2 -Iinclude -D__USE_MINGW_ANSI_STDIO=1 src\*.c -o build\svsm.exe -lws2_32 -lshell32
    if not errorlevel 1 (
        set "EXE=build\svsm.exe"
    ) else (
        echo     Build failed ^(is the dashboard still open in another window? close it and retry^).
    )
) else (
    echo [1/3] No compiler found.
)
if not defined EXE (
    if exist dist\svsm.exe (
        echo     Using the ready-made dist\svsm.exe instead.
        set "EXE=dist\svsm.exe"
    )
)
if not defined EXE (
    echo [!] Cannot start: no gcc and no dist\svsm.exe. Install MinGW or copy the dist folder.
    exit /b 1
)

rem ---- 2. ML ---------------------------------------------------------------
set "MLFLAG="
if /i "%~1"=="noml" set "MLFLAG=--no-ml"

if not defined MLFLAG (
    echo [2/3] Checking the ML model...
    set "PY="
    where python >nul 2>nul
    if not errorlevel 1 set "PY=python"
    if not defined PY (
        where py >nul 2>nul
        if not errorlevel 1 set "PY=py -3"
    )
    if not defined PY (
        echo     Python not found - continuing with rule-based monitoring only.
        set "MLFLAG=--no-ml"
    ) else (
        !PY! -c "import sklearn, pandas, joblib" >nul 2>nul
        if errorlevel 1 (
            echo     Installing Python packages ^(one time, needs internet^)...
            !PY! -m pip install -q -r ml\requirements.txt
        )
        !PY! ml\predict.py 50 85 70 40 1 >nul 2>nul
        if errorlevel 1 (
            echo     Training the model for this computer ^(one time, about 20 s^)...
            !PY! ml\train_model.py >nul 2>nul
        )
        !PY! ml\predict.py 50 85 70 40 1 >nul 2>nul
        if errorlevel 1 (
            echo     ML could not be set up - continuing with rule-based monitoring only.
            set "MLFLAG=--no-ml"
        ) else (
            if /i not "!PY!"=="python" set "MLFLAG=--ml-python py"
        )
    )
) else (
    echo [2/3] ML skipped.
)

rem ---- 3. start --------------------------------------------------------------
echo [3/3] Starting...
if /i "%~1"=="demo" (
    %EXE% --demo 40 --seed 7 %MLFLAG%
) else if /i "%~1"=="web" (
    %EXE% --serve 8080 --open %MLFLAG%
) else (
    %EXE% %MLFLAG%
)
endlocal
