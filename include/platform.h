/**
 * @file    platform.h
 * @brief   Tiny portability layer so the same C code runs on Linux, macOS and Windows.
 *
 * Everything operating-system specific that is not already hidden behind
 * standard C lives behind these few functions (see platform.c).
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

/** One-time start-up: Winsock + UTF-8 / ANSI console on Windows, no-op elsewhere. */
void plat_init(void);

/** Monotonic clock in milliseconds (relative to an arbitrary start). */
long plat_now_ms(void);

/** Create a directory; silently succeeds if it already exists. */
void plat_mkdir(const char *path);

/** Height of the terminal window in rows (0 if unknown / not a terminal). */
int  plat_term_rows(void);

/** Width of the terminal window in columns (0 if unknown / not a terminal). */
int  plat_term_cols(void);

bool plat_stdout_is_tty(void);
bool plat_stdin_is_tty(void);

/** Open @p url in the user's default browser (Windows, macOS, Linux, WSL). */
void plat_open_browser(const char *url);

#endif /* PLATFORM_H */
