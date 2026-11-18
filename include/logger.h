/**
 * @file    logger.h
 * @brief   Persistent alert history (plain-text file).
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include "monitor.h"
#include "vehicle.h"

/** Select log file and create its directory. Returns 0 on success. */
int  logger_init(const char *path);

/** Append one record. Returns 0 on success, -1 on I/O failure. */
int  logger_write(AlertType t, Severity s, const char *msg, const Vehicle *v);

/**
 * Log only alerts that are NEW or have ESCALATED since @p prev.
 * This prevents the file being flooded with the same alert every second.
 * Returns the number of records written.
 */
int  logger_log_changes(const SafetyReport *prev, const SafetyReport *cur,
                        const Vehicle *v);

/** Print the last @p n records to @p out. Returns lines printed, -1 on error. */
int  logger_show_recent(FILE *out, int n);

/** Last @p n records as one malloc'd string ("" if none). Caller frees. */
char *logger_recent_text(int n);

/** Truncate the history file. Returns 0 on success. */
int  logger_clear(void);

/** Path currently in use. */
const char *logger_path(void);

#endif /* LOGGER_H */
