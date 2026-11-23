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
#include "ml.h"
#include "platform.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static long now_ms(void) { return plat_now_ms(); }

/** Evaluate, persist new alerts, and draw. Keeps @p prev up to date.
 *  The rule-based report and the ML prediction are computed independently. */
static bool g_ml_paused = false;            /* P key: hide ML without stopping Python */

static void refresh(const Vehicle *v, const Vehicle *shown, SafetyReport *prev,
                    const char *notice, bool redraw)
{
    SafetyReport cur, disp;
    MlPrediction ml;
    monitor_evaluate(v, &cur);               /* real state: alerts + history log */
    logger_log_changes(prev, &cur, v);
    *prev = cur;
    ml_predict(v, &ml);                      /* ml.valid == false if ML is off */
    monitor_evaluate(shown, &disp);          /* what is on screen stays self-consistent */
    dashboard_render(shown, &disp, g_ml_paused ? NULL : &ml, notice, redraw);
}

/* ------------------------------------------------------------------ */
/* smooth animation: the simulation steps once per second, the screen  */
/* eases toward the real values ~15 times per second                   */
/* ------------------------------------------------------------------ */

#define FRAME_MS 66

static float ease1(float shown, float real, float a, float eps, bool *moving)
{
    float d = real - shown;
    if (d < eps && d > -eps)
        return real;
    *moving = true;
    return shown + d * a;
}

static void ease_toward(Vehicle *s, const Vehicle *r, float dt, bool *moving)
{
    if (r->ticks < s->ticks) {               /* simulation was reset: snap */
        *s = *r;
        return;
    }
    float a = dt / (dt + 0.16f);             /* ~0.16 s time constant, no libm needed */
    s->speed_kmh     = ease1(s->speed_kmh,     r->speed_kmh,     a, 0.05f, moving);
    s->fuel_pct      = ease1(s->fuel_pct,      r->fuel_pct,      a, 0.02f, moving);
    s->engine_temp_c = ease1(s->engine_temp_c, r->engine_temp_c, a, 0.05f, moving);
    s->obstacle_m    = ease1(s->obstacle_m,    r->obstacle_m,    a, 0.05f, moving);
    s->odometer_km   = r->odometer_km;
    s->target_speed_kmh = r->target_speed_kmh;
    s->temp_bias_c   = r->temp_bias_c;
    s->seatbelt_fastened = r->seatbelt_fastened;
    s->ticks         = r->ticks;
    s->state         = r->state;
}

/** Start the ML predictor; failure is reported but never fatal. */
static void start_ml(const char *py, const char *script)
{
    fprintf(stderr, "Loading ML risk model ...\n");
    if (ml_start(py, script) != 0)
        fprintf(stderr, "ML disabled: %s\n(rule-based monitoring works normally)\n",
                ml_last_error());
}

/** --predict: one prediction from the command line. */
static int run_predict(char **vals, const char *py, const char *script)
{
    float x[5];
    for (int i = 0; i < 5; i++) {
        char *end;
        x[i] = strtof(vals[i], &end);
        if (end == vals[i] || *end != '\0') {
            fprintf(stderr, "Invalid number '%s'. Usage: --predict SPEED TEMP FUEL DIST BELT(0/1)\n", vals[i]);
            return 2;
        }
    }
    Vehicle v;
    vehicle_reset(&v);
    v.speed_kmh = x[0]; v.engine_temp_c = x[1]; v.fuel_pct = x[2];
    v.obstacle_m = x[3]; v.seatbelt_fastened = x[4] != 0.0f;

    char req[128];
    if (!ml_format_request(&v, req, sizeof req) || (x[4] != 0.0f && x[4] != 1.0f)) {
        fprintf(stderr, "Invalid input: values out of range (speed 0..%.0f, temp %.0f..%.0f, "
                        "fuel 0..100, dist 0..%.0f, belt 0/1)\n",
                SPEED_MAX_KMH, TEMP_MIN_C, TEMP_MAX_C, OBSTACLE_MAX_M);
        return 2;
    }
    if (ml_start(py, script) != 0) {
        fprintf(stderr, "ML unavailable: %s\n", ml_last_error());
        return 3;
    }
    MlPrediction p;
    int rc = 0;
    if (ml_predict(&v, &p))
        printf("ML risk: %s (confidence %.1f%%)  P(Low)=%.3f P(Moderate)=%.3f P(High)=%.3f\n",
               p.label, p.confidence * 100.0f, p.probs[0], p.probs[1], p.probs[2]);
    else {
        fprintf(stderr, "Prediction failed: %s\n", ml_last_error());
        rc = 3;
    }
    ml_stop();
    return rc;
}

