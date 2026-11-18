/**
 * @file    logger.c
 * @brief   Alert history persisted to a plain-text file.
 *
 * Record format (one per line):
 *   [YYYY-MM-DD HH:MM:SS] [SEVERITY] TYPE: message | spd=.. fuel=.. temp=.. obst=.. belt=..
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "logger.h"
#include "config.h"

#define LINE_MAX_LEN 320

static char g_path[256] = LOG_FILE_DEFAULT;

/** Create the directory part of g_path (single level is enough here). */
static void ensure_directory(void)
{
    char dir[256];
    strncpy(dir, g_path, sizeof dir - 1);
    dir[sizeof dir - 1] = '\0';

    char *slash = strrchr(dir, '/');
    if (!slash)
        return;                               /* file in current directory */
    *slash = '\0';
    if (dir[0] != '\0')
        mkdir(dir, 0755);                     /* EEXIST is fine */
}

int logger_init(const char *path)
{
    if (path && *path) {
        strncpy(g_path, path, sizeof g_path - 1);
        g_path[sizeof g_path - 1] = '\0';
    }
    ensure_directory();

    FILE *f = fopen(g_path, "a");             /* verify we can write */
    if (!f)
        return -1;
    fclose(f);
    return 0;
}

const char *logger_path(void)
{
    return g_path;
}

int logger_write(AlertType t, Severity s, const char *msg, const Vehicle *v)
{
    FILE *f = fopen(g_path, "a");
    if (!f)
        return -1;

    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(f, "[%s] [%-8s] %-9s: %s | spd=%.0f fuel=%.1f temp=%.0f obst=%.1f belt=%s\n",
            stamp, severity_name(s), alert_name(t), msg,
            v->speed_kmh, v->fuel_pct, v->engine_temp_c, v->obstacle_m,
            v->seatbelt_fastened ? "ON" : "OFF");

    return fclose(f) == 0 ? 0 : -1;
}

int logger_log_changes(const SafetyReport *prev, const SafetyReport *cur,
                       const Vehicle *v)
{
    int written = 0;

    for (int i = 0; i < ALERT_COUNT; i++) {
        const Alert *c = &cur->alerts[i];
        const Alert *p = &prev->alerts[i];

        if (!c->active)
            continue;
        bool is_new       = !p->active;
        bool is_escalated = p->active && c->level > p->level;

        if ((is_new || is_escalated) &&
            logger_write((AlertType)i, c->level, c->message, v) == 0)
            written++;
    }
    return written;
}

int logger_show_recent(FILE *out, int n)
{
    FILE *f = fopen(g_path, "r");
    if (!f)
        return (errno == ENOENT) ? 0 : -1;

    char (*ring)[LINE_MAX_LEN] = calloc((size_t)n, sizeof *ring);
    if (!ring) {
        fclose(f);
        return -1;
    }

    long total = 0;
    char line[LINE_MAX_LEN];
    while (fgets(line, sizeof line, f)) {
        strcpy(ring[total % n], line);
        total++;
    }
    fclose(f);

    long first = total > n ? total - n : 0;
    for (long i = first; i < total; i++)
        fputs(ring[i % n], out);

    free(ring);
    return (int)(total - first);
}

int logger_clear(void)
{
    FILE *f = fopen(g_path, "w");
    if (!f)
        return -1;
    return fclose(f) == 0 ? 0 : -1;
}
