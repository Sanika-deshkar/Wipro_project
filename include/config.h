/**
 * @file    config.h
 * @brief   Central configuration: thresholds, limits and file paths.
 *
 * Every "magic number" used by the monitoring logic lives here so that the
 * safety rules can be tuned in one place without touching any other module.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- Application identity ------------------------------------------- */
#define APP_NAME     "Smart Vehicle Safety Monitoring & Dashboard System"
#define APP_VERSION  "1.0.0"

/* ---- Physical limits of the simulated vehicle ----------------------- */
#define SPEED_MAX_KMH        220.0f
#define TEMP_MIN_C           -20.0f
#define TEMP_MAX_C           150.0f
#define OBSTACLE_MAX_M       200.0f   /* sensor range */
#define AMBIENT_TEMP_C       25.0f

/* ---- Speed thresholds (km/h) ---------------------------------------- */
#define SPEED_WARN_KMH       80.0f    /* above this  -> WARNING  */
#define SPEED_CRIT_KMH       120.0f   /* above this  -> CRITICAL */
#define SPEED_BELT_REQ_KMH   0.0f     /* belt required when moving faster */
#define SPEED_BELT_CRIT_KMH  40.0f    /* unbelted above this -> CRITICAL */

/* ---- Fuel thresholds (percent) -------------------------------------- */
#define FUEL_WARN_PCT        15.0f    /* below this  -> WARNING  */
#define FUEL_CRIT_PCT        5.0f     /* below this  -> CRITICAL */

/* ---- Engine temperature thresholds (deg C) -------------------------- */
#define TEMP_WARN_C          100.0f   /* above this  -> WARNING  */
#define TEMP_CRIT_C          110.0f   /* above this  -> CRITICAL */

/* ---- Obstacle distance thresholds (metres) -------------------------- */
#define OBST_WARN_M          10.0f    /* closer than -> WARNING  */
#define OBST_CRIT_M          3.0f     /* closer than -> CRITICAL */

/* ---- Simulation tuning ---------------------------------------------- */
#define SIM_TICK_MS          1000     /* one simulated step per second   */
#define SIM_SPEED_STEP_KMH   10.0f    /* change per accelerate/brake key */
#define SIM_ACCEL_PER_TICK   8.0f     /* max speed gain per tick         */
#define SIM_BRAKE_PER_TICK   12.0f    /* max speed loss per tick         */
#define SIM_FUEL_IDLE_PCT    0.05f    /* fuel burnt per tick at idle     */
#define SIM_FUEL_PER_KMH     0.010f   /* extra fuel per tick per km/h    */

/* ---- Alert history file --------------------------------------------- */
#define LOG_DIR              "logs"
#define LOG_FILE_DEFAULT     "logs/alert_history.log"
#define LOG_VIEW_LINES       15

#endif /* CONFIG_H */
