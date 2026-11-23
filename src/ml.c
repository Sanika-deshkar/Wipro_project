/**
 * @file    ml.c
 * @brief   C <-> Python bridge (child process + two pipes). See ml.h.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ml.h"
#include "config.h"

#define START_TIMEOUT_MS    20000     /* importing scikit-learn can be slow */
#define REPLY_TIMEOUT_MS     2000
#define LINE_MAX_LEN          256

static char g_err[160] = "";
static bool g_up = false;
static bool g_io = false;             /* child process currently exists */
static char g_last_req[LINE_MAX_LEN] = "";
static MlPrediction g_last;           /* cache: same inputs -> same answer */

static void set_err(const char *fmt, const char *a)
{
    snprintf(g_err, sizeof g_err, fmt, a ? a : "");
}

const char *ml_last_error(void) { return g_err; }
bool ml_available(void)         { return g_up; }

const char *ml_level_name(MlLevel l)
{
    return l == ML_LOW ? "Low" : l == ML_MODERATE ? "Moderate" : "High";
}

/* ------------------------------------------------------------------ */
/* pure helpers                                                        */
/* ------------------------------------------------------------------ */

static bool in_range(float x, float lo, float hi) { return isfinite(x) && x >= lo && x <= hi; }

bool ml_format_request(const Vehicle *v, char *buf, int size)
{
    if (!v || !in_range(v->speed_kmh, 0.0f, SPEED_MAX_KMH)
           || !in_range(v->engine_temp_c, TEMP_MIN_C, TEMP_MAX_C)
           || !in_range(v->fuel_pct, 0.0f, 100.0f)
           || !in_range(v->obstacle_m, 0.0f, OBSTACLE_MAX_M))
        return false;
    int n = snprintf(buf, (size_t)size, "%.2f %.2f %.2f %.2f %d\n",
                     v->speed_kmh, v->engine_temp_c, v->fuel_pct, v->obstacle_m,
                     v->seatbelt_fastened ? 1 : 0);
    return n > 0 && n < size;
}

bool ml_parse_reply(const char *line, MlPrediction *out)
{
    char label[16];
    float conf, p[3];
    memset(out, 0, sizeof *out);
    if (!line || sscanf(line, "OK %15s %f %f %f %f", label, &conf, &p[0], &p[1], &p[2]) != 5)
        return false;
    if (!strcmp(label, "Low"))           out->level = ML_LOW;
    else if (!strcmp(label, "Moderate")) out->level = ML_MODERATE;
    else if (!strcmp(label, "High"))     out->level = ML_HIGH;
    else return false;
    if (!(conf >= 0.0f && conf <= 1.0f))
        return false;
    snprintf(out->label, sizeof out->label, "%.11s", label);
    out->confidence = conf;
    memcpy(out->probs, p, sizeof p);
    out->valid = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* process + pipe plumbing (OS specific)                               */
/* ------------------------------------------------------------------ */

static bool io_start(const char *py, const char *script);
static void io_stop(void);
static bool io_write(const char *s);
static bool io_read_line(char *buf, int size, int timeout_ms);

#ifdef _WIN32
#include <windows.h>

static HANDLE h_proc, h_in, h_out;       /* h_in: our write end, h_out: our read end */

static bool io_start(const char *py, const char *script)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE child_in_r, child_in_w, child_out_r, child_out_w;
    if (!CreatePipe(&child_in_r, &child_in_w, &sa, 0) || !CreatePipe(&child_out_r, &child_out_w, &sa, 0)) {
        set_err("cannot create pipes", NULL);
        return false;
    }
    SetHandleInformation(child_in_w, HANDLE_FLAG_INHERIT, 0);    /* parent-only ends */
    SetHandleInformation(child_out_r, HANDLE_FLAG_INHERIT, 0);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --serve", py, script);

    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = child_in_r;
    si.hStdOutput = child_out_w;
    si.hStdError  = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(child_in_r);
    CloseHandle(child_out_w);
    if (si.hStdError != INVALID_HANDLE_VALUE) CloseHandle(si.hStdError);
    if (!ok) {
        CloseHandle(child_in_w);
        CloseHandle(child_out_r);
        set_err("cannot run '%s' (is Python installed?)", py);
        return false;
    }
    CloseHandle(pi.hThread);
    h_proc = pi.hProcess; h_in = child_in_w; h_out = child_out_r;
    return true;
}

static void io_stop(void)
{
    if (h_in)   { CloseHandle(h_in);  h_in = NULL; }          /* EOF ends the service */
    if (h_proc) {
        if (WaitForSingleObject(h_proc, 500) != WAIT_OBJECT_0) TerminateProcess(h_proc, 0);
        CloseHandle(h_proc); h_proc = NULL;
    }
    if (h_out)  { CloseHandle(h_out); h_out = NULL; }
}

static bool io_write(const char *s)
{
    DWORD n;
    return h_in && WriteFile(h_in, s, (DWORD)strlen(s), &n, NULL) && n == strlen(s);
}

static bool io_read_line(char *buf, int size, int timeout_ms)
{
    int len = 0;
    int waited = 0;
    while (len < size - 1) {
        DWORD avail = 0, n;
        if (!PeekNamedPipe(h_out, NULL, 0, NULL, &avail, NULL))
            return false;                                      /* child gone */
        if (avail == 0) {
            if (waited >= timeout_ms || WaitForSingleObject(h_proc, 0) == WAIT_OBJECT_0)
                return false;
            Sleep(2);
            waited += 2;
            continue;
        }
        char ch;
        if (!ReadFile(h_out, &ch, 1, &n, NULL) || n == 0)
            return false;
        if (ch == '\n') break;
        if (ch != '\r') buf[len++] = ch;
    }
    buf[len] = '\0';
    return true;
}

