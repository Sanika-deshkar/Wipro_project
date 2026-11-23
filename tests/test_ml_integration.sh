#!/bin/sh
# C <-> Python integration tests (command line + HTTP API).
# Usage: sh tests/test_ml_integration.sh build/svsm      (also: make test-ml)
# Needs the ML dependencies (make ml-setup) and a trained model (make ml-train).
# Use SVSM_PYTHON=/path/to/python to pick the interpreter.
BIN=${1:-build/svsm}
PASS=0; FAIL=0
mkdir -p build
LOG=build/ml_test_alerts.log

check() {   # check "name" "haystack" "needle"
    if printf '%s' "$2" | grep -q -- "$3"; then
        PASS=$((PASS + 1)); echo "  [PASS] $1"
    else
        FAIL=$((FAIL + 1)); echo "  [FAIL] $1   (wanted: $3)"; echo "         got: $(printf '%s' "$2" | head -c 300)"
    fi
}
check_not() {
    if printf '%s' "$2" | grep -q -- "$3"; then
        FAIL=$((FAIL + 1)); echo "  [FAIL] $1   (must not contain: $3)"
    else
        PASS=$((PASS + 1)); echo "  [PASS] $1"
    fi
}
rc_is() {   # rc_is "name" expected actual
    if [ "$2" -eq "$3" ]; then PASS=$((PASS + 1)); echo "  [PASS] $1 (exit $3)"
    else FAIL=$((FAIL + 1)); echo "  [FAIL] $1 (wanted exit $2, got $3)"; fi
}

echo "=== ML integration tests ==="
echo "--- --predict: different input scenarios"
R=$("$BIN" --predict 50 85 70 40 1 2>&1);    check "safe driving -> Low"                 "$R" "ML risk: Low"
R=$("$BIN" --predict 0 70 60 30 1 2>&1);     check "parked -> Low"                       "$R" "ML risk: Low"
R=$("$BIN" --predict 95 85 60 30 1 2>&1);    check "moderate speed -> Moderate"          "$R" "ML risk: Moderate"
R=$("$BIN" --predict 50 85 3 40 1 2>&1);     check "critical fuel only -> Moderate"      "$R" "ML risk: Moderate"
R=$("$BIN" --predict 150 85 60 40 0 2>&1);   check "overspeed + no belt -> High"         "$R" "ML risk: High"
R=$("$BIN" --predict 150 113 3 2 0 2>&1);    check "all five sensors bad -> High"        "$R" "ML risk: High"
R=$("$BIN" --predict 70 115 60 2 1 2>&1);    check "overheat + obstacle 2 m -> High"     "$R" "ML risk: High"
R=$("$BIN" --predict 150 113 3 2 0 2>&1);    check "confidence is printed"               "$R" "confidence [0-9.]*%"

echo "--- invalid inputs and error handling"
R=$("$BIN" --predict 400 85 70 40 1 2>&1);   rc_is "speed 400 rejected" 2 $?;  check "message explains range" "$R" "out of range"
R=$("$BIN" --predict -5 85 70 40 1 2>&1);    rc_is "negative speed rejected" 2 $?
R=$("$BIN" --predict 50 85 150 40 1 2>&1);   rc_is "fuel 150 rejected" 2 $?
R=$("$BIN" --predict 50 85 70 999 1 2>&1);   rc_is "obstacle 999 rejected" 2 $?
R=$("$BIN" --predict 50 85 70 40 2 2>&1);    rc_is "seatbelt 2 rejected" 2 $?
R=$("$BIN" --predict abc 85 70 40 1 2>&1);  rc_is "non-numeric rejected" 2 $?;  check "message names the bad value" "$R" "Invalid number 'abc'"
R=$("$BIN" --predict nan 85 70 40 1 2>&1);   rc_is "NaN rejected" 2 $?
R=$("$BIN" --predict 50 85 70 2>&1);         rc_is "too few arguments rejected" 2 $?
R=$(SVSM_PYTHON=no_such_python "$BIN" --predict 50 85 70 40 1 2>&1); rc_is "missing Python reported" 3 $?
check "missing Python message" "$R" "ML unavailable"
R=$(SVSM_ML_SCRIPT=no/such/predict.py "$BIN" --predict 50 85 70 40 1 2>&1); rc_is "missing script reported" 3 $?
check "missing script message" "$R" "script not found"

echo "--- dashboard: rule-based alerts and ML shown separately"
D=$("$BIN" --demo 40 --seed 7 --no-color --log "$LOG" 2>/dev/null)
check "ML panel is drawn"                    "$D" "ML DRIVER RISK"
check "ML predicts Low at some point"        "$D" "\[ LOW \]"
check "ML predicts High at some point"       "$D" "\[ HIGH \]"
check "rule alert OVERSPEED still shown"     "$D" "OVERSPEED"
check "rule alert SEATBELT still shown"      "$D" "SEATBELT"
check "rule status banner still shown"       "$D" "CRITICAL .*SAFETY STATUS"
check "alert history still written"          "$(cat $LOG)" "OVERSPEED"

echo "--- graceful degradation (ML off / broken)"
D=$("$BIN" --demo 40 --seed 7 --no-color --no-ml --log "$LOG" 2>&1)
check "--no-ml: panel says unavailable"      "$D" "UNAVAILABLE"
check_not "--no-ml: no model loading"        "$D" "Loading ML"
check "--no-ml: rules still alert"           "$D" "OVERSPEED"
D=$(SVSM_PYTHON=no_such_python "$BIN" --demo 40 --seed 7 --no-color --log "$LOG" 2>&1); rc_is "demo survives missing Python" 0 $?
check "reason shown"                         "$D" "ML disabled"
check "rules still alert without ML"         "$D" "OVERSPEED"

echo "--- HTTP API includes the ML prediction"
PORT=18081
"$BIN" --serve $PORT --web web --log "$LOG" >build/ml_server.out 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; wait $PID 2>/dev/null' EXIT INT TERM
i=0
while ! curl -s -o /dev/null "http://127.0.0.1:$PORT/api/state"; do
    i=$((i + 1)); [ $i -gt 300 ] && { echo "server did not start"; cat build/ml_server.out; exit 1; }
    sleep 0.1
done
URL="http://127.0.0.1:$PORT/api/cmd?a=set&manual=1"
R=$(curl -s "$URL&speed=50&fuel=70&temp=85&obst=40&belt=1")
check "safe readings: ML Low"                "$R" '"ml":{"on":true,"label":"Low"'
check "safe readings: no rule alerts"        "$R" '"count":0'
R=$(curl -s "$URL&speed=150&fuel=3&temp=113&obst=2&belt=0")
check "dangerous readings: ML High"          "$R" '"label":"High"'
check "dangerous readings: 5 rule alerts"    "$R" '"count":5'
R=$(curl -s "$URL&speed=150&fuel=70&temp=85&obst=40&belt=1")
check "one rule alert while ML tracks risk"  "$R" '"count":1'
R=$(curl -s "$URL&speed=999&fuel=70&temp=85&obst=40&belt=1")
check "API survives out-of-range input"      "$R" '"ml":'

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
