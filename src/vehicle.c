/**
 * @file    vehicle.c
 * @brief   Vehicle state helpers.
 */
#include "vehicle.h"
#include "config.h"

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

void vehicle_reset(Vehicle *v)
{
    v->speed_kmh         = 0.0f;
    v->fuel_pct          = 100.0f;
    v->engine_temp_c     = AMBIENT_TEMP_C;
    v->obstacle_m        = OBSTACLE_MAX_M;
    v->seatbelt_fastened = false;
    v->target_speed_kmh  = 0.0f;
    v->temp_bias_c       = 0.0f;
    v->odometer_km       = 0.0f;
    v->ticks             = 0;
    v->state             = SIM_STOPPED;
}

void vehicle_clamp(Vehicle *v)
{
    v->speed_kmh        = clampf(v->speed_kmh, 0.0f, SPEED_MAX_KMH);
    v->target_speed_kmh = clampf(v->target_speed_kmh, 0.0f, SPEED_MAX_KMH);
    v->fuel_pct         = clampf(v->fuel_pct, 0.0f, 100.0f);
    v->engine_temp_c    = clampf(v->engine_temp_c, TEMP_MIN_C, TEMP_MAX_C);
    v->obstacle_m       = clampf(v->obstacle_m, 0.0f, OBSTACLE_MAX_M);
}
