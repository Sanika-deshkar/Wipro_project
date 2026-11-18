/**
 * @file    main.c
 * @brief   Entry point: command line parsing, interactive loop, demo mode.
 *
 * Usage:
 *   svsm                 interactive dashboard (keyboard controlled)
 *   svsm --serve         C backend for the web dashboard (http://localhost:8080)
 *   svsm --demo [N]      scripted N-second scenario, no keyboard needed
 *   svsm --help
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "vehicle.h"
#include "monitor.h"
#include "simulation.h"
#include "dashboard.h"
#include "logger.h"
#include "terminal.h"
#include "server.h"
#include "platform.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static long now_ms(void) { return plat_now_ms(); }

/** Evaluate, persist new alerts, and draw. Keeps @p prev up to date. */
static void refresh(const Vehicle *v, SafetyReport *prev, const char *notice,
                    bool redraw)
{
    SafetyReport cur;
    monitor_evaluate(v, &cur);
    logger_log_changes(prev, &cur, v);
    *prev = cur;
    dashboard_render(v, &cur, notice, redraw);
}

static void usage(const char *prog)
{
    printf("%s v%s\n\n"
           "Usage: %s [options]\n\n"
           "  (no options)     interactive dashboard\n"
           "  --demo [N]       run a scripted N-second scenario (default 40)\n"
           "  --seed S         fixed random seed (repeatable runs)\n"
           "  --log FILE       alert history file (default %s)\n"
           "  --history [N]    print the last N history records and exit\n"
           "  --serve [PORT]   run the C backend + web dashboard (default port 8080)\n"
           "  --web DIR        folder with the web dashboard (default web)\n"
           "  --public         listen on all interfaces, not just localhost\n"
           "  --open           open the dashboard in your browser (with --serve)\n"
           "  --no-color       disable ANSI colours\n"
           "  --version        show version\n"
           "  --help           show this help\n",
           APP_NAME, APP_VERSION, prog, LOG_FILE_DEFAULT);
}

/* ------------------------------------------------------------------ */
/* manual input                                                        */
/* ------------------------------------------------------------------ */

/** Ask for a float. Empty input keeps the current value. */
static void ask_float(const char *label, float *value, float lo, float hi)
{
    char line[64], prompt[96];
    snprintf(prompt, sizeof prompt, "%s [%.1f] (%.0f..%.0f): ", label, *value, lo, hi);

    if (!term_prompt(prompt, line, sizeof line) || line[0] == '\0')
        return;

    char *end;
    float x = strtof(line, &end);
    if (end == line || *end != '\0' || x < lo || x > hi) {
        printf("  invalid value - keeping %.1f\n", *value);
        return;
    }
    *value = x;
}

static void manual_input(Vehicle *v)
{
    char line[16];

    term_clear();
    printf("=== MANUAL SENSOR INPUT (press Enter to keep a value) ===\n\n");
    ask_float("Speed (km/h)",       &v->speed_kmh,     0.0f, SPEED_MAX_KMH);
    ask_float("Fuel (%)",           &v->fuel_pct,      0.0f, 100.0f);
    ask_float("Engine temp (C)",    &v->engine_temp_c, TEMP_MIN_C, TEMP_MAX_C);
    ask_float("Obstacle dist (m)",  &v->obstacle_m,    0.0f, OBSTACLE_MAX_M);

    if (term_prompt("Seatbelt fastened? (y/n): ", line, sizeof line) && line[0])
        v->seatbelt_fastened = (tolower((unsigned char)line[0]) == 'y');

    v->target_speed_kmh = v->speed_kmh;      /* simulator continues from here */
    vehicle_clamp(v);
    term_clear();
}

static void show_history(void)
{
    term_clear();
    printf("=== ALERT HISTORY (last %d records, %s) ===\n\n",
           LOG_VIEW_LINES, logger_path());
    int n = logger_show_recent(stdout, LOG_VIEW_LINES);
    if (n <= 0)
        printf("(no alerts recorded yet)\n");
    printf("\nPress any key to return...");
    fflush(stdout);
    term_read_key(-1);
    term_clear();
}

/* ------------------------------------------------------------------ */
/* interactive mode                                                    */
/* ------------------------------------------------------------------ */

