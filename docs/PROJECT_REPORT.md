# Smart Vehicle Safety Monitoring and Dashboard System Using C on Linux

**Capstone Project Report**

| | |
|---|---|
| Student name | _[your name]_ |
| Roll / enrolment no. | _[your number]_ |
| Programme / batch | _[programme, batch]_ |
| Guide / mentor | _[mentor name]_ |
| Institution | _[institution]_ |
| Date of submission | _[date]_ |

---

## Table of contents

1. Abstract
2. Introduction
3. Problem statement and objectives
4. Scope and constraints
5. Requirements
6. System design
7. Module description
8. Data structures
9. Algorithms and safety rules
10. Simulation model
11. Alert history (file handling)
12. Build system (Makefile)
13. Implementation notes
14. Testing
15. Results
16. User manual
17. Limitations
18. Future scope
19. Conclusion
20. References
21. Appendix: source file inventory

---

## 1. Abstract

Road-vehicle safety depends on the driver being told about dangerous conditions
quickly and clearly. This project implements a **Smart Vehicle Safety Monitoring and
Dashboard System** in the C language for Linux. A simulated vehicle produces five
sensor readings — speed, fuel level, engine temperature, obstacle distance and
seatbelt status. A rule engine evaluates them against configurable thresholds and
classifies the overall situation as **Safe, Warning or Critical**. A colour text-mode
dashboard displays all parameters in real time, several alerts can be active at the same
time, the user can start, stop and reset the simulation, and every alert is stored in a
history file. The program is built with GCC and a Makefile, uses only standard C and
POSIX libraries, and is covered by 27 automated unit tests that also pass under
AddressSanitizer and UndefinedBehaviorSanitizer.

## 2. Introduction

Modern cars contain many sensors and an instrument cluster that gathers their values.
Two related ideas are combined here:

1. **Car Safety Monitoring System** – detects unsafe conditions (overspeed, low fuel,
   overheating, close obstacles, unfastened seatbelt) and raises alerts.
2. **Car Dashboard Display Simulation** – presents the vehicle's parameters on a
   digital instrument panel.

Merging them produces a more realistic system: the dashboard is not only a display,
it also reflects the safety state computed by the monitoring logic.

## 3. Problem statement and objectives

**Problem statement.** Design and implement, in C on Linux, a program that simulates
a vehicle, continuously monitors its safety-critical parameters, displays them on a
digital dashboard, warns the driver of multiple simultaneous hazards, and keeps a
permanent record of those warnings.

**Objectives**

* Implement monitoring for speed, fuel, temperature, obstacle distance and seatbelt.
* Classify the system as Safe, Warning or Critical.
* Support multiple simultaneous alerts.
* Provide a live, readable digital dashboard in the terminal.
* Provide Start, Stop and Reset simulation controls.
* Store alert history in a file and allow it to be viewed.
* Use a modular design, GCC and a Makefile; keep the code warning-free and tested.

## 4. Scope and constraints

| In scope | Out of scope |
|---|---|
| Single simulated vehicle and single driver | Real CAN-bus / hardware sensors |
| Terminal (ANSI) user interface | Graphical (GUI) interface |
| Linux / POSIX systems | Windows-native build |
| Text-file alert history | Database storage |

## 5. Requirements

### 5.1 Functional requirements

| ID | Requirement |
|---|---|
| FR-1 | The system shall accept/simulate vehicle speed and flag overspeed. |
| FR-2 | The system shall track fuel percentage and flag low fuel. |
| FR-3 | The system shall track engine temperature and flag overheating. |
| FR-4 | The system shall accept/simulate obstacle distance and flag proximity. |
| FR-5 | The system shall track seatbelt status and flag an unfastened belt while moving. |
| FR-6 | The system shall display all parameters on a digital dashboard. |
| FR-7 | The system shall report an overall status of Safe, Warning or Critical. |
| FR-8 | The system shall show all active alerts simultaneously. |
| FR-9 | The user shall be able to start, stop and reset the simulation. |
| FR-10 | Every new or escalated alert shall be appended to a history file. |
| FR-11 | The user shall be able to view and clear the alert history. |
| FR-12 | The user shall be able to enter sensor values manually. |

### 5.2 Non-functional requirements

| ID | Requirement |
|---|---|
| NFR-1 | Written in C11, compiled with GCC with `-Wall -Wextra -Wpedantic` and zero warnings. |
| NFR-2 | Only standard C and POSIX libraries (no third-party dependencies). |
| NFR-3 | The terminal must always be restored, including on Ctrl+C. |
| NFR-4 | Thresholds must be changeable in one place. |
| NFR-5 | Memory-safe: clean under AddressSanitizer / UBSan. |

