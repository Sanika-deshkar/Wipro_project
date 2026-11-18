/**
 * @file    test_monitor.c
 * @brief   Unit tests for the rule engine, simulator and logger.
 *          Build & run with:  make test
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "vehicle.h"
#include "monitor.h"
#include "simulation.h"
#include "logger.h"

static int g_run = 0, g_fail = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        g_run++;                                                       \
        if (cond) printf("  [PASS] %s\n", name);                       \
        else { g_fail++; printf("  [FAIL] %s  (line %d)\n", name, __LINE__); } \
    } while (0)

/** A healthy, belted, moving vehicle: should produce zero alerts. */
static Vehicle baseline(void)
{
    Vehicle v;
    vehicle_reset(&v);
    v.speed_kmh = 60; v.fuel_pct = 70; v.engine_temp_c = 85;
    v.obstacle_m = 80; v.seatbelt_fastened = true;
    return v;
}

static void test_safe(void)
{
    puts("Safe state");
    Vehicle v = baseline(); SafetyReport r;
    monitor_evaluate(&v, &r);
    CHECK(r.overall == LEVEL_SAFE && r.active_count == 0, "no alerts on healthy vehicle");
}

static void test_speed(void)
{
    puts("Speed monitoring");
    Vehicle v = baseline(); SafetyReport r;
    v.speed_kmh = SPEED_WARN_KMH;        monitor_evaluate(&v, &r);
    CHECK(!r.alerts[ALERT_OVERSPEED].active, "exactly at limit is not overspeed");
    v.speed_kmh = SPEED_WARN_KMH + 1;    monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_OVERSPEED].level == LEVEL_WARNING, "just above limit -> WARNING");
    v.speed_kmh = SPEED_CRIT_KMH + 1;    monitor_evaluate(&v, &r);
    CHECK(r.overall == LEVEL_CRITICAL, "far above limit -> CRITICAL");
}

static void test_fuel(void)
{
    puts("Fuel monitoring");
    Vehicle v = baseline(); SafetyReport r;
    v.fuel_pct = 14.9f;  monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_LOW_FUEL].level == LEVEL_WARNING, "<15% -> WARNING");
    v.fuel_pct = 4.9f;   monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_LOW_FUEL].level == LEVEL_CRITICAL, "<5% -> CRITICAL");
    v.fuel_pct = 15.0f;  monitor_evaluate(&v, &r);
    CHECK(!r.alerts[ALERT_LOW_FUEL].active, "15% exactly -> no alert");
}

static void test_temperature(void)
{
    puts("Temperature monitoring");
    Vehicle v = baseline(); SafetyReport r;
    v.engine_temp_c = 105; monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_OVERHEAT].level == LEVEL_WARNING, "105 C -> WARNING");
    v.engine_temp_c = 115; monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_OVERHEAT].level == LEVEL_CRITICAL, "115 C -> CRITICAL");
}

static void test_obstacle(void)
{
    puts("Obstacle monitoring");
    Vehicle v = baseline(); SafetyReport r;
    v.obstacle_m = 8;  monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_OBSTACLE].level == LEVEL_WARNING, "8 m -> WARNING");
    v.obstacle_m = 2;  monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_OBSTACLE].level == LEVEL_CRITICAL, "2 m -> CRITICAL");
}

static void test_seatbelt(void)
{
    puts("Seatbelt monitoring");
    Vehicle v = baseline(); SafetyReport r;
    v.seatbelt_fastened = false; v.speed_kmh = 0;
    monitor_evaluate(&v, &r);
    CHECK(!r.alerts[ALERT_SEATBELT].active, "unbelted while parked -> no alert");
    v.speed_kmh = 20; monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_SEATBELT].level == LEVEL_WARNING, "unbelted & slow -> WARNING");
    v.speed_kmh = 60; monitor_evaluate(&v, &r);
    CHECK(r.alerts[ALERT_SEATBELT].level == LEVEL_CRITICAL, "unbelted & fast -> CRITICAL");
}

static void test_multiple(void)
{
    puts("Simultaneous alerts");
    Vehicle v = baseline(); SafetyReport r;
    v.speed_kmh = 130; v.fuel_pct = 10; v.engine_temp_c = 105;
    v.obstacle_m = 5;  v.seatbelt_fastened = false;
    monitor_evaluate(&v, &r);
    CHECK(r.active_count == 5, "all five alerts active together");
    CHECK(r.overall == LEVEL_CRITICAL, "overall = worst severity");
}

static void test_simulation(void)
{
    puts("Simulation");
    Vehicle v; vehicle_reset(&v); sim_seed(42);
    sim_step(&v);
    CHECK(v.ticks == 0, "step ignored while stopped");

    sim_start(&v); sim_accelerate(&v);
    for (int i = 0; i < 20; i++) sim_step(&v);
    CHECK(v.speed_kmh > 5 && v.speed_kmh < 15, "speed converges near 10 km/h target");
    CHECK(v.fuel_pct < 100.0f, "fuel is consumed");
    CHECK(v.ticks == 20, "tick counter advances");

    v.fuel_pct = 0; v.target_speed_kmh = 100;
    for (int i = 0; i < 30; i++) sim_step(&v);
    CHECK(v.speed_kmh < 1.0f, "vehicle stops when out of fuel");

    sim_reset(&v);
    CHECK(v.fuel_pct == 100.0f && v.ticks == 0 && v.state == SIM_STOPPED, "reset restores defaults");
}

static void test_logger(void)
{
    puts("Alert history file");
    const char *path = "build/test_history.log";
    CHECK(logger_init(path) == 0, "log file opens");
    logger_clear();

    Vehicle v = baseline(); SafetyReport prev, cur;
    memset(&prev, 0, sizeof prev);

    v.speed_kmh = 90;
    monitor_evaluate(&v, &cur);
    CHECK(logger_log_changes(&prev, &cur, &v) == 1, "new alert is logged");
    prev = cur;
    CHECK(logger_log_changes(&prev, &cur, &v) == 0, "unchanged alert is not re-logged");

    v.speed_kmh = 130;
    monitor_evaluate(&v, &cur);
    CHECK(logger_log_changes(&prev, &cur, &v) == 1, "escalation is logged");

    FILE *sink = fopen("build/test_sink.txt", "w");
    int n = logger_show_recent(sink, 10);
    fclose(sink);
    CHECK(n == 2, "history returns both records");
}

int main(void)
{
    puts("=== Smart Vehicle Safety System - unit tests ===\n");
    test_safe(); test_speed(); test_fuel(); test_temperature();
    test_obstacle(); test_seatbelt(); test_multiple();
    test_simulation(); test_logger();

    printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
