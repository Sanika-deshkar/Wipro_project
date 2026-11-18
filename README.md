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

* Linux (tested on Ubuntu, GCC 15 / GNU Make 4.4)
* `gcc` and `make` — on Debian/Ubuntu: `sudo apt install build-essential`
* A terminal with ANSI colour support, at least 74 × 32 characters

## Quick start (one command)

| Where | Command | What happens |
|---|---|---|
| Windows | double-click **`start.bat`** | builds in WSL, starts the C server, opens the browser |
| WSL / Linux | `make start` | builds, starts the C server, opens the browser |

Press **Ctrl+C** (or close the window) to stop. `NO_BROWSER=1 make start` skips opening the browser;
`PORT=9000 sh start.sh` uses another port.

### From VS Code

Open the `smart-vehicle-system` folder in VS Code (**File > Open Folder**), then press **Ctrl+Shift+B**
(or **Terminal > Run Task... > Start dashboard**). The task builds, starts the C server and opens the browser.
Other tasks: *Run terminal version*, *Build*, *Run unit tests*, *Run API tests*, *Clean*.
You can also type `make start` in VS Code's integrated terminal (use a WSL terminal on Windows).
To view the dashboard inside VS Code: **Ctrl+Shift+P > Simple Browser: Show > http://localhost:8080**.

## Build and run

```bash
make            # compile  -> build/svsm
make run        # compile and start the interactive dashboard
make demo       # 40-second scripted scenario (no keyboard needed)
make start      # ONE command: build + C backend + open the browser
make serve      # C backend + web dashboard at http://localhost:8080
make test       # 27 unit tests
make test-api   # start the server and test the HTTP API with curl (24 checks)
make debug      # clean rebuild + tests with AddressSanitizer / UBSan
make history    # print the last 20 alert-history records
make clean      # remove build/        (distclean also removes logs/)
make help       # list targets
```

## Keyboard controls (interactive mode)

| Key | Action | Key | Action |
|---|---|---|---|
| `S` | Start / Stop simulation | `B` | Toggle seatbelt |
| `R` | Reset to defaults | `F` | Refuel to 100 % |
| `W` / `X` | Target speed +10 / −10 km/h | `M` | Manual sensor input |
| `H` | View alert history | `C` | Clear alert history |
| `Q` | Quit | `Ctrl+C` | Quit (terminal is restored safely) |

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
├── start.bat / start.sh     one-command launcher (Windows / Linux)
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
│   └── terminal.h           raw keyboard input API
├── src/
│   ├── main.c               CLI parsing, interactive loop, demo mode
│   ├── vehicle.c            reset / clamp
│   ├── monitor.c            five sensor checkers + overall status
│   ├── simulation.c         speed, fuel, temperature, obstacle model
│   ├── dashboard.c          ANSI panel renderer
│   ├── logger.c             file-based alert history
│   ├── server.c             HTTP server + JSON API for the web dashboard
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
