/**
 * @file    terminal.c
 * @brief   termios based keyboard handling (Linux / POSIX).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include "terminal.h"

static struct termios g_orig;
static bool           g_have_orig = false;
static bool           g_raw       = false;
static bool           g_atexit    = false;
static volatile sig_atomic_t g_interrupted = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_interrupted = 1;
}

bool term_interrupted(void)
{
    return g_interrupted != 0;
}

bool term_raw_enable(void)
{
    if (g_raw)
        return true;
    if (!isatty(STDIN_FILENO))
        return false;

    if (!g_have_orig) {
        if (tcgetattr(STDIN_FILENO, &g_orig) != 0)
            return false;
        g_have_orig = true;
    }

    struct termios raw = g_orig;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);   /* ISIG kept: Ctrl+C works */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return false;
    g_raw = true;

    if (!g_atexit) {
        atexit(term_raw_disable);
        signal(SIGINT,  on_signal);
        signal(SIGTERM, on_signal);
        g_atexit = true;
    }
    return true;
}

void term_raw_disable(void)
{
    if (g_raw && g_have_orig)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig);
    g_raw = false;
}

int term_read_key(int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }

    int rc = select(STDIN_FILENO + 1, &fds, NULL, NULL, ptv);
    if (rc <= 0)
        return -1;                       /* timeout or interrupted */

    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 0)
        return 'q';                      /* EOF on stdin: quit cleanly */
    return n < 0 ? -1 : (int)ch;
}

bool term_prompt(const char *prompt, char *buf, int size)
{
    bool was_raw = g_raw;
    if (was_raw)
        term_raw_disable();

    fputs(prompt, stdout);
    fflush(stdout);
    bool ok = fgets(buf, size, stdin) != NULL;
    if (ok)
        buf[strcspn(buf, "\r\n")] = '\0';

    if (was_raw)
        term_raw_enable();
    return ok;
}

void term_clear(void)
{
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}