### 5.3 Software and hardware environment

| Item | Detail |
|---|---|
| Operating system | Linux (developed and tested on Ubuntu) |
| Compiler | GCC 15.2 (C11) |
| Build tool | GNU Make 4.4 |
| Libraries | libc, POSIX `termios`, `select`, `sys/stat` |
| Terminal | ANSI-capable, ≥ 74 × 32 characters |

## 6. System design

### 6.1 Architecture

The program is split into layers so that each module has one responsibility and the
rule engine has no knowledge of the screen or the keyboard (this is what makes it
easy to unit-test).

```
                       +---------------------------+
                       |          main.c           |
                       |  CLI, event loop, demo    |
                       +-------------+-------------+
          keys                       |                          output
   +----------------+                |               +--------------------+
   |  terminal.c    |<---------------+-------------->|    dashboard.c     |
   | raw input      |                |               |   ANSI rendering   |
   +----------------+                |               +---------^----------+
                                     v                         |
   +----------------+      +-----------------+      +-----------+--------+
   | simulation.c   |----->|    vehicle.h    |----->|     monitor.c      |
   | physics model  |      |  Vehicle state  |      |  rules -> alerts   |
   +----------------+      +-----------------+      +-----------+--------+
                                                                |
                                                    +-----------v--------+
                                                    |     logger.c       |
                                                    | alert_history.log  |
                                                    +--------------------+
```

### 6.2 Main-loop flowchart

```mermaid
flowchart TD
    A([Start]) --> B[Parse options]
    B --> C{Mode?}
    C -- demo --> D[Run scripted scenario]
    C -- history --> E[Print history, exit]
    C -- interactive --> F[Enable raw terminal]
    F --> G[Evaluate sensors -> SafetyReport]
    G --> H[Log new / escalated alerts]
    H --> I[Render dashboard]
    I --> J[Wait for key or next tick]
    J --> K{Simulation running and tick due?}
    K -- yes --> L[sim_step: update vehicle]
    K -- no --> M
    L --> M{Key pressed?}
    M -- yes --> N[Handle command S R W X B F M H C Q]
    M -- no --> G
    N --> O{Quit?}
    O -- no --> G
    O -- yes --> P[Restore terminal] --> Q([End])
```

### 6.3 Safety-evaluation flowchart

```mermaid
flowchart TD
    S([Vehicle snapshot]) --> A[check_speed]
    A --> B[check_fuel]
    B --> C[check_temperature]
    C --> D[check_obstacle]
    D --> E[check_seatbelt]
    E --> F[Count active alerts]
    F --> G[overall = worst severity]
    G --> H([SafetyReport])
```

All five checkers always run, which is why several alerts can be active together.

### 6.4 Alert-logging state logic

To avoid writing the same line every second, the logger compares the *previous* and
*current* report for each alert type:

| Previous | Current | Logged? |
|---|---|---|
| inactive | active | yes — new alert |
| Warning | Critical | yes — escalation |
| active | same level | no |
| Critical | Warning | no — de-escalation |
| active | inactive | no — cleared |

## 7. Module description

| Module | Files | Responsibility |
|---|---|---|
| Configuration | `config.h` | Thresholds, limits, file paths, simulation constants |
| Vehicle model | `vehicle.h/.c` | `Vehicle` structure; reset and clamping to valid ranges |
| Monitor | `monitor.h/.c` | Five sensor checkers; produces `SafetyReport` |
| Simulation | `simulation.h/.c` | Physics-lite model; driver inputs (accelerate, brake, belt, refuel) |
| Dashboard | `dashboard.h/.c` | Fixed-layout ANSI panel with bars, colours and alert list |
| Logger | `logger.h/.c` | Append, view-last-N and clear of the history file |
| Terminal | `terminal.h/.c` | Raw mode, non-blocking `select()` input, safe restore |
| Server | `server.h/.c` | Single-threaded HTTP server (POSIX sockets): static files + JSON API for the web dashboard |
| Main | `main.c` | Options, interactive loop, manual input, demo scenario, `--serve` entry |
| Web front end | `web/index.html, style.css, app.js` | Browser display and controls; no logic of its own, talks to the C server |

## 8. Data structures

