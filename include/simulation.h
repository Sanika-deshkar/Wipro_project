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

/** Number of one-key scenario presets and their names (index 0..SIM_SCENARIOS-1). */
#define SIM_SCENARIOS 5
const char *sim_scenario_name(int id);

/** Jump to a preset situation (keeps the simulation state running/stopped). */
void sim_scenario(Vehicle *v, int id);

/** Fault injection: raise/lower the engine-cooling fault, pop an obstacle in front. */
void sim_heat(Vehicle *v, float delta_c);
void sim_obstacle_ahead(Vehicle *v, float metres);

/** Scripted 45-second demo drive; call once per tick with the tick index. */
void sim_demo_script(Vehicle *v, int t);

#endif /* SIMULATION_H */
