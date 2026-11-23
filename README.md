# Smart Vehicle Safety Monitoring and Dashboard System

A terminal-based vehicle safety monitor and digital dashboard written in **C (C11)**
for **Linux**. It combines two classic projects — *Car Safety Monitoring System* and
*Car Dashboard Display Simulation* — into one application: a simulated vehicle feeds a
rule engine that raises Safe / Warning / Critical alerts, a live ANSI dashboard shows
every parameter, and every alert is written to a history file.

```
+-----------------------------------------------------------+
|          SMART VEHICLE SAFETY & DASHBOARD SYSTEM          |
+-----------------------------------------------------------+
| SIM: RUNNING   TIME: 00:00:04  ODO: 0.06 km               |
+-----------------------------------------------------------+
| Speed        150 km/h    [###################.] CRITICAL  |
| Fuel         48.4 %      [##########..........] OK        |
| Engine Temp  113 C       [################....] CRITICAL  |
| Obstacle     155.9 m     [################....] OK        |
| Seatbelt     UNFASTENED                         CRITICAL  |
+-----------------------------------------------------------+
| SAFETY STATUS : [ CRITICAL ]   active alerts: 3           |
+-----------------------------------------------------------+
| ! [CRITICAL] OVERSPEED 150 km/h (limit 80)                |
| ! [CRITICAL] ENGINE OVERHEAT 113 C (max 110)              |
| ! [CRITICAL] SEATBELT OFF at 150 km/h (>40)               |
+-----------------------------------------------------------+
```

## Features

| Module | What it does |
|---|---|
| Speed monitoring | Speed input, overspeed detection (Warning > 80, Critical > 120 km/h) |
| Fuel monitoring | Fuel %, low-fuel warning (< 15 %) and critical (< 5 %) |
| Temperature monitoring | Engine overheating (Warning > 100 °C, Critical > 110 °C) |
| Obstacle monitoring | Distance input, proximity alerts (< 10 m warning, < 3 m critical) |
| Seatbelt monitoring | Fastened / unfastened; alert only while moving |
| Digital dashboard | Colour ANSI panel with bars, odometer and clock |
| Safety status | Overall **SAFE / WARNING / CRITICAL** = worst active alert |
| Alert system | All five alerts can be active simultaneously |
| Simulation controls | Start, Stop, Reset, accelerate, brake, refuel, manual input |
| Alert history | Appended to `logs/alert_history.log`; viewable in-app or via `--history` |

All thresholds live in [`include/config.h`](include/config.h).

## Requirements

* Linux, macOS or Windows (tested: Ubuntu / GCC 15 and native Windows 11; macOS code paths are untested)
* `gcc` and `make` — on Debian/Ubuntu: `sudo apt install build-essential`
* A terminal with ANSI colour support, at least 64 columns × 46 rows (60 rows for the roomy layout; the dashboard switches to a compact layout automatically); the optional ML risk prediction needs Python 3 with scikit-learn (`make ml-setup`)

## Quick start - works on any laptop

| Your computer | Do this | Needs installing? |
|---|---|---|
| **Windows 10 / 11** | double-click **`start.bat`** | **Nothing** - uses the ready-made `dist\svsm.exe` |
| **Linux** (Ubuntu, Debian, Fedora ...) | `sh start.sh` (or `make start`) | Nothing for x86-64 (uses `dist/svsm-linux-x64`); otherwise `build-essential` |
| **macOS** | `sh start.sh` | Xcode command-line tools (`xcode-select --install`) |
| **Windows with WSL** | `start.bat` also works, or `wsl -e make start` | Nothing |

Every launcher starts the C server **and opens your browser by itself**. Close the window or press **Ctrl+C** to stop.
If port 8080 is busy the server automatically uses the next free port (8081, 8082 ...) and opens that one.
`NO_BROWSER=1` skips opening the browser; `PORT=9000 sh start.sh` picks another start port.

On Windows, SmartScreen may warn about an unsigned program: click **More info -> Run anyway**.

The same C source builds natively on all three systems (a small `platform.c` layer wraps Windows sockets / console
and POSIX). The ready-made programs in [`dist/`](dist/) are rebuilt with `make dist`.

### From VS Code

Open the `smart-vehicle-system` folder in VS Code (**File > Open Folder**), then press **Ctrl+Shift+B**
(or **Terminal > Run Task... > Start dashboard**). The task builds, starts the C server and opens the browser.
Other tasks: *Run terminal version*, *Build*, *Run unit tests*, *Run API tests*, *Clean*.
You can also type `make start` in VS Code's integrated terminal (use a WSL terminal on Windows).
To view the dashboard inside VS Code: **Ctrl+Shift+P > Simple Browser: Show > http://localhost:8080**.

## One-line start (any computer)

