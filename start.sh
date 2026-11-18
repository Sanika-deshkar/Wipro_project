#!/bin/sh
# One-command launcher for Linux / macOS / WSL.
#   sh start.sh                 build (if a compiler exists), start the C backend, open the browser
#   PORT=9000 sh start.sh       another port (a busy port is skipped automatically)
#   NO_BROWSER=1 sh start.sh    do not open the browser
cd "$(dirname "$0")" || exit 1

PORT=${PORT:-8080}
OPEN="--open"
[ -n "$NO_BROWSER" ] && OPEN=""

BIN=""

# 1. build from source when a compiler and make are available
if command -v make >/dev/null 2>&1 && command -v cc >/dev/null 2>&1; then
    make -s all >/dev/null 2>&1 && BIN="build/svsm"
fi

# 2. otherwise use the ready-made Linux program from dist/
if [ -z "$BIN" ] && [ "$(uname -s)" = "Linux" ]; then
    case "$(uname -m)" in
        x86_64|amd64) [ -x dist/svsm-linux-x64 ] && BIN="dist/svsm-linux-x64" ;;
    esac
fi

if [ -z "$BIN" ]; then
    cat <<'EOF'
Could not start: no C compiler found and no ready-made program for this computer.

Install the build tools once, then run this script again:
  Debian / Ubuntu / WSL :  sudo apt update && sudo apt install -y build-essential
  Fedora                :  sudo dnf install -y gcc make
  macOS                 :  xcode-select --install
  Windows (no Linux)    :  double-click start.bat instead
EOF
    exit 1
fi

exec "$BIN" --serve "$PORT" $OPEN
