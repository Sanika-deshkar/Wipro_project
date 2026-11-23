/**
 * @file    dashboard.c
 * @brief   UTF-8 / ANSI text-mode digital dashboard ("SVSM / Smart Vehicle Monitor").
 *
 * Layout (top to bottom, horizontally centred in the terminal)
 *   header + live status line
 *   SPEED card        large number, zone-coloured slider, overspeed flag
 *   VEHICLE HEALTH    2 x 2 grid: fuel, engine, obstacle, seatbelt
 *   SAFETY ASSESSMENT rule-based status + active alerts (fixed height)
 *   ML DRIVER RISK    machine-learning prediction, kept separate from the rules
 *   TELEMETRY         rolling speed / ML-risk history (last 30 s)
 *   CONTROLS          key hints, scenarios, fault injection (interactive only)
 *
 * The full layout needs about 60 terminal rows.  On smaller windows (and in
 * --demo output) a compact variant without the blank spacer lines is drawn
 * (about 46 rows).
 *
 * Every line is built through out(), which counts *visible* columns (UTF-8
 * code points, not bytes) so the right-hand borders always line up.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dashboard.h"
#include "config.h"
#include "ml.h"
#include "platform.h"

/* Every frame is clipped to the window height (g_maxrows) so a redraw can never
 * scroll the terminal: once the limit is reached further output is muted. */
static int  g_line    = 0;
static int  g_maxrows = 0;               /* 0 = unlimited (demo / pipes) */
static bool g_mute    = false;

