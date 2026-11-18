/**
 * @file    simulation.h
 * @brief   Physics-lite vehicle simulator (one call = one second).
 */
#ifndef SIMULATION_H
#define SIMULATION_H

#include "vehicle.h"

/** Seed the random generator (0 = use current time). */
void sim_seed(unsigned int seed);

/** Begin / halt / reset the simulation. */
void sim_start(Vehicle *v);
void sim_stop(Vehicle *v);
void sim_reset(Vehicle *v);

/** Advance the world by one tick (speed, fuel, temperature, obstacle). */
void sim_step(Vehicle *v);

/** Driver inputs. */
void sim_accelerate(Vehicle *v);
void sim_brake(Vehicle *v);
void sim_toggle_seatbelt(Vehicle *v);
void sim_refuel(Vehicle *v);

/** Scripted 45-second demo drive; call once per tick with the tick index. */
void sim_demo_script(Vehicle *v, int t);

#endif /* SIMULATION_H */
