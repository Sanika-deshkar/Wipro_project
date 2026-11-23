/**
 * @file    platform.c
 * @brief   Windows / POSIX implementations of the platform layer.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifdef _WIN32
/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <shellapi.h>

/* very old MinGW headers lack these */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#define svsm_tick_ms() ((ULONGLONG)GetTickCount())   /* wraps after 49 days; fine for a UI clock */

void plat_init(void)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SetConsoleOutputCP(CP_UTF8);                       /* box-drawing characters */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode))                    /* ANSI colours / cursor codes */
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

long plat_now_ms(void)
{
    static ULONGLONG base = 0;
    ULONGLONG now = svsm_tick_ms();
    if (base == 0)
        base = now;
    return (long)(now - base);
}

void plat_mkdir(const char *path)         { _mkdir(path); }
bool plat_stdout_is_tty(void)             { return _isatty(_fileno(stdout)) != 0; }
bool plat_stdin_is_tty(void)              { return _isatty(_fileno(stdin)) != 0; }

int plat_term_rows(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return 0;
    return info.srWindow.Bottom - info.srWindow.Top + 1;
}

int plat_term_cols(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return 0;
    return info.srWindow.Right - info.srWindow.Left + 1;
}

void plat_open_browser(const char *url)
{
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

#else
/* ------------------------------------------------------------------ */
/* Linux / macOS / WSL                                                 */
/* ------------------------------------------------------------------ */
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

void plat_init(void) { }

long plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void plat_mkdir(const char *path)         { mkdir(path, 0755); }
bool plat_stdout_is_tty(void)             { return isatty(STDOUT_FILENO) != 0; }
bool plat_stdin_is_tty(void)              { return isatty(STDIN_FILENO) != 0; }

int plat_term_rows(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
        return 0;
    return ws.ws_row;
}

int plat_term_cols(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
        return 0;
    return ws.ws_col;
}

/** True when running inside WSL (the browser then lives on the Windows side). */
static bool running_in_wsl(void)
{
    FILE *f = fopen("/proc/version", "r");
    if (!f)
        return false;
    char buf[256] = "";
    if (!fgets(buf, sizeof buf, f))
        buf[0] = '\0';
    fclose(f);
    for (char *p = buf; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return strstr(buf, "microsoft") != NULL;
}

void plat_open_browser(const char *url)
{
    char cmd[320];
#ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1 &", url);
#else
    if (running_in_wsl())
        snprintf(cmd, sizeof cmd, "cmd.exe /c start \"\" \"%s\" >/dev/null 2>&1 &", url);
    else
        snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", url);
#endif
    if (system(cmd) != 0)
        fprintf(stderr, "Could not open a browser automatically. Open %s yourself.\n", url);
}
#endif