#else  /* ---------------------------- POSIX ----------------------------- */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t g_pid = -1;
static int   fd_in = -1, fd_out = -1;

static bool io_start(const char *py, const char *script)
{
    int to_child[2], from_child[2], status_pipe[2];
    if (pipe(to_child) || pipe(from_child) || pipe(status_pipe)) {
        set_err("cannot create pipes", NULL);
        return false;
    }
    signal(SIGPIPE, SIG_IGN);                                  /* handle write errors ourselves */
    fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC);                /* closes on successful exec */

    g_pid = fork();
    if (g_pid < 0) {
        set_err("fork failed", NULL);
        return false;
    }
    if (g_pid == 0) {                                          /* ---- child ---- */
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) dup2(dn, STDERR_FILENO);
        close(to_child[0]);  close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        close(status_pipe[0]);
        execlp(py, py, script, "--serve", (char *)NULL);
        char c = 1;                                            /* exec failed: tell the parent */
        if (write(status_pipe[1], &c, 1) < 0) { /* ignore */ }
        _exit(127);
    }
    close(to_child[0]); close(from_child[1]); close(status_pipe[1]);
    fd_in = to_child[1]; fd_out = from_child[0];

    char c;
    ssize_t r = read(status_pipe[0], &c, 1);                   /* 0 = exec succeeded */
    close(status_pipe[0]);
    if (r > 0) {
        waitpid(g_pid, NULL, 0);
        g_pid = -1;
        close(fd_in); close(fd_out); fd_in = fd_out = -1;
        set_err("cannot run '%s' (is Python installed?)", py);
        return false;
    }
    return true;
}

static void io_stop(void)
{
    if (fd_in >= 0)  { close(fd_in);  fd_in = -1; }            /* EOF ends the service */
    if (g_pid > 0) {
        for (int i = 0; i < 50; i++) {                         /* wait up to 0.5 s */
            if (waitpid(g_pid, NULL, WNOHANG) != 0) { g_pid = -1; break; }
            nanosleep(&(struct timespec){0, 10000000L}, NULL);
        }
        if (g_pid > 0) { kill(g_pid, SIGKILL); waitpid(g_pid, NULL, 0); g_pid = -1; }
    }
    if (fd_out >= 0) { close(fd_out); fd_out = -1; }
}

static bool io_write(const char *s)
{
    size_t len = strlen(s);
    return fd_in >= 0 && write(fd_in, s, len) == (ssize_t)len;
}

static bool io_read_line(char *buf, int size, int timeout_ms)
{
    int len = 0;
    while (len < size - 1) {
        struct pollfd p = { fd_out, POLLIN, 0 };
        int pr = poll(&p, 1, timeout_ms);
        if (pr <= 0)
            return false;                                      /* timeout / error */
        char ch;
        if (read(fd_out, &ch, 1) != 1)
            return false;                                      /* EOF: child died */
        if (ch == '\n') break;
        if (ch != '\r') buf[len++] = ch;
    }
    buf[len] = '\0';
    return true;
}
#endif

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int ml_start(const char *python_cmd, const char *script)
{
    ml_stop();
    g_err[0] = '\0';
    g_last_req[0] = '\0';

    if (!python_cmd || !*python_cmd) python_cmd = getenv("SVSM_PYTHON");
    if (!python_cmd || !*python_cmd) {
#ifdef _WIN32
        python_cmd = "python";
#else
        python_cmd = "python3";
#endif
    }
    if (!script || !*script) script = getenv("SVSM_ML_SCRIPT");
    if (!script || !*script) script = "ml/predict.py";

    FILE *f = fopen(script, "r");
    if (!f) {
        set_err("predictor script not found: %s", script);
        return -1;
    }
    fclose(f);

    if (!io_start(python_cmd, script))
        return -1;
    g_io = true;

    char line[LINE_MAX_LEN];
    if (!io_read_line(line, sizeof line, START_TIMEOUT_MS)) {
        set_err("predictor did not start (is scikit-learn installed and ml/model trained?)", NULL);
        ml_stop();
        return -1;
    }
    if (strcmp(line, "READY") != 0) {                          /* e.g. "ERR cannot load model ..." */
        snprintf(g_err, sizeof g_err, "%.150s", line);
        ml_stop();
        return -1;
    }
    g_up = true;
    return 0;
}

void ml_stop(void)
{
    if (g_io)
        io_stop();
    g_io = false;
    g_up = false;
}

bool ml_predict(const Vehicle *v, MlPrediction *out)
{
    char req[LINE_MAX_LEN], reply[LINE_MAX_LEN];
    memset(out, 0, sizeof *out);

    if (!g_up)
        return false;
    if (!ml_format_request(v, req, sizeof req)) {
        set_err("sensor reading out of range - no prediction", NULL);
        return false;
    }
    if (!strcmp(req, g_last_req) && g_last.valid) {            /* unchanged inputs */
        *out = g_last;
        return true;
    }
    if (!io_write(req) || !io_read_line(reply, sizeof reply, REPLY_TIMEOUT_MS)) {
        set_err("predictor stopped responding - ML disabled", NULL);
        ml_stop();
        return false;
    }
    if (!ml_parse_reply(reply, out)) {
        snprintf(g_err, sizeof g_err, "%.150s", reply);
        return false;
    }
    g_err[0] = '\0';
    snprintf(g_last_req, sizeof g_last_req, "%s", req);
    g_last = *out;
    return true;
}