static int run_interactive(void)
{
    if (!term_raw_enable()) {
        fprintf(stderr, "Interactive mode needs a terminal. "
                        "Use --demo for non-interactive output.\n");
        return 1;
    }

    Vehicle v;
    SafetyReport prev;
    char notice[96] = "Press S to start the simulation.";

    vehicle_reset(&v);
    memset(&prev, 0, sizeof prev);
    term_clear();

    bool running = true, dirty = true;
    long last_tick = now_ms();

    while (running && !term_interrupted()) {
        if (dirty) {
            refresh(&v, &prev, notice, true);
            dirty = false;
        }

        int wait = -1;                               /* stopped: sleep on input */
        if (v.state == SIM_RUNNING) {
            wait = (int)(last_tick + SIM_TICK_MS - now_ms());
            if (wait < 0) wait = 0;
        }

        int key = term_read_key(wait);

        if (v.state == SIM_RUNNING && now_ms() - last_tick >= SIM_TICK_MS) {
            sim_step(&v);
            last_tick = now_ms();
            dirty = true;
        }
        if (key <= 0)
            continue;

        dirty = true;
        notice[0] = '\0';

        switch (tolower(key)) {
        case 's':
            if (v.state == SIM_RUNNING) {
                sim_stop(&v);
                snprintf(notice, sizeof notice, "Simulation stopped.");
            } else {
                sim_start(&v);
                last_tick = now_ms();
                snprintf(notice, sizeof notice, "Simulation started.");
            }
            break;
        case 'r':
            sim_reset(&v);
            memset(&prev, 0, sizeof prev);
            snprintf(notice, sizeof notice, "Simulation reset.");
            break;
        case 'w':
            sim_accelerate(&v);
            snprintf(notice, sizeof notice, "Target speed: %.0f km/h", v.target_speed_kmh);
            break;
        case 'x':
            sim_brake(&v);
            snprintf(notice, sizeof notice, "Target speed: %.0f km/h", v.target_speed_kmh);
            break;
        case 'b':
            sim_toggle_seatbelt(&v);
            snprintf(notice, sizeof notice, "Seatbelt %s.",
                     v.seatbelt_fastened ? "fastened" : "released");
            break;
        case 'f':
            sim_refuel(&v);
            snprintf(notice, sizeof notice, "Tank refilled to 100%%.");
            break;
        case 'm':
            manual_input(&v);
            snprintf(notice, sizeof notice, "Manual values applied.");
            break;
        case 'h':
            show_history();
            break;
        case 'c':
            snprintf(notice, sizeof notice, logger_clear() == 0
                     ? "Alert history cleared." : "Could not clear history.");
            break;
        case 'q':
            running = false;
            break;
        default:
            snprintf(notice, sizeof notice, "Unknown key '%c'.", isprint(key) ? key : '?');
        }
    }

    term_raw_disable();
    printf("\nSession ended. Alert history saved in %s\n", logger_path());
    return 0;
}

/* ------------------------------------------------------------------ */
/* demo mode (no keyboard, deterministic script)                       */
/* ------------------------------------------------------------------ */

static int run_demo(int seconds)
{
    Vehicle v;
    SafetyReport prev;

    vehicle_reset(&v);
    v.fuel_pct = 40.0f;                      /* so low-fuel shows up in demo */
    memset(&prev, 0, sizeof prev);
    sim_start(&v);

    for (int t = 0; t < seconds; t++) {
        sim_demo_script(&v, t);
        sim_step(&v);
        refresh(&v, &prev, NULL, false);
    }
    printf("\nDemo finished. Alert history appended to %s\n", logger_path());
    return 0;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    plat_init();
    int  demo_secs = 0, history_n = 0;
    bool demo = false, history = false, color = plat_stdout_is_tty();
    bool serve = false, pub = false, open_browser = false;
    int  port = 8080;
    const char *web_dir = "web";
    unsigned int seed = 0;
    const char *log_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("%s v%s\n", APP_NAME, APP_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--no-color")) {
            color = false;
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            log_path = argv[++i];
        } else if (!strcmp(argv[i], "--demo")) {
            demo = true;
            demo_secs = (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
                        ? atoi(argv[++i]) : 40;
        } else if (!strcmp(argv[i], "--serve")) {
            serve = true;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
                port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--web") && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (!strcmp(argv[i], "--public")) {
            pub = true;
        } else if (!strcmp(argv[i], "--open")) {
            open_browser = true;
        } else if (!strcmp(argv[i], "--history")) {
            history = true;
            history_n = (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
                        ? atoi(argv[++i]) : LOG_VIEW_LINES;
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (logger_init(log_path) != 0)
        fprintf(stderr, "warning: cannot write alert history (%s)\n", logger_path());

    dashboard_set_color(color);
    sim_seed(seed);

    if (history) {
        if (history_n < 1) history_n = LOG_VIEW_LINES;
        if (logger_show_recent(stdout, history_n) <= 0)
            puts("(no alerts recorded yet)");
        return 0;
    }
    if (serve) {
        if (port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port.\n");
            return 2;
        }
        return server_run(port, web_dir, pub, open_browser);
    }
    if (demo)
        return run_demo(demo_secs > 0 ? demo_secs : 40);

    return run_interactive();
}