| System | Command | What it does |
|---|---|---|
| **Windows (cmd / PowerShell)** | `run.bat` | builds with MinGW if present, else uses the ready-made `dist\svsm.exe`; sets up Python + the ML model if Python exists |
| **Linux / macOS / WSL** | `sh run.sh` | builds with make/cc (or uses `dist/svsm-linux-x64`), creates a private `.venv` for the ML packages |

Options for both: `demo` (40-second scripted drive), `web` (browser dashboard), `noml` (rules only).
Without Python the program still runs with rule-based monitoring; the ML panel then shows "UNAVAILABLE".
The screen animates smoothly (about 15 frames per second) while the simulation ticks once per second.

## Build and run

```bash
make            # compile  -> build/svsm
make run        # compile and start the interactive dashboard
make demo       # 40-second scripted scenario (no keyboard needed)
make start      # ONE command: build + C backend + open the browser
make dist       # rebuild dist/svsm.exe (Windows) and dist/svsm-linux-x64
make serve      # C backend + web dashboard at http://localhost:8080
make test       # 46 unit checks (rules, simulator, logger, ML helpers)
make test-api   # start the server and test the HTTP API with curl (26 checks)
make debug      # clean rebuild + tests with AddressSanitizer / UBSan
make history    # print the last 20 alert-history records
make ml-setup   # pip install the Python ML dependencies
make ml-train   # train + evaluate the ML model -> ml/reports/
make ml-test    # 25 Python tests of the predictor
make test-ml    # 41 C <-> Python integration checks
make test-all   # every test suite
make linux-demo # scripted Linux demonstration (gcc, make, shell, logs, ML)
make clean      # remove build/        (distclean also removes logs/)
make help       # list targets
```

## ML risk prediction (Python / scikit-learn)

A Random Forest trained on `ml/data/vehicle_risk_dataset.csv` (10,000 rows) predicts an
overall risk level **Low / Moderate / High** from speed, engine temperature, fuel, obstacle
distance and seatbelt. It is shown on the dashboard in its **own panel, separate from the
rule-based alerts**, and is optional: without Python the rule-based system works as before.

```bash
make ml-setup && make ml-train      # one-time: install packages, train, write ml/reports/
make run                            # dashboard now has the "ML RISK PREDICTION" row
./build/svsm --predict 150 113 3 2 0   # one prediction: speed temp fuel obstacle belt(0/1)
./build/svsm --no-ml                # rules only
```

Test accuracy 99.6 % (macro F1 0.997); see [`ml/reports/metrics.md`](ml/reports/metrics.md) and
section 22 of [`docs/PROJECT_REPORT.md`](docs/PROJECT_REPORT.md). Python tests use `unittest`
(`make ml-test`). Options: `--ml-python CMD`, `--ml-script FILE`, env `SVSM_PYTHON`.

## Keyboard controls (interactive mode)

| Key | Action | Key | Action |
|---|---|---|---|
| `S` | Start / Stop simulation | `B` | Toggle seatbelt |
| `R` | Reset to defaults | `F` | Refuel to 100 % |
| `W` / `X` | Target speed +10 / −10 km/h | `M` | Manual sensor input |
| `H` | View alert history | `C` | Clear alert history |
| `Q` | Quit | `Ctrl+C` | Quit (terminal is restored safely) |
| `↑` / `↓` | Same as `W` / `X` (faster / slower) | `←` / `→` | Toggle seatbelt |
| `1`-`5` | Scenario presets: City, Highway, Overheat, Obstacle, Chaos | `P` | Pause / resume the ML panel |
| `T` / `Y` | Inject engine cooling fault +10 / -10 °C | `O` | Obstacle appears 6 m ahead |

**Quick tour:** press `B` (belt on), `S` (start), then tap `W` repeatedly and watch
Overspeed, rising engine temperature and falling fuel trigger alerts. Press `H` to see
them recorded. Press `M` to type exact values, e.g. speed 150, obstacle 2 m.

## Command line options

```
svsm                  interactive dashboard
svsm --demo [N]       scripted N-second scenario (default 40)
svsm --seed S         fixed random seed (repeatable runs)
svsm --log FILE       alert history file (default logs/alert_history.log)
svsm --history [N]    print last N history records and exit
svsm --serve [PORT]   C backend + web dashboard (default port 8080)
svsm --web DIR        folder with the web files (default web)
svsm --public         listen on all interfaces instead of localhost only
svsm --open           with --serve: open the dashboard in your browser
svsm --no-color       disable ANSI colours
svsm --version | --help
```

## Web dashboard (powered by the C program)

The browser dashboard in [`web/`](web/) is **driven entirely by the C code**. `svsm --serve` starts a small HTTP server
(POSIX sockets, no libraries) that serves the page and exposes the simulator, rule engine and alert log as a JSON API.
The page contains no simulation or safety logic - every reading, alert and history record comes from C.