static int d_printf(const char *fmt, ...)
{
    va_list ap;
    int n = 0;
    if (g_mute) return 0;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
static int d_fputs(const char *str, FILE *f) { return g_mute ? 0 : fputs(str, f); }
static int d_putchar(int ch)                  { return g_mute ? ch : putchar(ch); }
#define printf  d_printf
#define fputs   d_fputs
#define putchar d_putchar

#define IW      58               /* visible columns between a card's borders */
#define LW      29               /* width of the left column of the health grid */
#define TREND_N 30

/* ANSI codes */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define BLINK   "\033[5m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RED     "\033[1;31m"
#define CYAN    "\033[36m"
#define WHITE   "\033[97m"

static bool        g_color   = true;
static bool        g_redraw  = false;
static bool        g_compact = false;
static bool        g_mini    = false;       /* short window: fewest rows */
static int         g_col     = 0;       /* visible columns printed on this line */
static const char *g_frame   = "";      /* card border colour for this frame */
static int         g_mx      = 2;       /* left margin that centres the dashboard */

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

static const char *ml_color(MlLevel l)
{
    return l == ML_HIGH ? c(RED) : l == ML_MODERATE ? c(YELLOW) : c(GREEN);
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
    g_col = 0;
    g_line++;
    if (g_maxrows && g_line >= g_maxrows)
        g_mute = true;
}

static void pad(int n)
{
    for (int i = 0; i < n; i++)
        putchar(' ');
    g_col += n > 0 ? n : 0;
}

static void center(const char *color, const char *text)
{
    int len = utf8_cols(text);
    pad((IW - len) / 2);
    out(color, "%s", text);
}

/** Left margin (centres the dashboard); not counted in g_col. */
static void margin(void)
{
    for (int i = 0; i < g_mx; i++)
        putchar(' ');
}

/* Blank spacer lines between sections.  Which ones are drawn depends on how many
 * spare rows the window has (see dashboard_render): the gap between the dashboard
 * and the controls is always kept, the others are added by priority. */
typedef enum {
    GAP_RULE, GAP_HDR, GAP_SPEED, GAP_HEALTH, GAP_ASSESS, GAP_ML, GAP_TREND,
    GAP_CTRL1, GAP_CTRL2, GAP_COUNT
} GapId;

static bool g_gap_on[GAP_COUNT];

static void gap(GapId id)
{
    if (g_gap_on[id])
        eol();
}

/* ---- cards ----------------------------------------------------------- */

static void section(const char *title)
{
    if (g_mini)
        return;
    margin();
    out(c(DIM), "%s", title);
    eol();
}

static void rule_top(const char *title, bool grid)
{
    margin();
    printf("%s┌", g_frame);
    int used = 0;
    if (title) {
        printf("─ %s ", title);
        used = 3 + utf8_cols(title);
    }
    for (int i = used; i < IW; i++)
        fputs(grid && i == LW ? "┬" : "─", stdout);
    printf("┐%s", c(RESET));
    eol();
}

static void box_mid(void)
{
    margin();
    printf("%s├", g_frame);
    for (int i = 0; i < IW; i++)
        fputs(i == LW ? "┼" : "─", stdout);
    printf("┤%s", c(RESET));
    eol();
}

static void box_bottom(bool grid)
{
    margin();
    printf("%s└", g_frame);
    for (int i = 0; i < IW; i++)
        fputs(grid && i == LW ? "┴" : "─", stdout);
    printf("┘%s", c(RESET));
    eol();
}

static void line_begin(void)
{
    margin();
    printf("%s│%s", g_frame, c(RESET));
    g_col = 0;
}

static void line_end(void)
{
    pad(IW - g_col);
    printf("%s│%s", g_frame, c(RESET));
    eol();
}

static void blank_line(void)
{
    if (g_compact)
        return;
    line_begin();
    line_end();
}

/** Column divider inside a grid row. */
static void cell_sep(void)
{
    pad(LW - g_col);
    printf("%s│%s", g_frame, c(RESET));
    g_col++;
}

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
    int fill = (int)(f * (float)width + 0.5f);

    for (int i = 0; i < width; i++) {
        float cell = ((float)i + 0.5f) / (float)width * vmax;
        if (i < fill)
            out(sev_color(zone(cell, warn, crit, high_bad)), "█");
        else
            out(c(DIM), "░");
    }
}

static Severity level_of(const SafetyReport *r, AlertType t)
{
    return r->alerts[t].active ? r->alerts[t].level : LEVEL_SAFE;
}

static void tag(Severity s, bool alerting, const char *ok_text)
{
    char buf[24];
    if (!alerting)
        snprintf(buf, sizeof buf, "[ %s ]", ok_text);
    else
        snprintf(buf, sizeof buf, "[ %s ]", s == LEVEL_CRITICAL ? "CRITICAL" : "WARNING");
    out(sev_color(s), "%s", buf);
}

/** "[K] label" padded to @p width visible columns. */
static void key(const char *k, const char *label, int width)
{
    int start = g_col;
    out(c(CYAN), "[%s]", k);
    out("", " %s", label);
    pad(width - (g_col - start));
}

/* ---- rolling trend history ------------------------------------------- */

static float g_hist_speed[TREND_N];
static int   g_hist_risk[TREND_N];          /* -1 = no prediction */
static int   g_trend_pos = 0;               /* index of the oldest sample */
static long  g_trend_ticks = -1;

/** Push one sample per simulated second; clear when the run restarts. */
static void trend_update(const Vehicle *v, const MlPrediction *ml)
{
    long t = (long)v->ticks;
    if (g_trend_ticks < 0 || t < g_trend_ticks) {             /* first call or reset */
        for (int k = 0; k < TREND_N; k++) { g_hist_speed[k] = -1.0f; g_hist_risk[k] = -1; }
        g_trend_pos = 0;
        g_trend_ticks = t;
        return;
    }
    if (t == g_trend_ticks)
        return;
    g_trend_ticks = t;
    g_hist_speed[g_trend_pos] = v->speed_kmh;                 /* overwrite oldest ... */
    g_hist_risk[g_trend_pos]  = (ml && ml->valid) ? (int)ml->level : -1;
    g_trend_pos = (g_trend_pos + 1) % TREND_N;                /* ... which is now the newest */
}

/* ---- sections -------------------------------------------------------- */

static void heavy_rule(void)
{
    margin();
    printf("%s", c(DIM));
    for (int i = 0; i < IW + 2; i++)
        fputs("━", stdout);
    printf("%s", c(RESET));
    eol();
}

static void draw_header(const Vehicle *v)
{
    unsigned long t = v->ticks;

    margin();
    out(c(BOLD WHITE), "SVSM");
    out(c(DIM), "  /  ");
    out(c(BOLD), "SMART VEHICLE MONITOR");
    eol();
    heavy_rule();
    gap(GAP_RULE);

    margin();
    pad(2);
    if (v->state == SIM_RUNNING) out(c(GREEN), "● LIVE");
    else                         out(c(RED),   "■ STOPPED");
    pad(27 - g_col);
    out("", "%02lu:%02lu:%02lu", t / 3600, (t / 60) % 60, t % 60);
    pad(39 - g_col);
    out(c(DIM), "TRIP  ");
    out("", "%.2f km", v->odometer_km);
    eol();
    gap(GAP_HDR);
}

static void draw_speed(const Vehicle *v, const SafetyReport *r)
{
    Severity ssev = level_of(r, ALERT_OVERSPEED);
    char num[16];
    snprintf(num, sizeof num, "%.0f", v->speed_kmh);

    rule_top("SPEED", false);
    blank_line();
    if (g_mini) {
        char both[32];
        snprintf(both, sizeof both, "%s km/h", num);
        line_begin();
        center(g_color ? BOLD : "", both);
        line_end();
    } else {
        line_begin();
        center(g_color ? BOLD : "", num);
        line_end();
        line_begin();
        center(c(DIM), "km/h");
        line_end();
    }
    blank_line();

    /* slider: zone-coloured rail with a marker at the current speed */
    {
        const int W = 40;
        int mark = (int)(v->speed_kmh / SPEED_MAX_KMH * (float)(W - 1) + 0.5f);
        if (mark < 0) mark = 0;
        if (mark > W - 1) mark = W - 1;
        line_begin();
        pad(3);
        out(c(DIM), "0 ");
        for (int i = 0; i < W; i++) {
            float cell = ((float)i + 0.5f) / (float)W * SPEED_MAX_KMH;
            if (i == mark)
                out(c(BOLD WHITE), "●");
            else
                out(sev_color(zone(cell, SPEED_WARN_KMH, SPEED_CRIT_KMH, true)), "━");
        }
        out(c(DIM), " %.0f", SPEED_MAX_KMH);
        line_end();
    }
    {
        char lim[32];
        snprintf(lim, sizeof lim, "LIMIT %.0f", SPEED_WARN_KMH);
        line_begin();
        if (g_mini) {                       /* limit and status share one row */
            pad(2);
            if (r->alerts[ALERT_OVERSPEED].active)
                out(g_color && ssev == LEVEL_CRITICAL ? BLINK RED : sev_color(ssev), "▲ OVERSPEED");
            else
                out(c(GREEN), "✔ WITHIN LIMIT");
            pad(40 - g_col);
            out(c(DIM), "%s", lim);
            line_end();
        } else {
            center(c(DIM), lim);
            line_end();
        }
    }
    if (!g_mini) {
        line_begin();
        pad(2);
        if (r->alerts[ALERT_OVERSPEED].active)
            out(g_color && ssev == LEVEL_CRITICAL ? BLINK RED : sev_color(ssev), "▲ OVERSPEED");
        else
            out(c(GREEN), "✔ WITHIN LIMIT");
        line_end();
    }
    box_bottom(false);
    gap(GAP_SPEED);
}

static void draw_health(const Vehicle *v, const SafetyReport *r)
{
    Severity fs = level_of(r, ALERT_LOW_FUEL), es = level_of(r, ALERT_OVERHEAT);
    Severity os = level_of(r, ALERT_OBSTACLE), bs = level_of(r, ALERT_SEATBELT);

    section("VEHICLE HEALTH");
    rule_top(NULL, true);

    /* row 1: labels, values + gauges, status */
    line_begin(); pad(1); out(c(BOLD), "FUEL"); cell_sep(); pad(1); out(c(BOLD), "ENGINE"); line_end();

    line_begin();
    pad(1);
    out(sev_color(fs), "%3.0f%%  ", v->fuel_pct);
    out("", "[");
    gauge(v->fuel_pct, 100.0f, FUEL_WARN_PCT, FUEL_CRIT_PCT, false, 10);
    out("", "]");
    cell_sep();
    pad(1);
    out(sev_color(es), "%3.0f°C ", v->engine_temp_c);
    out("", "[");
    gauge(v->engine_temp_c, TEMP_MAX_C, TEMP_WARN_C, TEMP_CRIT_C, true, 10);
    out("", "]");
    line_end();

    line_begin();
    pad(2);
    if (r->alerts[ALERT_LOW_FUEL].active)
        out(sev_color(fs), "! %s", fs == LEVEL_CRITICAL ? "CRITICAL" : "LOW FUEL");
    else
        out(c(GREEN), "✔ OK");
    cell_sep();
    pad(2);
    if (r->alerts[ALERT_OVERHEAT].active)
        out(sev_color(es), "! %s", es == LEVEL_CRITICAL ? "OVERHEAT" : "HOT");
    else
        out(c(GREEN), "✔ NORMAL");
    line_end();
    box_mid();

    /* row 2: obstacle + seatbelt */
    line_begin(); pad(1); out(c(BOLD), "OBSTACLE"); cell_sep(); pad(1); out(c(BOLD), "SEATBELT"); line_end();
    line_begin();
    pad(1);
    out(sev_color(os), "%5.1f m  ", v->obstacle_m);
    tag(os, r->alerts[ALERT_OBSTACLE].active, "SAFE");
    cell_sep();
    pad(1);
    out(sev_color(bs), "%-9s", v->seatbelt_fastened ? "FASTENED" : "OFF");
    tag(bs, r->alerts[ALERT_SEATBELT].active, "SAFE");
    line_end();
    box_bottom(true);
    gap(GAP_HEALTH);
}

static void draw_assessment(const Vehicle *v, const SafetyReport *r)
{
    static const char *const NAMES[ALERT_COUNT] = {
        "Overspeed", "Fuel", "Engine temperature", "Obstacle", "Seatbelt"
    };
    char detail[48];

    section("SAFETY ASSESSMENT");
    rule_top("RULE-BASED MONITOR", false);
    line_begin();
    pad(2);
    out(g_color && r->overall == LEVEL_CRITICAL ? BLINK RED : sev_color(r->overall), "%s  %s",
        r->overall == LEVEL_SAFE ? "✔" : "!", severity_name(r->overall));
    pad(5);
    out(c(DIM), "SAFETY STATUS: ");
    out(sev_color(r->overall), "%d ALERT%s", r->active_count, r->active_count == 1 ? "" : "S");
    line_end();
    blank_line();

    int shown = 0;
    for (int i = 0; i < ALERT_COUNT; i++) {
        const Alert *a = &r->alerts[i];
        if (!a->active)
            continue;
        switch ((AlertType)i) {
        case ALERT_OVERSPEED: snprintf(detail, sizeof detail, "%.0f km/h  /  limit %.0f", v->speed_kmh, SPEED_WARN_KMH); break;
        case ALERT_LOW_FUEL:  snprintf(detail, sizeof detail, "%.1f%%", v->fuel_pct); break;
        case ALERT_OVERHEAT:  snprintf(detail, sizeof detail, "%.0f°C", v->engine_temp_c); break;
        case ALERT_OBSTACLE:  snprintf(detail, sizeof detail, "%.1f m", v->obstacle_m); break;
        default:              snprintf(detail, sizeof detail, "OFF at %.0f km/h", v->speed_kmh); break;
        }
        line_begin();
        pad(2);
        out(sev_color(a->level), "!  ");
        out(c(BOLD), "%-20s", NAMES[i]);
        out(sev_color(a->level), "%s", detail);
        line_end();
        shown++;
    }
    if (shown == 0) {
        line_begin();
        pad(2);
        out(c(GREEN), "✔  No active alerts - all systems normal");
        line_end();
        shown = 1;
    }
    for (; shown < ALERT_COUNT; shown++) {
        line_begin();
        line_end();
    }
    box_bottom(false);
    gap(GAP_ASSESS);
}

static void draw_ml(const MlPrediction *ml)
{
    static const char *const NAMES[3] = { "LOW ", "MOD ", "HIGH" };

    section("ML DRIVER RISK");
    rule_top("RANDOM FOREST (SEPARATE FROM RULES)", false);
    line_begin();
    pad(2);
    if (ml && ml->valid) {
        char label[16], buf[32];
        snprintf(label, sizeof label, "%s", ml->label);
        for (char *p = label; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        snprintf(buf, sizeof buf, "[ %s ]", label);
        out(g_color && ml->level == ML_HIGH ? BLINK RED : ml_color(ml->level), "%-12s", buf);
        out("", "confidence %3.0f%%", ml->confidence * 100.0f);
        pad(4);
        out(c(DIM), "[P] pause");
    } else if (!ml) {
        out(c(YELLOW), "PAUSED");
        pad(3);
        out(c(DIM), "[P] RESUME");
    } else {
        out(c(DIM), "UNAVAILABLE  (rule-based alerts still active)");
    }
    line_end();
    blank_line();

    if (g_mini) {
        line_begin();
        pad(2);
        for (int k = 0; k < 3; k++) {
            float p = (ml && ml->valid) ? ml->probs[k] : 0.0f;
            int fill = (int)(p * 6.0f + 0.5f);
            out(c(BOLD), "%s ", NAMES[k]);
            for (int b = 0; b < 6; b++)
                out(b < fill ? ml_color((MlLevel)k) : c(DIM), b < fill ? "█" : "░");
            out("", " %3.0f%%", p * 100.0f);
            pad(2);
        }
        line_end();
    }
    for (int k = 0; !g_mini && k < 3; k++) {
        float p = (ml && ml->valid) ? ml->probs[k] : 0.0f;
        line_begin();
        pad(2);
        out(c(BOLD), "%s  ", NAMES[k]);
        int fill = (int)(p * 20.0f + 0.5f);
        for (int b = 0; b < 20; b++)
            out(b < fill ? ml_color((MlLevel)k) : c(DIM), b < fill ? "█" : "░");
        out("", " %3.0f%%", p * 100.0f);
        line_end();
    }
    box_bottom(false);
    gap(GAP_ML);
}

static void draw_trends(const Vehicle *v)
{
    static const char *const blocks[8] = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

    margin();
    pad(2);
    out(c(DIM), "TELEMETRY     SPEED ── ");
    for (int k = 0; k < TREND_N; k++) {
        float s = g_hist_speed[(g_trend_pos + k) % TREND_N];
        if (s < 0.0f) { out(c(DIM), "·"); continue; }
        int lv = (int)(s / SPEED_MAX_KMH * 7.99f);
        out(sev_color(zone(s, SPEED_WARN_KMH, SPEED_CRIT_KMH, true)), "%s",
            blocks[lv < 0 ? 0 : lv > 7 ? 7 : lv]);
    }
    out("", " %.0f", v->speed_kmh);
    eol();

    margin();
    pad(16);
    out(c(DIM), "RISK  ── ");
    for (int k = 0; k < TREND_N; k++) {
        int lv = g_hist_risk[(g_trend_pos + k) % TREND_N];
        if (lv < 0) out(c(DIM), "·");
        else        out(ml_color((MlLevel)lv), "%s", blocks[lv == 0 ? 1 : lv == 1 ? 4 : 7]);
    }
    eol();
    if (!g_mini) {
        margin();
        pad(16);
        out(c(DIM), "▁ low   ▄ mod   █ high");
        eol();
    }
    gap(GAP_TREND);
}

static void draw_controls_mini(void)
{
    margin(); pad(1);
    key("S", "Start", 11); key("R", "Reset", 11); key("W/↑", "Faster", 14); key("X/↓", "Slower", 14); key("B", "Belt", 0);
    eol();
    margin(); pad(1);
    key("F", "Refuel", 11); key("M", "Manual", 11); key("H", "History", 12); key("C", "Clear", 10); key("P", "ML", 7); key("Q", "Quit", 0);
    eol();
    margin(); pad(1);
    out(c(DIM), "SCENES "); key("1", "City", 10); key("2", "Hwy", 9); key("3", "Heat", 10); key("4", "Obst", 10); key("5", "Chaos", 0);
    eol();
    margin(); pad(1);
    out(c(DIM), "FAULTS "); key("T", "Heat+", 11); key("Y", "Heat-", 11); key("O", "Obstacle", 0);
    eol();
}

static void draw_controls(void)
{
    if (g_mini) {
        draw_controls_mini();
        return;
    }
    heavy_rule();
    margin();
    out(c(BOLD), "CONTROLS");
    eol();
    margin(); pad(2); key("S", "Start", 13); key("R", "Reset", 13); key("W/↑", "Accelerate", 19); key("X/↓", "Brake", 0);
    eol();
    margin(); pad(2); key("B", "Belt", 13);  key("F", "Refuel", 13); key("M", "Manual", 19);      key("H", "History", 0);
    eol();
    margin(); pad(2); key("C", "Clear", 13); key("P", "ML", 13);     key("Q", "Quit", 0);
    eol();
    gap(GAP_CTRL1);

    margin();
    out(c(BOLD), "  SCENARIOS");
    pad(5);
    key("1", "City", 12); key("2", "Highway", 15); key("3", "Overheat", 0);
    eol();
    margin(); pad(16); key("4", "Obstacle", 15); key("5", "Chaos", 0);
    eol();
    gap(GAP_CTRL2);

    margin();
    out(c(BOLD), "  FAULT INJECTION");
    pad(2);
    key("T", "Heat+", 12); key("Y", "Heat-", 12); key("O", "Obstacle", 0);
    eol();
}

/* ---- main entry ------------------------------------------------------ */

static void draw_frame(const Vehicle *v, const SafetyReport *r, const MlPrediction *ml,
                       const char *notice, bool redraw)
{
    g_line = 0;
    draw_header(v);
    draw_speed(v, r);
    draw_health(v, r);
    draw_assessment(v, r);
    draw_ml(ml);
    draw_trends(v);
    if (redraw)
        draw_controls();

    if (notice && *notice) {
        margin();
        printf("%s▸ %s%s", c(CYAN), notice, c(RESET));
    }
    eol();
}

void dashboard_render(const Vehicle *v, const SafetyReport *r,
                      const MlPrediction *ml, const char *notice, bool redraw)
{
    g_redraw  = redraw;
    int rows  = redraw ? plat_term_rows() : 0;
    g_mini    = redraw && rows < 48;                    /* short window (or unknown) */
    g_compact = !redraw || rows < 62;
    g_frame   = r->overall == LEVEL_SAFE ? c(CYAN) : sev_color(r->overall);
    {
        int cols = redraw ? plat_term_cols() : 0;          /* centre only on a live terminal */
        g_mx = cols > IW + 4 ? (cols - (IW + 2)) / 2 : 2;
    }

    trend_update(v, ml);

    /* 1. dry run with every optional gap off: how many rows does the frame need? */
    static const GapId PRIORITY[GAP_COUNT] = {
        GAP_TREND, GAP_SPEED, GAP_ASSESS, GAP_HEALTH, GAP_ML, GAP_HDR, GAP_RULE, GAP_CTRL1, GAP_CTRL2
    };
    for (int i = 0; i < GAP_COUNT; i++)
        g_gap_on[i] = false;
    g_maxrows = 0;
    g_mute    = true;
    draw_frame(v, r, ml, notice, redraw);
    int base = g_line;

    /* 2. spend the spare rows: the dashboard/controls gap is mandatory, the rest by priority */
    int spare = redraw ? (rows > 0 ? rows - 1 - base : 0) : 0;
    g_gap_on[GAP_TREND] = true;                         /* always: also separates demo frames */
    spare--;
    for (int i = 1; i < GAP_COUNT && spare > 0; i++, spare--)
        g_gap_on[PRIORITY[i]] = true;

    /* 3. real pass, clipped to the window height so it can never scroll */
    {
        static int last_tier = -1;
        int tier = g_mini ? 0 : g_compact ? 1 : 2;
        if (redraw && tier != last_tier)
            (fputs)("\033[2J", stdout);        /* layout changed: wipe leftovers */
        last_tier = tier;
    }
    g_maxrows = redraw && rows > 0 ? rows - 1 : 0;
    g_mute    = false;
    if (redraw)
        (fputs)("\033[H", stdout);
    draw_frame(v, r, ml, notice, redraw);

    g_mute = false;
    if (redraw)
        (fputs)("\033[J", stdout);
    fflush(stdout);
}