```c
typedef struct {
    float speed_kmh, fuel_pct, engine_temp_c, obstacle_m;
    bool  seatbelt_fastened;
    float target_speed_kmh, odometer_km;
    unsigned long ticks;
    SimState state;                  /* SIM_STOPPED | SIM_RUNNING */
} Vehicle;

typedef enum { LEVEL_SAFE, LEVEL_WARNING, LEVEL_CRITICAL } Severity;
typedef enum { ALERT_OVERSPEED, ALERT_LOW_FUEL, ALERT_OVERHEAT,
               ALERT_OBSTACLE, ALERT_SEATBELT, ALERT_COUNT } AlertType;

typedef struct { bool active; Severity level; char message[96]; } Alert;

typedef struct {
    Alert    alerts[ALERT_COUNT];    /* indexed by AlertType */
    int      active_count;
    Severity overall;
} SafetyReport;
```

Indexing `alerts[]` by the `AlertType` enum gives O(1) access and makes
"multiple simultaneous warnings" natural: each type has its own slot.

## 9. Algorithms and safety rules

### 9.1 Threshold table

| Parameter | Safe | Warning | Critical |
|---|---|---|---|
| Speed | ≤ 80 km/h | > 80 km/h | > 120 km/h |
| Fuel | ≥ 15 % | < 15 % | < 5 % |
| Engine temperature | ≤ 100 °C | > 100 °C | > 110 °C |
| Obstacle distance | ≥ 10 m | < 10 m | < 3 m |
| Seatbelt | fastened, or vehicle stationary | unfastened, speed ≤ 40 km/h | unfastened, speed > 40 km/h |

Boundaries are strict (a speed of exactly 80 km/h is *not* overspeed); this is verified
by unit tests.

### 9.2 Overall status

```
overall = max( severity of every active alert )     /* SAFE if none */
```

### 9.3 Seatbelt rule

A parked vehicle with an open belt is not a hazard, so the seatbelt alert is raised
only when speed > 0. It becomes Critical above 40 km/h.

## 10. Simulation model

One tick represents one second.

| Quantity | Model |
|---|---|
| Speed | Ramps toward the driver's target; max +8 km/h per tick, −12 km/h per tick braking; ±0.5 km/h jitter while cruising |
| Fuel | `−(0.05 + 0.01 × speed)` % per tick; engine stops (speed → 0) at 0 % |
| Engine temperature | Relaxes 10 % per tick toward `60 + 0.45 × speed` °C, plus ±0.4 °C noise |
| Obstacle | Random walk ±6 m, closing at `0.05 × speed` m per tick; respawns 100–200 m ahead when passed |
| Odometer | `+ speed / 3600` km per tick |

Fuel use is **intentionally accelerated** (about 100 s of driving at 100 km/h empties
the tank) so low-fuel behaviour can be demonstrated during a short viva or demo.

## 11. Alert history (file handling)

* File: `logs/alert_history.log` (directory created automatically with `mkdir`).
* Opened in append mode for each record, so data survives a crash.
* Record format:

```
[2026-11-18 13:30:40] [CRITICAL] SEATBELT : SEATBELT OFF at 130 km/h (>40) | spd=130 fuel=8.4 temp=103 obst=96.6 belt=OFF
```

* Viewing the last *N* lines uses a circular buffer of *N* lines, so memory stays
  constant however large the file is.
* C library functions used: `fopen`, `fprintf`, `fgets`, `fclose`, `strftime`,
  `localtime`, `mkdir`.

## 12. Build system (Makefile)

| Target | Action |
|---|---|
| `make` | Compile every `src/*.c` to `build/*.o` and link `build/svsm` |
| `make run` | Build, then launch the interactive dashboard |
| `make demo` | Build, then run a seeded 40-second scenario |
| `make serve` | Build, then start the C backend and web dashboard on `http://localhost:8080` |
| `make test` | Build and run the unit tests (`tests/test_monitor.c`) |
| `make test-api` | Build, start the server and test every HTTP endpoint with `curl` (`tests/test_api.sh`) |
| `make debug` | Clean rebuild + tests with `-g -O0 -fsanitize=address,undefined` |
| `make history` | Print the last 20 history records |
| `make clean` / `distclean` | Remove `build/` (and `logs/`) |
| `make help` | List targets |

Features: automatic header dependency tracking (`-MMD -MP`), out-of-source object
files, wildcard source discovery (adding a `.c` file needs no Makefile edit), and
`-Wall -Wextra -Wpedantic` on every build.

## 13. Implementation notes

* **Non-blocking input.** `termios` disables canonical mode and echo; `select()` waits
  for a key *or* for the next simulation tick, so the dashboard updates every second
  yet reacts instantly to key presses without threads.
* **Safe terminal restore.** `atexit()` and SIGINT/SIGTERM handlers restore the original
  terminal settings, so Ctrl+C never leaves the shell in raw mode.
