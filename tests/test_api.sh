#!/bin/sh
# Integration test for the C backend (HTTP API).
# Usage: sh tests/test_api.sh build/svsm      (also run by `make test-api`)
BIN=${1:-build/svsm}
PORT=18080
LOG=build/api_test.log
URL="http://127.0.0.1:$PORT"
PASS=0; FAIL=0

mkdir -p build
rm -f "$LOG"
"$BIN" --serve $PORT --web web --log "$LOG" >build/api_server.out 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; wait $PID 2>/dev/null' EXIT INT TERM

# wait for the server to come up (max ~5 s)
i=0
while ! curl -s -o /dev/null "$URL/api/state"; do
    i=$((i + 1)); [ $i -gt 50 ] && { echo "server did not start"; cat build/api_server.out; exit 1; }
    sleep 0.1
done

check() {   # check "name" "haystack" "needle"
    if printf '%s' "$2" | grep -q -- "$3"; then
        PASS=$((PASS + 1)); echo "  [PASS] $1"
    else
        FAIL=$((FAIL + 1)); echo "  [FAIL] $1   (wanted: $3)"; echo "         got: $(printf '%s' "$2" | head -c 200)"
    fi
}

echo "=== C backend API tests ==="

R=$(curl -s "$URL/api/state")
check "initial state is stopped"            "$R" '"running":false'
check "initial status is SAFE (overall 0)"  "$R" '"overall":0'

R=$(curl -s "$URL/api/cmd?a=set&manual=1&speed=150&fuel=10&temp=112&obst=2&belt=0")
check "manual mode on"                      "$R" '"manual":true'
check "five alerts active at once"          "$R" '"count":5'
check "overall status CRITICAL"             "$R" '"overall":2'
check "overspeed alert text from C engine"  "$R" 'OVERSPEED 150 km/h'

R=$(curl -s "$URL/api/history?n=50")
check "alerts written to history"           "$R" '"type":"OVERSPEED"'
check "history has seatbelt record"         "$R" '"type":"SEATBELT"'
R=$(curl -s "$URL/api/export.log")
check "export.log is the real log file"     "$R" 'COLLISION RISK'
grep -q 'OVERHEAT' "$LOG" && { PASS=$((PASS + 1)); echo "  [PASS] log file on disk has the alerts"; } || { FAIL=$((FAIL + 1)); echo "  [FAIL] log file on disk"; }
R=$(curl -s "$URL/api/export.csv")
check "export.csv has header"               "$R" 'time,severity,type,message'

R=$(curl -s "$URL/api/cmd?a=reset")
check "reset restores full tank"            "$R" '"fuel":100.00'
check "reset leaves manual mode"            "$R" '"manual":false'

R=$(curl -s "$URL/api/cmd?a=belt&v=1")
check "seatbelt command"                    "$R" '"belt":true'
curl -s "$URL/api/cmd?a=rate&v=4" >/dev/null
curl -s "$URL/api/cmd?a=target&v=60" >/dev/null
R=$(curl -s "$URL/api/cmd?a=start&belt=0")
check "start -> running"                    "$R" '"running":true'
check "start carries the seatbelt answer"   "$R" '"belt":false'
sleep 2
R=$(curl -s "$URL/api/state")
TICKS=$(printf '%s' "$R" | sed -n 's/.*"ticks":\([0-9]*\).*/\1/p')
[ "${TICKS:-0}" -ge 4 ] && { PASS=$((PASS + 1)); echo "  [PASS] C simulation ticks by itself ($TICKS ticks in 2 s at 4x)"; } || { FAIL=$((FAIL + 1)); echo "  [FAIL] simulation did not tick ($TICKS)"; }
R=$(curl -s "$URL/api/cmd?a=stop")
check "stop -> not running"                 "$R" '"running":false'

R=$(curl -s "$URL/api/cmd?a=demo&belt=1")
check "demo starts"                         "$R" '"demo":true'
check "demo carries the seatbelt answer"    "$R" '"belt":true'
sleep 1
R=$(curl -s "$URL/api/state")
check "demo keeps running"                  "$R" '"running":true'

R=$(curl -s "$URL/")
check "index.html served by C"              "$R" 'Smart Vehicle'
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$URL/app.js")
check "app.js served (HTTP 200)"            "$CODE" '200'
CODE=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' "$URL/../Makefile")
check "path traversal blocked (404)"        "$CODE" '404'
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$URL/api/nope")
check "unknown endpoint is 404"             "$CODE" '404'

curl -s "$URL/api/cmd?a=clear" >/dev/null
R=$(curl -s "$URL/api/history")
check "clear empties the history"           "$R" '^\[\]$'

echo
echo "$((PASS + FAIL)) checks, $FAIL failed"
[ $FAIL -eq 0 ]
