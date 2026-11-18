/**
 * @file    dashboard.c
 * @brief   Rich UTF-8 / ANSI text-mode digital dashboard.
 *
 * Visual elements
 *   - double-line frame whose colour follows the overall safety status
 *   - large 3-row speed readout
 *   - gauges whose cells are coloured by zone (green / yellow / red)
 *   - full-width status banner (blinks when CRITICAL)
 *   - keycap-style control hints
 *
 * Every line is built through out(), which counts *visible* columns (UTF-8
 * code points, not bytes) so the right-hand border always lines up.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dashboard.h"
#include "config.h"

#define INNER_W 70               /* visible columns between the borders */

/* ANSI codes */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define BLINK   "\033[5m"
#define REV     "\033[7m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RED     "\033[1;31m"
#define CYAN    "\033[36m"
#define WHITE   "\033[97m"
#define BG_GRN  "\033[42;30;1m"
#define BG_YEL  "\033[43;30;1m"
#define BG_RED  "\033[41;97;1m"

static bool        g_color  = true;
static bool        g_redraw = false;
static int         g_col    = 0;       /* visible columns printed on this line */
static const char *g_frame  = "";      /* frame colour for this frame */

void dashboard_set_color(bool on) { g_color = on; }

static const char *c(const char *code) { return g_color ? code : ""; }

static const char *sev_color(Severity s)
{
    switch (s) {
    case LEVEL_CRITICAL: return c(RED);
    case LEVEL_WARNING:  return c(YELLOW);
    default:             return c(GREEN);
    }
}

/* ---- low-level output ------------------------------------------------ */

