/**
 * @file    dashboard.h
 * @brief   Terminal (ANSI) rendering of the digital dashboard.
 */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdbool.h>
#include "vehicle.h"
#include "monitor.h"
#include "ml.h"

/**
 * Draw the complete dashboard.
 * @param redraw  true  -> cursor-home and overwrite in place (interactive)
 *                false -> plain append, no cursor control (demo / piping)
 * @param ml      ML prediction shown in its own panel (NULL or !valid = "unavailable");
 *                it is independent of the rule-based report @p r
 * @param notice  one-line message shown under the panel (may be NULL)
 */
void dashboard_render(const Vehicle *v, const SafetyReport *r,
                      const MlPrediction *ml, const char *notice, bool redraw);

/** Enable/disable colour escape codes (disabled when output is not a TTY). */
void dashboard_set_color(bool on);

#endif /* DASHBOARD_H */