* **Flicker-free redraw.** The dashboard uses cursor-home plus erase-to-end-of-line
  rather than clearing the screen on every frame; the alert area has fixed height so the
  panel never jumps.
* **Alignment with colour.** Colour escape codes are printed outside `printf` width
  specifiers so column widths are never disturbed.
* **Separation of concerns.** The rule engine takes a `const Vehicle *` and returns data;
  it never prints. The same code serves the dashboard, the logger and the tests.
* **Input validation.** Manual input is parsed with `strtof`, checked for trailing
  characters and range, and ignored (keeping the old value) when invalid.

## 14. Testing

### 14.1 Strategy

* **Unit tests** (`make test`) exercise the rule engine, simulator and logger directly
  without a terminal.
* **Sanitizer run** (`make debug`) repeats them with AddressSanitizer and UBSan.
* **API integration tests** (`make test-api`) start the real server and exercise every endpoint with `curl` (24 checks: commands, alerts, log file contents, CSV export, static files, 404s, path-traversal rejection).
* **Scripted demo** (`make demo`) is a seeded, repeatable end-to-end scenario.
* **Interactive test** — keys driven through a pseudo-terminal to confirm start,
  accelerate, history, manual input and quit work end to end.

### 14.2 Unit-test cases (27 checks, all passing)

| # | Area | Case | Expected |
|---|---|---|---|
| 1 | Safe | Healthy belted vehicle | 0 alerts, SAFE |
| 2 | Speed | Exactly 80 km/h | No alert |
| 3 | Speed | 81 km/h | WARNING |
| 4 | Speed | 121 km/h | CRITICAL |
| 5 | Fuel | 14.9 % | WARNING |
| 6 | Fuel | 4.9 % | CRITICAL |
| 7 | Fuel | 15.0 % | No alert |
| 8 | Temp | 105 °C | WARNING |
| 9 | Temp | 115 °C | CRITICAL |
| 10 | Obstacle | 8 m | WARNING |
| 11 | Obstacle | 2 m | CRITICAL |
| 12 | Seatbelt | Open, parked | No alert |
| 13 | Seatbelt | Open, 20 km/h | WARNING |
| 14 | Seatbelt | Open, 60 km/h | CRITICAL |
| 15 | Multi-alert | All five faults at once | 5 active alerts |
| 16 | Multi-alert | Same case | Overall CRITICAL |
| 17 | Simulation | Step while stopped | Ignored (ticks = 0) |
| 18 | Simulation | Target 10 km/h for 20 ticks | Speed converges near 10 |
| 19 | Simulation | Driving | Fuel decreases |
| 20 | Simulation | 20 steps | Tick counter = 20 |
| 21 | Simulation | Empty tank, target 100 | Vehicle stops |
| 22 | Simulation | Reset | Defaults restored |
| 23 | Logger | Open log file | Success |
| 24 | Logger | New alert | 1 record |
| 25 | Logger | Unchanged alert | 0 records |
| 26 | Logger | Escalation | 1 record |
| 27 | Logger | Read back | 2 records |

### 14.3 System-level test scenarios

| Scenario | Steps | Expected result |
|---|---|---|
| Overspeed | `B`, `S`, press `W` until > 80 km/h | Speed row WARNING, alert listed, logged |
| Critical overspeed | Keep accelerating beyond 120 | Status CRITICAL |
| Unbelted driving | Start, accelerate, do not press `B` | Seatbelt alert; Critical above 40 km/h |
| Low fuel | Drive fast for ~1 minute | Fuel Warning then Critical |
| Overheat | Hold 130+ km/h | Temperature Warning then Critical |
| Proximity | `M`, enter obstacle 2 m | Obstacle CRITICAL, collision-risk message |
| Multiple alerts | `M`: speed 150, fuel 10, temp 112, obstacle 2, belt n | Five alerts, overall CRITICAL |
| Stop / Reset | `S` stops clock; `R` restores defaults | Dashboard reflects state |
| History | `H`, then `C` | Records shown; file emptied |
| Ctrl+C | Press during run | Terminal restored, message printed |

## 15. Results

The system met every functional requirement. A representative dashboard (manual input of
150 km/h, 112 °C, belt off) is shown below, and the history file correctly contained
one record per new or escalated alert.

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

| Measure | Result |
|---|---|
| Compiler warnings (`-Wall -Wextra -Wpedantic`) | 0 |
| Unit checks | 27 / 27 passed |
| AddressSanitizer / UBSan | No errors |
| Source size | ≈ 1 200 lines of C across 7 source files and 7 headers (plus tests) |

## 16. User manual