```
 Browser (web/)  --- GET /api/state, /api/cmd?a=... --->  C program (svsm --serve)
 index.html                                              simulation.c  monitor.c  logger.c
 style.css, app.js  <---------- JSON ---------------      logs/alert_history.log
```

**Run it**

```bash
make serve            # then open http://localhost:8080
```

On Windows with WSL: run `make serve` inside WSL and open <http://localhost:8080> in the Windows browser.
If the server is not running the page shows an **"C backend not running"** banner with a Retry button.

| Endpoint | Purpose |
|---|---|
| `GET /` and static files | the dashboard (`web/`) |
| `GET /api/state` | live readings, trip stats and the safety report |
| `GET /api/cmd?a=start\|stop\|reset\|demo\|accel\|brake\|refuel` | simulation controls |
| `GET /api/cmd?a=start&belt=0\|1`, `a=demo&belt=0\|1` | start / demo together with the driver's seatbelt answer |
| `GET /api/cmd?a=target&v=N`, `a=belt[&v=0\|1]`, `a=rate&v=1\|2\|4`, `a=manual&v=0\|1` | driver inputs |
| `GET /api/cmd?a=set&speed=&fuel=&temp=&obst=&belt=&manual=1` | set sensors by hand |
| `GET /api/cmd?a=clear` | clear the alert history file |
| `GET /api/history?n=200` | alert history, newest first |
| `GET /api/export.log`, `/api/export.csv` | download the real log file / a CSV |

The server listens on `127.0.0.1` only (use `--public` to expose it) and blocks path traversal.

Pressing **Start** or **Demo** first asks "Is your seatbelt fastened?" in a pop-up (Yes / No / Cancel); the answer is
sent to the C program together with the start command.

Layout: the whole dashboard fits one screen with no page scrolling in either direction - on desktop the sensors, a tabbed
Alerts / History / Manual / Guide panel and the controls are all visible together; on phones a bottom bar switches between
Monitor, Controls and Alerts & log.

Interactive touches: live sparkline in every card, click a card or an alert for a detail window (60-second chart,
lowest / highest / average, "try it" slider), pop-up notifications for new alerts, light / dark theme, searchable
history, and keyboard shortcuts (`S R W X B F M D T`). Thresholds come from `include/config.h` (single source of truth).

## Project layout

```
smart-vehicle-system/
├── start.bat / start.sh     one-command launcher (Windows / Linux, macOS)
├── dist/                    ready-made svsm.exe (Windows) + svsm-linux-x64
├── Makefile                 build, run, test, debug, clean
├── README.md                this file
├── include/                 public headers (one per module)
│   ├── config.h             all thresholds and constants
│   ├── vehicle.h            Vehicle state structure
│   ├── monitor.h            rule engine API, Severity / AlertType
│   ├── simulation.h         simulator API
│   ├── dashboard.h          renderer API
│   ├── logger.h             alert history API
│   ├── server.h             HTTP server API
│   ├── platform.h           Windows / Linux / macOS portability layer
│   └── terminal.h           raw keyboard input API
├── src/
│   ├── main.c               CLI parsing, interactive loop, demo mode
│   ├── vehicle.c            reset / clamp
│   ├── monitor.c            five sensor checkers + overall status
│   ├── simulation.c         speed, fuel, temperature, obstacle model
│   ├── dashboard.c          ANSI panel renderer
│   ├── logger.c             file-based alert history
│   ├── server.c             HTTP server + JSON API for the web dashboard
│   ├── platform.c           sockets/console/clock/browser differences per OS
│   └── terminal.c           termios + select() non-blocking input
├── web/                     HTML/CSS/JS dashboard (front end for the C backend)
├── tests/test_monitor.c     unit tests (rules, simulator, logger)
├── tests/test_api.sh        integration test of the HTTP API (curl)
├── docs/PROJECT_REPORT.md   full capstone project report
└── logs/alert_history.log   created at run time
```

## Sample alert history

```
[2026-11-18 13:30:40] [WARNING ] OVERSPEED: Overspeed 82 km/h (limit 80) | spd=82 fuel=23.9 temp=67 obst=149.1 belt=ON
[2026-11-18 13:30:40] [CRITICAL] SEATBELT : SEATBELT OFF at 130 km/h (>40) | spd=130 fuel=8.4 temp=103 obst=96.6 belt=OFF
```

An alert is written when it first appears or when its severity **escalates**, so the
file records events rather than repeating the same line every second.

## Documentation

The full capstone report — requirements, design, flowcharts, data structures, test
cases, results, limitations and future scope — is in
[`docs/PROJECT_REPORT.md`](docs/PROJECT_REPORT.md).

## Licence

Academic capstone project. Free to use and modify for learning purposes.