static void usage(const char *prog)
{
    printf("%s v%s\n\n"
           "Usage: %s [options]\n\n"
           "  (no options)     interactive dashboard\n"
           "  --demo [N]       run a scripted N-second scenario (default 40)\n"
           "  --seed S         fixed random seed (repeatable runs)\n"
           "  --predict S T F D B  one ML risk prediction: speed temp fuel obstacle belt(0/1)\n"
           "  --no-ml          disable the ML risk prediction (rules only)\n"
           "  --ml-python CMD  Python interpreter for the ML model (default python3)\n"
           "  --ml-script FILE predictor script (default ml/predict.py)\n"
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

    Vehicle v, shown;
    SafetyReport prev;
    char notice[96] = "Press S to start the simulation.";

    vehicle_reset(&v);
    shown = v;
    memset(&prev, 0, sizeof prev);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);   /* one write per frame: no flicker */
    fputs("\033[?1049h", stdout);          /* alternate screen: redraw in place */
    term_clear();

    bool running = true, dirty = true, moving = false;
    long last_tick = now_ms(), last_frame = now_ms();

    while (running && !term_interrupted()) {
        if (dirty || moving) {
            long t = now_ms();
            moving = false;
            ease_toward(&shown, &v, (float)(t - last_frame) / 1000.0f, &moving);
            last_frame = t;
            refresh(&v, &shown, &prev, notice, true);
            dirty = false;
        }

        int wait = -1;                               /* idle: sleep on input */
        if (moving)
            wait = FRAME_MS;                         /* animating: next frame */
        if (v.state == SIM_RUNNING) {
            int to_tick = (int)(last_tick + SIM_TICK_MS - now_ms());
            if (to_tick < 0) to_tick = 0;
            if (wait < 0 || to_tick < wait) wait = to_tick;
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

        if (key == KEY_UP)   key = 'w';
        if (key == KEY_DOWN) key = 'x';
        if (key == KEY_RIGHT || key == KEY_LEFT) {
            sim_toggle_seatbelt(&v);
            key = '\1';                           /* handled: arrow left/right = seatbelt */
        }

        switch (key < 256 ? tolower(key) : 0) {
        case '\1':
            snprintf(notice, sizeof notice, "Seatbelt %s.",
                     v.seatbelt_fastened ? "fastened" : "released");
            break;
        case '1': case '2': case '3': case '4': case '5': {
            int id = key - '1';
            sim_scenario(&v, id);
            if (v.state != SIM_RUNNING) { sim_start(&v); last_tick = now_ms(); }
            snprintf(notice, sizeof notice, "Scenario %d: %s", id + 1, sim_scenario_name(id));
            break;
        }
        case 't':
            sim_heat(&v, 10.0f);
            snprintf(notice, sizeof notice, "Cooling fault +10 C (engine runs hotter)");
            break;
        case 'y':
            sim_heat(&v, -10.0f);
            snprintf(notice, sizeof notice, "Cooling fault -10 C");
            break;
        case 'o':
            sim_obstacle_ahead(&v, 6.0f);
            snprintf(notice, sizeof notice, "Obstacle appears 6 m ahead!");
            break;
        case 'p':
            g_ml_paused = !g_ml_paused;
            snprintf(notice, sizeof notice, "ML prediction %s.", g_ml_paused ? "paused" : "resumed");
            break;
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
            snprintf(notice, sizeof notice, "Unknown key '%c'.", (key < 256 && isprint(key)) ? key : '?');
        }
    }

    fputs("\033[?1049l", stdout);          /* back to the normal screen */
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
        refresh(&v, &v, &prev, NULL, false);
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
    bool use_ml = true;
    const char *ml_py = NULL, *ml_script = NULL;
    char **predict_args = NULL;

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
        } else if (!strcmp(argv[i], "--no-ml")) {
            use_ml = false;
        } else if (!strcmp(argv[i], "--ml-python") && i + 1 < argc) {
            ml_py = argv[++i];
        } else if (!strcmp(argv[i], "--ml-script") && i + 1 < argc) {
            ml_script = argv[++i];
        } else if (!strcmp(argv[i], "--predict")) {
            if (i + 5 >= argc) {
                fprintf(stderr, "--predict needs 5 values: SPEED TEMP FUEL DIST BELT(0/1)\n");
                return 2;
            }
            predict_args = &argv[i + 1];
            i += 5;
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
    if (predict_args)
        return run_predict(predict_args, ml_py, ml_script);
    if (use_ml)
        start_ml(ml_py, ml_script);

    if (serve) {
        if (port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port.\n");
            return 2;
        }
        int src = server_run(port, web_dir, pub, open_browser);
        ml_stop();
        return src;
    }
    int rc = demo ? run_demo(demo_secs > 0 ? demo_secs : 40) : run_interactive();
    ml_stop();
    return rc;
}