static int utf8_cols(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

/** Print coloured text and advance the visible-column counter. */
static void out(const char *color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    g_col += utf8_cols(buf);
    printf("%s%s%s", color, buf, *color ? c(RESET) : "");
}

static void eol(void)
{
    fputs(g_redraw ? "\033[K\n" : "\n", stdout);
}

static void pad(int n)
{
    for (int i = 0; i < n; i++)
        putchar(' ');
    g_col += n > 0 ? n : 0;
}

static void line_begin(void)
{
    printf("%s║%s", g_frame, c(RESET));
    g_col = 0;
}

static void line_end(void)
{
    pad(INNER_W - g_col);
    printf("%s║%s", g_frame, c(RESET));
    eol();
}

static void hrule(const char *l, const char *m, const char *r)
{
    printf("%s%s", g_frame, l);
    for (int i = 0; i < INNER_W; i++)
        fputs(m, stdout);
    printf("%s%s", r, c(RESET));
    eol();
}

static void blank_line(void) { line_begin(); line_end(); }

/* ---- widgets --------------------------------------------------------- */

static Severity zone(float v, float warn, float crit, bool high_bad)
{
    if (high_bad)
        return v > crit ? LEVEL_CRITICAL : (v > warn ? LEVEL_WARNING : LEVEL_SAFE);
    return v < crit ? LEVEL_CRITICAL : (v < warn ? LEVEL_WARNING : LEVEL_SAFE);
}

/** Zone-coloured gauge: each filled cell takes the colour of its zone. */
static void gauge(float value, float vmax, float warn, float crit,
                  bool high_bad, int width)
{
    float f = value / vmax;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    int fill = (int)(f * width + 0.5f);

    for (int i = 0; i < width; i++) {
        float cell = ((float)i + 0.5f) / (float)width * vmax;
        if (i < fill)
            out(sev_color(zone(cell, warn, crit, high_bad)), "█");
        else
            out(c(DIM), "░");
    }
}

static void badge(Severity s, bool alerting)
{
    if (!alerting)
        out(c(GREEN), "● OK");
    else if (s == LEVEL_CRITICAL)
        out(g_color ? BLINK RED : "", "■ CRITICAL");
    else
        out(c(YELLOW), "▲ WARNING");
}

static const char *const DIGIT[10][3] = {
    {"┏━┓", "┃ ┃", "┗━┛"}, {" ╻ ", " ┃ ", " ╹ "}, {"╺━┓", "┏━┛", "┗━╸"},
    {"╺━┓", " ━┫", "╺━┛"}, {"╻ ╻", "┗━┫", "  ╹"}, {"┏━╸", "┗━┓", "╺━┛"},
    {"┏━╸", "┣━┓", "┗━┛"}, {"╺━┓", "  ┃", "  ╹"}, {"┏━┓", "┣━┫", "┗━┛"},
    {"┏━┓", "┗━┫", "╺━┛"}
};

/** One row (0..2) of a 3-digit large number. */
static void big_number_row(int value, int row, const char *color)
{
    if (value < 0)   value = 0;
    if (value > 999) value = 999;

    int d[3] = { value / 100, (value / 10) % 10, value % 10 };
    bool started = false;

    for (int i = 0; i < 3; i++) {
        if (d[i] != 0 || started || i == 2) {
            started = true;
            out(color, "%s", DIGIT[d[i]][row]);
        } else {
            pad(3);                           /* suppress leading zeros */
        }
        if (i < 2)
            pad(1);
    }
}

static Severity level_of(const SafetyReport *r, AlertType t)
{
    return r->alerts[t].active ? r->alerts[t].level : LEVEL_SAFE;
}

/** Label | value | gauge | badge row. */
static void sensor_row(const char *label, const char *value, float v,
                       float vmax, float warn, float crit, bool high_bad,
                       const SafetyReport *r, AlertType t)
{
    line_begin();
    pad(2);
    out(c(BOLD), "%-9s", label);
    out(sev_color(level_of(r, t)), "%s", value);
    pad(12 - utf8_cols(value));
    gauge(v, vmax, warn, crit, high_bad, 30);
    pad(1);
    badge(level_of(r, t), r->alerts[t].active);
    line_end();
}

static void keycap(const char *key, const char *label)
{
    out(c(REV), " %s ", key);
    out(c(DIM), " %s  ", label);
}

/* ---- main entry ------------------------------------------------------ */

void dashboard_render(const Vehicle *v, const SafetyReport *r,
                      const char *notice, bool redraw)
{
    char buf[128];
    g_redraw = redraw;
    g_frame  = r->overall == LEVEL_SAFE ? c(CYAN) : sev_color(r->overall);

    if (redraw)
        fputs("\033[H", stdout);

    /* ---- title ------------------------------------------------------ */
    hrule("╔", "═", "╗");
    {
        const char *title = "◆  SMART VEHICLE SAFETY & DASHBOARD SYSTEM  ◆";
        int len = utf8_cols(title);
        line_begin();
        pad((INNER_W - len) / 2);
        out(c(BOLD WHITE), "%s", title);
        line_end();
    }
    hrule("╠", "═", "╣");

    /* ---- simulation info -------------------------------------------- */
    unsigned long t = v->ticks;
    line_begin();
    pad(2);
    if (v->state == SIM_RUNNING) out(c(GREEN), "● RUNNING");
    else                         out(c(RED),   "■ STOPPED");
    pad(3);
    out(c(DIM), "TIME ");
    out("", "%02lu:%02lu:%02lu", t / 3600, (t / 60) % 60, t % 60);
    pad(3);
    out(c(DIM), "ODOMETER ");
    out("", "%.2f km", v->odometer_km);
    pad(3);
    out(c(DIM), "TARGET ");
    out("", "%.0f km/h", v->target_speed_kmh);
    line_end();
    hrule("╠", "═", "╣");

    /* ---- speed: big number + gauge ---------------------------------- */
    Severity ssev   = level_of(r, ALERT_OVERSPEED);
    const char *sc  = sev_color(ssev);
    int speed       = (int)(v->speed_kmh + 0.5f);

    line_begin();
    pad(2);
    big_number_row(speed, 0, sc);
    pad(4);
    out(c(BOLD), "SPEED ");
    out(c(DIM), "km/h   (limit %.0f)", SPEED_WARN_KMH);
    line_end();

    line_begin();
    pad(2);
    big_number_row(speed, 1, sc);
    pad(4);
    gauge(v->speed_kmh, 160.0f, SPEED_WARN_KMH, SPEED_CRIT_KMH, true, 40);
    pad(1);
    badge(ssev, r->alerts[ALERT_OVERSPEED].active);
    line_end();

    line_begin();
    pad(2);
    big_number_row(speed, 2, sc);
    pad(4);
    {
        char scale[64];
        snprintf(scale, sizeof scale, "%-20s%-17s%3s", "0", "80", "160");
        out(c(DIM), "%s", scale);
    }
    line_end();
    hrule("╠", "═", "╣");

    /* ---- other sensors ---------------------------------------------- */
    snprintf(buf, sizeof buf, "%.1f %%", v->fuel_pct);
    sensor_row("FUEL", buf, v->fuel_pct, 100.0f, FUEL_WARN_PCT, FUEL_CRIT_PCT,
               false, r, ALERT_LOW_FUEL);

    snprintf(buf, sizeof buf, "%.0f °C", v->engine_temp_c);
    sensor_row("ENGINE", buf, v->engine_temp_c, 130.0f, TEMP_WARN_C, TEMP_CRIT_C,
               true, r, ALERT_OVERHEAT);

    snprintf(buf, sizeof buf, "%.1f m", v->obstacle_m);
    sensor_row("OBSTACLE", buf, v->obstacle_m, 100.0f, OBST_WARN_M, OBST_CRIT_M,
               false, r, ALERT_OBSTACLE);

    {
        Severity bs = level_of(r, ALERT_SEATBELT);
        line_begin();
        pad(2);
        out(c(BOLD), "%-9s", "SEATBELT");
        out(sev_color(bs), "%-12s", v->seatbelt_fastened ? "FASTENED" : "UNFASTENED");
        if (v->seatbelt_fastened)
            out(c(GREEN), "▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰▰");
        else
            out(sev_color(bs == LEVEL_SAFE ? LEVEL_WARNING : bs),
                "▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱▱");
        pad(1);
        badge(bs, r->alerts[ALERT_SEATBELT].active);
        line_end();
    }
    hrule("╠", "═", "╣");

    /* ---- status banner ---------------------------------------------- */
    {
        const char *bg   = r->overall == LEVEL_CRITICAL ? BG_RED
                         : r->overall == LEVEL_WARNING  ? BG_YEL : BG_GRN;
        const char *icon = r->overall == LEVEL_CRITICAL ? "■"
                         : r->overall == LEVEL_WARNING  ? "▲" : "✔";
        char msg[96];
        snprintf(msg, sizeof msg, "%s  SAFETY STATUS: %s   |   %d active alert%s",
                 icon, severity_name(r->overall), r->active_count,
                 r->active_count == 1 ? "" : "s");

        int inner = INNER_W - 2;
        int len   = utf8_cols(msg);
        int left  = (inner - len) / 2;

        line_begin();
        pad(1);
        out(c(bg), "%*s%s%*s", left, "", msg, inner - len - left, "");
        pad(1);
        line_end();
    }
    hrule("╠", "═", "╣");

    /* ---- alert list (fixed height so the panel never jumps) --------- */
    line_begin();
    pad(2);
    out(c(DIM), "ACTIVE ALERTS");
    line_end();

    int shown = 0;
    for (int i = 0; i < ALERT_COUNT; i++) {
        const Alert *a = &r->alerts[i];
        if (!a->active)
            continue;
        line_begin();
        pad(2);
        out(sev_color(a->level), "%s %-9s %s",
            a->level == LEVEL_CRITICAL ? "■" : "▲",
            severity_name(a->level), a->message);
        line_end();
        shown++;
    }
    if (shown == 0) {
        line_begin();
        pad(2);
        out(c(GREEN), "✔ No active alerts - all systems normal");
        line_end();
        shown = 1;
    }
    for (; shown < ALERT_COUNT; shown++)
        blank_line();

    /* ---- controls (interactive only) -------------------------------- */
    if (redraw) {
        hrule("╠", "═", "╣");
        line_begin(); pad(1);
        keycap("S", "Start/Stop"); keycap("R", "Reset");
        keycap("W", "Faster");     keycap("X", "Slower");
        keycap("B", "Seatbelt");
        line_end();
        line_begin(); pad(1);
        keycap("F", "Refuel");  keycap("M", "Manual");
        keycap("H", "History"); keycap("C", "Clear log");
        keycap("Q", "Quit");
        line_end();
    }
    hrule("╚", "═", "╝");

    if (notice && *notice) {
        printf(" %s▸ %s%s", c(CYAN), notice, c(RESET));
    }
    eol();

    if (redraw)
        fputs("\033[J", stdout);
    fflush(stdout);
}
