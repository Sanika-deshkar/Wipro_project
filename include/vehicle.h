/**
 * @file    vehicle.h
 * @brief   Vehicle state model shared by every module.
 */
#ifndef VEHICLE_H
#define VEHICLE_H

#include <stdbool.h>

typedef enum {
    SIM_STOPPED = 0,
    SIM_RUNNING = 1
} SimState;

/** Snapshot of all sensor readings plus simulation bookkeeping. */
typedef struct {
    float         speed_kmh;          /**< current speed                    */
    float         fuel_pct;           /**< fuel level, 0..100               */
    float         engine_temp_c;      /**< engine coolant temperature       */
    float         obstacle_m;         /**< distance to nearest obstacle     */
    bool          seatbelt_fastened;  /**< driver seatbelt status           */

    float         target_speed_kmh;   /**< driver's requested speed (sim)   */
    float         temp_bias_c;        /**< injected cooling fault (sim), +deg C */
    float         odometer_km;        /**< distance travelled this run      */
    unsigned long ticks;              /**< elapsed simulation seconds       */
    SimState      state;              /**< running / stopped                */
} Vehicle;

/** Restore factory defaults (full tank, cold engine, stationary). */
void vehicle_reset(Vehicle *v);

/** Force every reading into its physically valid range. */
void vehicle_clamp(Vehicle *v);

#endif /* VEHICLE_H */
