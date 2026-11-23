#!/bin/sh
# ONE-LINE LAUNCHER for Linux / macOS / WSL:   sh run.sh
#
#   sh run.sh          interactive terminal dashboard (with ML panel)
#   sh run.sh demo     40-second scripted drive
#   sh run.sh web      web dashboard in your browser
#   sh run.sh noml     rule-based monitoring only
#
# Builds with make/cc (or uses dist/svsm-linux-x64 on x86-64 Linux), sets up the
# Python ML packages in a private virtualenv (.venv) once, then starts the program.
cd "$(dirname "$0")" || exit 1

# ---- 1. program -------------------------------------------------------------
BIN=""
if command -v make >/dev/null 2>&1 && command -v cc >/dev/null 2>&1; then
    echo "[1/3] Building the C program..."
    make -s all >/dev/null 2>&1 && BIN="build/svsm"
fi
if [ -z "$BIN" ] && [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] && [ -x dist/svsm-linux-x64 ]; then
    echo "[1/3] No compiler found - using the ready-made dist/svsm-linux-x64"
    BIN="dist/svsm-linux-x64"
fi
if [ -z "$BIN" ]; then
    echo "[!] Cannot start: install build tools (Debian/Ubuntu: sudo apt install build-essential)"
    exit 1
fi

# ---- 2. ML --------------------------------------------------------------------
MLFLAG=""
if [ "$1" = "noml" ]; then
    MLFLAG="--no-ml"
    echo "[2/3] ML skipped."
else
    echo "[2/3] Checking the ML model..."
    PY=""
    if [ -x .venv/bin/python ]; then
        PY=".venv/bin/python"
    elif command -v python3 >/dev/null 2>&1; then
        PY="python3"
    fi
    if [ -z "$PY" ]; then
        echo "    Python 3 not found - continuing with rule-based monitoring only."
        MLFLAG="--no-ml"
    else
        if ! $PY -c "import sklearn, pandas, joblib" >/dev/null 2>&1; then
            echo "    Installing Python packages (one time, needs internet)..."
            if [ "$PY" = "python3" ] && python3 -m venv .venv >/dev/null 2>&1; then
                PY=".venv/bin/python"
            fi
            $PY -m pip install -q -r ml/requirements.txt >/dev/null 2>&1
        fi
        if ! $PY ml/predict.py 50 85 70 40 1 >/dev/null 2>&1; then
            echo "    Training the model for this computer (one time, about 20 s)..."
            $PY ml/train_model.py >/dev/null 2>&1
        fi
        if $PY ml/predict.py 50 85 70 40 1 >/dev/null 2>&1; then
            MLFLAG="--ml-python $PY"
        else
            echo "    ML could not be set up - continuing with rule-based monitoring only."
            MLFLAG="--no-ml"
        fi
    fi
fi

# ---- 3. start -----------------------------------------------------------------
echo "[3/3] Starting..."
case "$1" in
    demo) exec $BIN --demo 40 --seed 7 $MLFLAG ;;
    web)  exec $BIN --serve 8080 --open $MLFLAG ;;
    *)    exec $BIN $MLFLAG ;;
esac
