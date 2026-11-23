#!/bin/sh
# Linux demonstration of the Smart Vehicle Safety Monitoring System with ML-based risk prediction.
#
#   sh scripts/linux_demo.sh                 # or: make linux-demo
#   sh scripts/linux_demo.sh | tee docs/evidence/linux_demo.txt     # keep a transcript
#
# Shows: GCC compilation, Makefile, terminal commands, file handling, shell scripting,
#        execution + debugging (sanitizers), and C <-> Python integration.
# Set PYTHON=/path/to/python3 (with scikit-learn) if the default python3 lacks the packages.

cd "$(dirname "$0")/.." || exit 1
PYTHON=${PYTHON:-${SVSM_PYTHON:-python3}}
export SVSM_PYTHON="$PYTHON"
STEP=0

step() { STEP=$((STEP + 1)); printf '\n==================================================================\n'
         printf ' STEP %d: %s\n' "$STEP" "$1"; printf '==================================================================\n'; }
run()  { printf '$ %s\n' "$*"; "$@"; }
sh_run() { printf '$ %s\n' "$1"; sh -c "$1"; }

step "Environment (terminal commands)"
sh_run "uname -srm"
sh_run "gcc --version | head -1"
sh_run "make --version | head -1"
sh_run "$PYTHON --version"
sh_run "$PYTHON -c 'import sklearn, pandas; print(\"scikit-learn\", sklearn.__version__, \"| pandas\", pandas.__version__)'" \
    || echo "(scikit-learn missing: run 'make ml-setup' - ML steps below will be skipped)"
sh_run "pwd && ls"

step "Project layout (file handling: ls, wc, find)"
sh_run "ls src include ml tests scripts docs"
sh_run "wc -l src/*.c include/*.h | tail -1"
sh_run "find . -name '*.c' -o -name '*.py' -o -name '*.sh' | grep -v build | sort"

step "GCC compilation - by hand, one command"
mkdir -p build
sh_run "gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude -D_POSIX_C_SOURCE=200809L src/*.c -o build/svsm_manual"
sh_run "ls -l build/svsm_manual && file build/svsm_manual 2>/dev/null | cut -c1-110"

step "Makefile build (incremental, with dependency tracking)"
run make clean
run make
sh_run "make --dry-run run | head -3"
run make help

step "Dataset handling with Linux text tools (head, wc, awk, sort, uniq)"
CSV=ml/data/vehicle_risk_dataset.csv
sh_run "head -5 $CSV"
sh_run "wc -l $CSV"
sh_run "tail -n +2 $CSV | cut -d, -f6 | sort | uniq -c | sort -rn"
sh_run "awk -F, 'NR>1 {s[\$6]+=\$1; n[\$6]++} END {for (k in s) printf \"%-9s mean speed %.1f km/h (%d rows)\\n\", k, s[k]/n[k], n[k]}' $CSV"

step "Train + evaluate the ML model (Python / scikit-learn)"
if $PYTHON -c 'import sklearn, pandas, joblib, matplotlib' 2>/dev/null; then
    run $PYTHON ml/train_model.py | head -30
    sh_run "ls -l ml/model ml/reports"
else
    echo "SKIPPED: scikit-learn not installed"
fi

step "C <-> Python integration: one prediction from the shell (shell loop over scenarios)"
printf '%s\n' \
  "safe-driving     50  85 70 40 1" \
  "parked            0  70 60 30 1" \
  "fast-no-belt    150  85 60 40 0" \
  "low-fuel         50  85  3 40 1" \
  "overheat+close   70 115 60  2 1" \
  "bad-input       400  85 70 40 1" > build/scenarios.txt
sh_run "cat build/scenarios.txt"
while read -r name s t f d b; do
    printf '\n[%s]  speed=%s temp=%s fuel=%s dist=%s belt=%s\n$ build/svsm --predict %s %s %s %s %s\n' "$name" "$s" "$t" "$f" "$d" "$b" "$s" "$t" "$f" "$d" "$b"
    ./build/svsm --predict "$s" "$t" "$f" "$d" "$b"
    echo "exit code: $?"
done < build/scenarios.txt

step "Execution: scripted 40 s drive - rule-based alerts AND ML prediction on the dashboard"
rm -f build/demo_alerts.log
run ./build/svsm --demo 40 --seed 7 --no-color --log build/demo_alerts.log > build/demo_output.txt 2>&1
sh_run "grep -c 'ML DRIVER RISK' build/demo_output.txt"
sh_run "sed -n '/Demo finished/!p' build/demo_output.txt | tail -46"

step "File handling: the alert-history log (grep, awk, sort, uniq, tail)"
sh_run "head -3 build/demo_alerts.log"
sh_run "wc -l build/demo_alerts.log"
sh_run "grep -c CRITICAL build/demo_alerts.log; grep -c WARNING build/demo_alerts.log"
sh_run "grep -o 'OVERSPEED\\|LOW_FUEL\\|OVERHEAT\\|OBSTACLE\\|SEATBELT' build/demo_alerts.log | sort | uniq -c | sort -rn"
run ./build/svsm --history 5 --log build/demo_alerts.log

step "ML risk over the drive (grep / sort / uniq on the demo output)"
sh_run "grep -o '\[ \(LOW\|MODERATE\|HIGH\) \]' build/demo_output.txt | sort | uniq -c"

step "Graceful degradation: ML off or Python missing - rule-based monitoring keeps working"
sh_run "./build/svsm --demo 12 --seed 7 --no-color --no-ml --log build/x.log 2>&1 | grep -E 'UNAVAILABLE|Overspeed' | sort -u | head -3"
sh_run "SVSM_PYTHON=no_such_python ./build/svsm --demo 12 --seed 7 --no-color --log build/x.log 2>&1 | grep -E 'ML disabled|OVERSPEED' | sort -u | head -3"

step "Process view: the C server starts the Python predictor as a child process (ps, curl, kill)"
./build/svsm --serve 18090 --web web --log build/serve.log > build/serve.out 2>&1 &
SRV=$!
i=0; while ! curl -s -o /dev/null http://127.0.0.1:18090/api/state && [ $i -lt 300 ]; do i=$((i + 1)); sleep 0.1; done
sh_run "ps -o pid,ppid,etime,cmd -p $SRV; ps -o pid,ppid,cmd --ppid $SRV"
sh_run "curl -s 'http://127.0.0.1:18090/api/cmd?a=set&manual=1&speed=150&fuel=3&temp=113&obst=2&belt=0' | grep -o '\"count\":[0-9]*\\|\"ml\":{[^}]*}'"
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
echo "server stopped; leftover predictor processes: $(pgrep -f 'ml/predict.py --serve' | wc -l)"

step "Testing: unit tests (C), Python tests, C<->Python integration, HTTP API"
run make test 2>&1 | tail -4
if $PYTHON -c 'import sklearn' 2>/dev/null; then
    run $PYTHON -m unittest discover -s tests -p "test_ml*.py" 2>&1 | tail -4
    run sh tests/test_ml_integration.sh build/svsm | tail -3
    run sh tests/test_api.sh build/svsm | tail -3
fi

step "Debugging: rebuild with AddressSanitizer + UBSan and re-run the tests"
run make debug 2>&1 | grep -E "checks|Build complete|ERROR|runtime error"
command -v valgrind >/dev/null && sh_run "valgrind -q --leak-check=full ./build/svsm --predict 50 85 70 40 1" || echo "(valgrind not installed - sanitizers used instead)"
run make clean >/dev/null; run make >/dev/null

printf '\nDemo complete.\n'
