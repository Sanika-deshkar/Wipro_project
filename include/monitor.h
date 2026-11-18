/**
 * @file    monitor.h
 * @brief   Safety rule engine: turns sensor readings into alerts.
 */
#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include "vehicle.h"

typedef enum {
    LEVEL_SAFE = 0,
    LEVEL_WARNING = 1,
    LEVEL_CRITICAL = 2
} Severity;

typedef enum {
    ALERT_OVERSPEED = 0,
    ALERT_LOW_FUEL,
    ALERT_OVERHEAT,
    ALERT_OBSTACLE,
    ALERT_SEATBELT,
    ALERT_COUNT              /**< number of alert types (keep last) */
} AlertType;

#define ALERT_MSG_LEN 96

typedef struct {
    bool     active;
    Severity level;
    char     message[ALERT_MSG_LEN];
} Alert;

/** Result of one evaluation pass. Several alerts may be active at once. */
typedef struct {
    Alert    alerts[ALERT_COUNT];   /**< indexed by AlertType             */
    int      active_count;          /**< how many alerts are active       */
    Severity overall;               /**< worst level among active alerts  */
} SafetyReport;

/** Evaluate every monitor and fill @p out. */
void monitor_evaluate(const Vehicle *v, SafetyReport *out);

const char *severity_name(Severity s);
const char *alert_name(AlertType t);

#endif /* MONITOR_H */
