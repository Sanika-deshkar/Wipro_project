#!/bin/sh
# One-command launcher: build -> start the C backend -> open the dashboard.
#   sh start.sh            (or: make start)
#   PORT=9000 sh start.sh  use another port
#   NO_BROWSER=1 sh start.sh   start the server without opening a browser
cd "$(dirname "$0")" || exit 1

BIN=${1:-build/svsm}
PORT=${PORT:-8080}
URL="http://localhost:$PORT"

# 1. build if needed
[ -x "$BIN" ] || make || exit 1

# 2. start the C server in the background
"$BIN" --serve "$PORT" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null; exit 0' INT TERM

# 3. wait until it answers (max ~5 s)
i=0
while ! curl -s -o /dev/null "$URL/api/state" 2>/dev/null; do
    kill -0 $SERVER 2>/dev/null || { echo "Server failed to start (is port $PORT already in use?)"; exit 1; }
    i=$((i + 1)); [ $i -gt 50 ] && break
    sleep 0.1
done

# 4. open the browser (WSL -> Windows browser, Linux -> xdg-open, macOS -> open)
if [ -z "$NO_BROWSER" ]; then
    if grep -qi microsoft /proc/version 2>/dev/null && command -v cmd.exe >/dev/null 2>&1; then
        cmd.exe /c start "" "$URL" >/dev/null 2>&1
    elif command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$URL" >/dev/null 2>&1 &
    elif command -v open >/dev/null 2>&1; then
        open "$URL"
    else
        echo "Open $URL in your browser."
    fi
fi

echo "Dashboard: $URL     (press Ctrl+C to stop)"
wait $SERVER
