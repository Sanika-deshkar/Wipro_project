/**
 * @file    monitor.c
 * @brief   Safety rule engine.
 *
 * Each sensor has its own small checker. All checkers run on every pass, so
 * several alerts can be active simultaneously (e.g. overspeed + unbelted +
 * obstacle). The overall status is the worst individual severity.
 */
#include <stdio.h>
#include <string.h>

#include "monitor.h"
#include "config.h"

static void raise(SafetyReport *r, AlertType t, Severity s, const char *fmt,
                  float a, float b)
{
    Alert *al = &r->alerts[t];
    al->active = true;
    al->level  = s;
    snprintf(al->message, ALERT_MSG_LEN, fmt, a, b);
}

static void check_speed(const Vehicle *v, SafetyReport *r)
{
    if (v->speed_kmh > SPEED_CRIT_KMH)
        raise(r, ALERT_OVERSPEED, LEVEL_CRITICAL,
              "OVERSPEED %.0f km/h (limit %.0f)", v->speed_kmh, SPEED_WARN_KMH);
    else if (v->speed_kmh > SPEED_WARN_KMH)
        raise(r, ALERT_OVERSPEED, LEVEL_WARNING,
              "Overspeed %.0f km/h (limit %.0f)", v->speed_kmh, SPEED_WARN_KMH);
}

static void check_fuel(const Vehicle *v, SafetyReport *r)
{
    if (v->fuel_pct < FUEL_CRIT_PCT)
        raise(r, ALERT_LOW_FUEL, LEVEL_CRITICAL,
              "FUEL CRITICAL %.1f%% (min %.0f%%)", v->fuel_pct, FUEL_CRIT_PCT);
    else if (v->fuel_pct < FUEL_WARN_PCT)
        raise(r, ALERT_LOW_FUEL, LEVEL_WARNING,
              "Low fuel %.1f%% (min %.0f%%)", v->fuel_pct, FUEL_WARN_PCT);
}

static void check_temperature(const Vehicle *v, SafetyReport *r)
{
    if (v->engine_temp_c > TEMP_CRIT_C)
        raise(r, ALERT_OVERHEAT, LEVEL_CRITICAL,
              "ENGINE OVERHEAT %.0f C (max %.0f)", v->engine_temp_c, TEMP_CRIT_C);
    else if (v->engine_temp_c > TEMP_WARN_C)
        raise(r, ALERT_OVERHEAT, LEVEL_WARNING,
              "Engine hot %.0f C (limit %.0f)", v->engine_temp_c, TEMP_WARN_C);
}

static void check_obstacle(const Vehicle *v, SafetyReport *r)
{
    if (v->obstacle_m < OBST_CRIT_M)
        raise(r, ALERT_OBSTACLE, LEVEL_CRITICAL,
              "COLLISION RISK: obstacle %.1f m (<%.0f)", v->obstacle_m, OBST_CRIT_M);
    else if (v->obstacle_m < OBST_WARN_M)
        raise(r, ALERT_OBSTACLE, LEVEL_WARNING,
              "Obstacle close: %.1f m (<%.0f)", v->obstacle_m, OBST_WARN_M);
}

static void check_seatbelt(const Vehicle *v, SafetyReport *r)
{
    if (v->seatbelt_fastened || v->speed_kmh <= SPEED_BELT_REQ_KMH)
        return;                               /* belted, or not moving */

    if (v->speed_kmh > SPEED_BELT_CRIT_KMH)
        raise(r, ALERT_SEATBELT, LEVEL_CRITICAL,
              "SEATBELT OFF at %.0f km/h (>%.0f)", v->speed_kmh, SPEED_BELT_CRIT_KMH);
    else
        raise(r, ALERT_SEATBELT, LEVEL_WARNING,
              "Seatbelt not fastened (%.0f km/h)", v->speed_kmh, 0.0f);
}

void monitor_evaluate(const Vehicle *v, SafetyReport *out)
{
    memset(out, 0, sizeof *out);

    check_speed(v, out);
    check_fuel(v, out);
    check_temperature(v, out);
    check_obstacle(v, out);
    check_seatbelt(v, out);

    for (int i = 0; i < ALERT_COUNT; i++) {
        if (!out->alerts[i].active)
            continue;
        out->active_count++;
        if (out->alerts[i].level > out->overall)
            out->overall = out->alerts[i].level;
    }
}

const char *severity_name(Severity s)
{
    switch (s) {
    case LEVEL_SAFE:     return "SAFE";
    case LEVEL_WARNING:  return "WARNING";
    case LEVEL_CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

const char *alert_name(AlertType t)
{
    static const char *const names[ALERT_COUNT] = {
        "OVERSPEED", "LOW_FUEL", "OVERHEAT", "OBSTACLE", "SEATBELT"
    };
    return (t >= 0 && t < ALERT_COUNT) ? names[t] : "UNKNOWN";
}