1. **Install tools:** `sudo apt install build-essential`
2. **Build:** `make`
3. **Run:** `make run` (or `./build/svsm`)
4. **Drive:** `B` fasten belt → `S` start → `W` to accelerate, `X` to brake.
5. **Observe:** each row turns green / yellow / red; alerts list below the status line.
6. **Manual mode:** `M`, then type values or press Enter to keep the current one.
7. **History:** `H` to view, `C` to clear; outside the program use `make history`.
8. **Quit:** `Q` or Ctrl+C.

Run `./build/svsm --help` for command-line options.

## 17. Limitations

* Sensors are simulated; there is no real hardware or CAN-bus interface.
* Linux/POSIX only because of `termios` and `select()`.
* Text-mode UI; terminal must be at least 74 columns wide.
* Fuel consumption is accelerated for demonstration, not physically accurate.
* Obstacle alerts are distance-based only; relative speed (time-to-collision) is not used.
* History is a plain text file without rotation.

### 17.1 Web dashboard (C backend + browser front end)

Besides the terminal interface, the same C program can serve a browser dashboard (`svsm --serve`). `server.c`
implements a small single-threaded HTTP server with POSIX sockets: one `select()` loop waits for either an incoming
request or the next simulation tick, so no threads or locks are needed. The browser page (`web/`) holds **no
simulation or safety logic**; it polls `GET /api/state` and sends commands with `GET /api/cmd?a=...`. Every
reading, alert message, trip statistic and history record is produced by `simulation.c`, `monitor.c` and
`logger.c`, the same modules used by the terminal version, and history is read back from the real
`alert_history.log` file.

| Endpoint | Purpose |
|---|---|
| `/` and static files | dashboard files from `web/` |
| `/api/state` | readings, trip statistics, safety report (JSON) |
| `/api/cmd?a=...` | start, stop, reset, demo, accel, brake, refuel, target, belt, manual, rate, set, clear |
| `/api/history?n=` | alert history, newest first |
| `/api/export.log`, `/api/export.csv` | download the log file / CSV |

Security notes: the server binds to `127.0.0.1` by default, accepts `GET` only, rejects any path containing `..`,
bounds every buffer, and ignores idle pre-connections with a receive timeout.

The browser UI itself is a single-screen layout (no page scrolling): sensor cards with sparklines, a tabbed
Alerts / History / Manual / Guide panel, a controls column, a detail window with a 60-sample chart, light/dark
themes, and a bottom navigation bar on phones.

## 18. Future scope

* Real data source: read sensors via serial port or OBD-II / CAN (SocketCAN).
* Time-to-collision using closing speed, and graded braking advice.
* Tyre pressure, door status, battery voltage and lane-departure modules.
* Audible alarm (terminal bell / ALSA) and blinking critical alerts.
* Log rotation and CSV export for analytics; SQLite storage.
* `ncurses` or GTK/SDL graphical gauges; configuration file for thresholds.
* Multi-threaded sensor acquisition and a client–server telemetry mode.

## 19. Conclusion

The project delivers a complete, modular, tested safety-monitoring and dashboard
application in C for Linux. It demonstrates structured programming, enumerations and
structures, file handling, POSIX terminal control, separation of logic from presentation,
automated testing and a professional Makefile build. Because all thresholds are
centralised and the rule engine is independent of the user interface, the system can be
extended to real sensors or new safety checks with small, local changes.

## 20. References

1. B. W. Kernighan and D. M. Ritchie, *The C Programming Language*, 2nd ed., Prentice Hall.
2. ISO/IEC 9899:2011, *Programming languages — C*.
3. M. Kerrisk, *The Linux Programming Interface*, No Starch Press, 2010.
4. GNU Make Manual — <https://www.gnu.org/software/make/manual/>
5. GCC Manual — <https://gcc.gnu.org/onlinedocs/>
6. `termios(3)`, `select(2)`, `mkdir(2)` — Linux man-pages.
7. ECMA-48 / ANSI escape code reference for terminal control sequences.

## 21. Appendix: source file inventory

| File | Purpose |
|---|---|
| `include/config.h` | Thresholds and constants |
| `include/vehicle.h`, `src/vehicle.c` | Vehicle state, reset, clamp |
| `include/monitor.h`, `src/monitor.c` | Rule engine |
| `include/simulation.h`, `src/simulation.c` | Simulator and driver inputs |
| `include/dashboard.h`, `src/dashboard.c` | ANSI dashboard |
| `include/logger.h`, `src/logger.c` | Alert history file |
| `include/terminal.h`, `src/terminal.c` | Raw keyboard input |
| `src/main.c` | Program entry and control loop |
| `tests/test_monitor.c` | Unit tests |
| `Makefile` | Build automation |
