/**
 * @file    terminal.h
 * @brief   Raw (non-blocking) keyboard input for Linux terminals (termios).
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>

/** Put the terminal in raw, no-echo mode. Safe to call repeatedly. */
bool term_raw_enable(void);

/** Restore the original terminal settings. Registered with atexit(). */
void term_raw_disable(void);

/**
 * Wait up to @p timeout_ms for a key.
 * @return key code, or -1 on timeout. A negative timeout blocks forever.
 */
int  term_read_key(int timeout_ms);

/** Prompt for a line in normal (cooked) mode, then return to raw mode. */
bool term_prompt(const char *prompt, char *buf, int size);

/** True once SIGINT/SIGTERM has been received (set by the handler). */
bool term_interrupted(void);

/** Clear the whole screen and home the cursor. */
void term_clear(void);

#endif /* TERMINAL_H */
