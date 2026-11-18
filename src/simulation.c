/**
 * @file    simulation.c
 * @brief   Simple vehicle physics used to drive the dashboard.
 *
 * Model (per one-second tick):
 *   speed      ramps toward the driver's target (limited accel / braking)
 *   fuel       burns at an idle rate + a rate proportional to speed
 *   engine     temperature relaxes toward 60 + 0.45 * speed (deg C)
 *   obstacle   random walk, closing in as speed increases; respawns far away
 *              once passed
 * Fuel consumption is deliberately accelerated so low-fuel behaviour can be
 * demonstrated in about a minute of running time.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "simulation.h"
#include "config.h"

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

void sim_seed(unsigned int seed)
{
    srand(seed ? seed : (unsigned int)time(NULL));
}

void sim_start(Vehicle *v)  { v->state = SIM_RUNNING; }
void sim_stop(Vehicle *v)   { v->state = SIM_STOPPED; }
void sim_reset(Vehicle *v)  { vehicle_reset(v); }

void sim_accelerate(Vehicle *v)
{
    v->target_speed_kmh += SIM_SPEED_STEP_KMH;
    vehicle_clamp(v);
}

void sim_brake(Vehicle *v)
{
    v->target_speed_kmh -= SIM_SPEED_STEP_KMH;
    vehicle_clamp(v);
}

void sim_toggle_seatbelt(Vehicle *v)
{
    v->seatbelt_fastened = !v->seatbelt_fastened;
}

void sim_refuel(Vehicle *v)
{
    v->fuel_pct = 100.0f;
}

void sim_step(Vehicle *v)
{
    if (v->state != SIM_RUNNING)
        return;

    v->ticks++;

    /* --- speed: engine dies when the tank is empty -------------------- */
    float target = (v->fuel_pct <= 0.0f) ? 0.0f : v->target_speed_kmh;
    float diff   = target - v->speed_kmh;

    bool cruising = (diff < 1.0f && diff > -1.0f);   /* at target: add jitter */

    if (diff > SIM_ACCEL_PER_TICK)       diff = SIM_ACCEL_PER_TICK;
    else if (diff < -SIM_BRAKE_PER_TICK) diff = -SIM_BRAKE_PER_TICK;
    v->speed_kmh += diff;
    if (cruising && v->speed_kmh > 1.0f)
        v->speed_kmh += frand(-0.5f, 0.5f);

    /* --- fuel ---------------------------------------------------------- */
    if (v->fuel_pct > 0.0f)
        v->fuel_pct -= SIM_FUEL_IDLE_PCT + v->speed_kmh * SIM_FUEL_PER_KMH;

    /* --- engine temperature ------------------------------------------- */
    float temp_target = 60.0f + 0.45f * v->speed_kmh;
    v->engine_temp_c += (temp_target - v->engine_temp_c) * 0.10f
                        + frand(-0.4f, 0.4f);

    /* --- obstacle ------------------------------------------------------ */
    v->obstacle_m += frand(-6.0f, 6.0f) - v->speed_kmh * 0.05f;
    if (v->obstacle_m < 1.0f)
        v->obstacle_m = frand(100.0f, OBSTACLE_MAX_M);

    /* --- odometer ------------------------------------------------------ */
    v->odometer_km += v->speed_kmh / 3600.0f;

    vehicle_clamp(v);
}

void sim_demo_script(Vehicle *v, int t)
{
    switch (t) {
    case 0:  v->target_speed_kmh = 50;  break;
    case 2:  v->seatbelt_fastened = true; break;
    case 8:  v->target_speed_kmh = 90;  break;    /* overspeed warning  */
    case 15: v->target_speed_kmh = 130; break;    /* critical overspeed */
    case 24: v->seatbelt_fastened = false; break; /* unbelted at speed  */
    case 28: v->target_speed_kmh = 20;  break;
    }
}
