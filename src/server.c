/**
 * @file    server.c
 * @brief   Single-threaded HTTP server: static files + JSON API.
 *
 * One loop does everything:
 *     select() on the listening socket with a timeout equal to the time
 *     left until the next simulation tick; on timeout run sim_step(); on a
 *     connection read one request, answer it, close.
 * No threads and no locks are needed because requests and ticks never overlap.
 *
 * Commands (query string of /api/cmd):
 *   a=start | stop | reset | demo | accel | brake | refuel
 *   a=target&v=N        target speed          a=belt[&v=0|1]   toggle / set belt
 *   a=manual&v=0|1      freeze physics        a=rate&v=1|2|4   simulation speed
 *   a=set&speed=&fuel=&temp=&obst=&belt=&manual=   set sensors by hand
 *   a=clear             clear the alert history
 */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "server.h"
#include "config.h"
#include "vehicle.h"
#include "monitor.h"
#include "simulation.h"
#include "logger.h"

#define DEMO_SECONDS   45
#define HIST_MAX       500
#define MAX_FILE_BYTES (4 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* server state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    Vehicle       v;
    SafetyReport  prev, cur;
    bool          manual, demo;
    int           rate;               /* 1, 2 or 4                    */
    unsigned long log_seq;            /* bumps whenever history changes */
    double        speed_sum;
    float         max_speed;
} Srv;

static Srv  g;
static char g_web[256] = "web";
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int tick_interval(void) { return SIM_TICK_MS / g.rate; }

/** Evaluate the safety rules and log new / escalated alerts. */
static void refresh(void)
{
    monitor_evaluate(&g.v, &g.cur);
    g.log_seq += (unsigned long)logger_log_changes(&g.prev, &g.cur, &g.v);
    g.prev = g.cur;
}

static void clear_run_stats(void)
{
    g.speed_sum = 0.0;
    g.max_speed = 0.0f;
    memset(&g.prev, 0, sizeof g.prev);
}

static void do_tick(void)
{
    if (g.v.state != SIM_RUNNING || g.manual)
        return;

    if (g.demo) {
        sim_demo_script(&g.v, (int)g.v.ticks);
        if (g.v.ticks >= DEMO_SECONDS) {
            g.demo = false;
            sim_stop(&g.v);
        }
    }

    unsigned long before = g.v.ticks;
    sim_step(&g.v);
    if (g.v.ticks != before) {
        g.speed_sum += g.v.speed_kmh;
        if (g.v.speed_kmh > g.max_speed)
            g.max_speed = g.v.speed_kmh;
    }
    refresh();
}

/* ------------------------------------------------------------------ */
/* growable text buffer                                                */
/* ------------------------------------------------------------------ */

typedef struct { char *p; size_t len, cap; } Buf;

static void b_init(Buf *b)  { b->cap = 1024; b->len = 0; b->p = malloc(b->cap); if (b->p) b->p[0] = '\0'; }
static void b_free(Buf *b)  { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static void b_add(Buf *b, const char *fmt, ...)
{
    if (!b->p)
        return;
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0)
            return;
        if ((size_t)n < b->cap - b->len) {
            b->len += (size_t)n;
            return;
        }
        char *np = realloc(b->p, b->cap * 2 + (size_t)n);
        if (!np)
            return;
        b->p = np;
        b->cap = b->cap * 2 + (size_t)n;
    }
}

/** Append @p s as a JSON string literal (with quotes). */
static void b_json_str(Buf *b, const char *s)
{
    b_add(b, "\"");
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') b_add(b, "\\%c", ch);
        else if (ch < 0x20)          b_add(b, "\\u%04x", ch);
        else                         b_add(b, "%c", ch);
    }
    b_add(b, "\"");
}

/* ------------------------------------------------------------------ */
/* alert-history parsing (log file -> records)                         */
/* ------------------------------------------------------------------ */

typedef struct {
    char  ts[24], level[12], type[16], msg[ALERT_MSG_LEN + 8], belt[4];
    float spd, fuel, temp, obst;
} HistRec;

static void copy_trim(char *dst, size_t cap, const char *a, const char *b)
{
    while (a < b && *a == ' ') a++;
    while (b > a && b[-1] == ' ') b--;
    size_t n = (size_t)(b - a);
    if (n >= cap) n = cap - 1;
    memcpy(dst, a, n);
    dst[n] = '\0';
}

/** [ts] [LEVEL   ] TYPE     : message | spd=.. fuel=.. temp=.. obst=.. belt=ON */
static bool parse_line(char *ln, HistRec *r)
{
    size_t L = strlen(ln);
    while (L && (ln[L - 1] == '\n' || ln[L - 1] == '\r'))
        ln[--L] = '\0';
    if (ln[0] != '[')
        return false;

    char *e = strchr(ln, ']');
    if (!e) return false;
    copy_trim(r->ts, sizeof r->ts, ln + 1, e);

    char *l0 = strchr(e + 1, '[');
    char *l1 = l0 ? strchr(l0, ']') : NULL;
    if (!l1) return false;
    copy_trim(r->level, sizeof r->level, l0 + 1, l1);

    char *t0 = l1 + 1;
    char *c  = strchr(t0, ':');
    if (!c) return false;
    copy_trim(r->type, sizeof r->type, t0, c);

    char *m0 = c + 1;
    char *sp = strstr(m0, " | spd=");
    if (!sp) return false;
    copy_trim(r->msg, sizeof r->msg, m0, sp);

    r->belt[0] = '\0';
    return sscanf(sp + 3, "spd=%f fuel=%f temp=%f obst=%f belt=%3s",
                  &r->spd, &r->fuel, &r->temp, &r->obst, r->belt) == 5;
}

/** Read the last @p max records (chronological order). Caller frees *out. */
static int read_history(HistRec **out, int max)
{
    *out = NULL;
    if (max < 1) max = 1;
    if (max > HIST_MAX) max = HIST_MAX;

    char *mem = NULL;
    size_t sz = 0;
    FILE *f = open_memstream(&mem, &sz);
    if (!f)
        return 0;
    logger_show_recent(f, max);
    fclose(f);
    if (!mem)
        return 0;

    HistRec *arr = calloc((size_t)max, sizeof *arr);
    int n = 0;
    char *save = NULL;
    for (char *ln = strtok_r(mem, "\n", &save); ln && arr && n < max; ln = strtok_r(NULL, "\n", &save))
        if (parse_line(ln, &arr[n]))
            n++;
    free(mem);
    *out = arr;
    return n;
}

/* ------------------------------------------------------------------ */
/* query-string helpers and commands                                   */
/* ------------------------------------------------------------------ */

static bool qget(const char *q, const char *key, char *val, size_t cap)
{
    size_t kl = strlen(key);
    for (const char *p = q; p && *p; ) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t n = strcspn(v, "&");
            if (n >= cap) n = cap - 1;
            memcpy(val, v, n);
            val[n] = '\0';
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

static bool qflag(const char *q, const char *key, bool *out)
{
    char v[16];
    if (!qget(q, key, v, sizeof v))
        return false;
    *out = (v[0] == '1' || v[0] == 't' || v[0] == 'y');
    return true;
}

static void handle_cmd(const char *q)
{
    char a[24], v[32];
    if (!qget(q, "a", a, sizeof a))
        return;
    bool hasv = qget(q, "v", v, sizeof v);
    bool flag;

    if      (!strcmp(a, "start"))  { g.manual = false; sim_start(&g.v); }
    else if (!strcmp(a, "stop"))   { sim_stop(&g.v); g.demo = false; }
    else if (!strcmp(a, "reset"))  { sim_reset(&g.v); g.manual = false; g.demo = false; clear_run_stats(); }
    else if (!strcmp(a, "demo"))   {
        g.manual = false;
        sim_reset(&g.v);
        g.v.fuel_pct = 40.0f;
        clear_run_stats();
        sim_start(&g.v);
        g.demo = true;
    }
    else if (!strcmp(a, "accel"))  sim_accelerate(&g.v);
    else if (!strcmp(a, "brake"))  sim_brake(&g.v);
    else if (!strcmp(a, "refuel")) sim_refuel(&g.v);
    else if (!strcmp(a, "target") && hasv) { g.v.target_speed_kmh = (float)atof(v); vehicle_clamp(&g.v); }
    else if (!strcmp(a, "belt"))   {
        if (hasv) g.v.seatbelt_fastened = (v[0] == '1');
        else      sim_toggle_seatbelt(&g.v);
    }
    else if (!strcmp(a, "manual") && hasv) g.manual = (v[0] == '1');
    else if (!strcmp(a, "rate") && hasv) {
        int r = atoi(v);
        if (r == 1 || r == 2 || r == 4)
            g.rate = r;
    }
    else if (!strcmp(a, "set")) {
        char x[32];
        if (qflag(q, "manual", &flag)) g.manual = flag;
        if (qget(q, "speed", x, sizeof x)) { g.v.speed_kmh = (float)atof(x); g.v.target_speed_kmh = g.v.speed_kmh; }
        if (qget(q, "fuel",  x, sizeof x)) g.v.fuel_pct      = (float)atof(x);
        if (qget(q, "temp",  x, sizeof x)) g.v.engine_temp_c = (float)atof(x);
        if (qget(q, "obst",  x, sizeof x)) g.v.obstacle_m    = (float)atof(x);
        if (qflag(q, "belt", &flag))       g.v.seatbelt_fastened = flag;
        vehicle_clamp(&g.v);
    }
    else if (!strcmp(a, "clear")) { logger_clear(); g.log_seq++; }

    /* start / demo may carry the driver's seatbelt answer: a=start&belt=1 */
    if ((!strcmp(a, "start") || !strcmp(a, "demo")) && qflag(q, "belt", &flag))
        g.v.seatbelt_fastened = flag;

    refresh();
}

/* ------------------------------------------------------------------ */
/* JSON / CSV bodies                                                   */
/* ------------------------------------------------------------------ */

static void write_state(Buf *b)
{
    const Vehicle *v = &g.v;
    const SafetyReport *r = &g.cur;

    b_add(b, "{\"running\":%s,\"manual\":%s,\"demo\":%s,\"rate\":%d,\"ticks\":%lu,\"seq\":%lu,",
          v->state == SIM_RUNNING ? "true" : "false", g.manual ? "true" : "false",
          g.demo ? "true" : "false", g.rate, v->ticks, g.log_seq);
    b_add(b, "\"max_speed\":%.1f,\"avg_speed\":%.1f,",
          g.max_speed, v->ticks ? g.speed_sum / (double)v->ticks : 0.0);
    b_add(b, "\"v\":{\"speed\":%.2f,\"fuel\":%.2f,\"temp\":%.2f,\"obst\":%.2f,\"belt\":%s,\"target\":%.1f,\"odo\":%.3f},",
          v->speed_kmh, v->fuel_pct, v->engine_temp_c, v->obstacle_m,
          v->seatbelt_fastened ? "true" : "false", v->target_speed_kmh, v->odometer_km);
    b_add(b, "\"report\":{\"overall\":%d,\"count\":%d,\"alerts\":{", (int)r->overall, r->active_count);

    bool first = true;
    for (int i = 0; i < ALERT_COUNT; i++) {
        if (!r->alerts[i].active)
            continue;
        b_add(b, "%s\"%s\":{\"level\":%d,\"msg\":", first ? "" : ",", alert_name((AlertType)i), (int)r->alerts[i].level);
        b_json_str(b, r->alerts[i].message);
        b_add(b, "}");
        first = false;
    }
    b_add(b, "}}}");
}

static void write_history_json(Buf *b, int n)
{
    HistRec *h = NULL;
    int cnt = read_history(&h, n);

    b_add(b, "[");
    for (int i = cnt - 1; i >= 0; i--) {                 /* newest first */
        b_add(b, "%s{\"ts\":", i == cnt - 1 ? "" : ",");
        b_json_str(b, h[i].ts);
        b_add(b, ",\"level\":");  b_json_str(b, h[i].level);
        b_add(b, ",\"type\":");   b_json_str(b, h[i].type);
        b_add(b, ",\"msg\":");    b_json_str(b, h[i].msg);
        b_add(b, ",\"spd\":\"%.0f\",\"fuel\":\"%.1f\",\"temp\":\"%.0f\",\"obst\":\"%.1f\",\"belt\":",
              h[i].spd, h[i].fuel, h[i].temp, h[i].obst);
        b_json_str(b, h[i].belt);
        b_add(b, "}");
    }
    b_add(b, "]");
    free(h);
}

static void write_history_csv(Buf *b)
{
    HistRec *h = NULL;
    int cnt = read_history(&h, HIST_MAX);

    b_add(b, "time,severity,type,message,speed_kmh,fuel_pct,temp_c,obstacle_m,seatbelt\n");
    for (int i = 0; i < cnt; i++) {
        b_add(b, "%s,%s,%s,\"", h[i].ts, h[i].level, h[i].type);
        for (const char *c = h[i].msg; *c; c++)
            b_add(b, *c == '"' ? "\"\"" : "%c", *c);
        b_add(b, "\",%.0f,%.1f,%.0f,%.1f,%s\n", h[i].spd, h[i].fuel, h[i].temp, h[i].obst, h[i].belt);
    }
    free(h);
}

/* ------------------------------------------------------------------ */
/* HTTP plumbing                                                       */
/* ------------------------------------------------------------------ */

static void send_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k <= 0)
            return;
        p += k;
        n -= (size_t)k;
    }
}

static void respond(int fd, int code, const char *ctype, const char *extra,
                    const char *body, size_t len)
{
    const char *text = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request"
                     : code == 404 ? "Not Found" : "Server Error";
    char head[512];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n%sConnection: close\r\n\r\n",
        code, text, ctype, len, extra ? extra : "");
    send_all(fd, head, (size_t)n);
    if (len)
        send_all(fd, body, len);
}

static void respond_buf(int fd, const char *ctype, const char *extra, Buf *b)
{
    respond(fd, 200, ctype, extra, b->p ? b->p : "", b->len);
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)                      return "application/octet-stream";
    if (!strcmp(dot, ".html"))     return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))      return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js"))       return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json"))     return "application/json";
    if (!strcmp(dot, ".svg"))      return "image/svg+xml";
    if (!strcmp(dot, ".png"))      return "image/png";
    if (!strcmp(dot, ".ico"))      return "image/x-icon";
    return "application/octet-stream";
}

static void serve_static(int fd, const char *path)
{
    if (strstr(path, "..") || path[0] != '/') {
        respond(fd, 404, "text/plain", NULL, "Not found\n", 10);
        return;
    }

    char full[1400];
    snprintf(full, sizeof full, "%s%s%s", g_web, path, path[strlen(path) - 1] == '/' ? "index.html" : "");

    struct stat st;
    FILE *f = (stat(full, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= MAX_FILE_BYTES)
              ? fopen(full, "rb") : NULL;
    if (!f) {
        respond(fd, 404, "text/plain", NULL, "Not found\n", 10);
        return;
    }
    char *data = malloc((size_t)st.st_size + 1);
    size_t n = data ? fread(data, 1, (size_t)st.st_size, f) : 0;
    fclose(f);
    if (data)
        respond(fd, 200, mime_for(full), NULL, data, n);
    else
        respond(fd, 500, "text/plain", NULL, "Out of memory\n", 14);
    free(data);
}

static void serve_api(int fd, const char *path, const char *query)
{
    Buf b;
    b_init(&b);

    if (!strcmp(path, "/api/state")) {
        write_state(&b);
        respond_buf(fd, "application/json", NULL, &b);
    } else if (!strcmp(path, "/api/cmd")) {
        handle_cmd(query);
        write_state(&b);
        respond_buf(fd, "application/json", NULL, &b);
    } else if (!strcmp(path, "/api/history")) {
        char nv[16];
        int n = qget(query, "n", nv, sizeof nv) ? atoi(nv) : 200;
        write_history_json(&b, n);
        respond_buf(fd, "application/json", NULL, &b);
    } else if (!strcmp(path, "/api/export.log")) {
        FILE *f = fopen(logger_path(), "rb");
        char chunk[4096];
        size_t k;
        while (f && (k = fread(chunk, 1, sizeof chunk, f)) > 0)
            b_add(&b, "%.*s", (int)k, chunk);
        if (f) fclose(f);
        respond_buf(fd, "text/plain; charset=utf-8",
                    "Content-Disposition: attachment; filename=\"alert_history.log\"\r\n", &b);
    } else if (!strcmp(path, "/api/export.csv")) {
        write_history_csv(&b);
        respond_buf(fd, "text/csv; charset=utf-8",
                    "Content-Disposition: attachment; filename=\"alert_history.csv\"\r\n", &b);
    } else {
        respond(fd, 404, "application/json", NULL, "{\"error\":\"unknown endpoint\"}", 27);
    }
    b_free(&b);
}

static void handle_connection(int fd)
{
    char req[4096];
    ssize_t n = recv(fd, req, sizeof req - 1, 0);
    if (n <= 0)
        return;                                   /* idle / pre-connect socket */
    req[n] = '\0';

    char method[8], target[1024];
    if (sscanf(req, "%7s %1023s", method, target) != 2) {
        respond(fd, 400, "text/plain", NULL, "Bad request\n", 12);
        return;
    }
    if (!strcmp(method, "OPTIONS")) {
        respond(fd, 204, "text/plain", NULL, "", 0);
        return;
    }
    if (strcmp(method, "GET") != 0) {
        respond(fd, 400, "text/plain", NULL, "GET only\n", 9);
        return;
    }

    char *query = strchr(target, '?');
    if (query)
        *query++ = '\0';
    else
        query = target + strlen(target);

    if (!strncmp(target, "/api/", 5))
        serve_api(fd, target, query);
    else
        serve_static(fd, target);
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

int server_run(int port, const char *web_dir, bool all_hosts)
{
    if (web_dir && *web_dir) {
        strncpy(g_web, web_dir, sizeof g_web - 1);
        g_web[sizeof g_web - 1] = '\0';
    }
    size_t wl = strlen(g_web);
    while (wl > 1 && g_web[wl - 1] == '/')
        g_web[--wl] = '\0';

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;                    /* no SA_RESTART: select() must wake up */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    memset(&g, 0, sizeof g);
    vehicle_reset(&g.v);
    g.rate = 1;
    refresh();

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(all_hosts ? INADDR_ANY : INADDR_LOOPBACK);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(lfd, 16) < 0) {
        fprintf(stderr, "Cannot listen on port %d: %s\n", port, strerror(errno));
        close(lfd);
        return 1;
    }

    printf("%s v%s\n", APP_NAME, APP_VERSION);
    printf("C backend running ->  http://localhost:%d\n", port);
    printf("Serving web files from '%s', alert history in '%s'\n", g_web, logger_path());
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    long next_tick = now_ms() + tick_interval();

    while (!g_stop) {
        bool ticking = (g.v.state == SIM_RUNNING && !g.manual);
        long now = now_ms();
        long wait = ticking ? next_tick - now : 500;
        if (wait < 0)   wait = 0;
        if (wait > 500) wait = 500;

        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(lfd, &rd);
        struct timeval tv = { wait / 1000, (wait % 1000) * 1000 };
        int rc = select(lfd + 1, &rd, NULL, NULL, &tv);

        now = now_ms();
        if (ticking) {
            int burst = 0;
            while (now >= next_tick && burst++ < 4) {
                do_tick();
                next_tick += tick_interval();
            }
            if (now >= next_tick)
                next_tick = now + tick_interval();
        } else {
            next_tick = now + tick_interval();
        }

        if (rc > 0 && FD_ISSET(lfd, &rd)) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) {
                struct timeval to = { 0, 300000 };          /* ignore silent pre-connects */
                setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
                handle_connection(cfd);
                close(cfd);
            }
        }
    }

    close(lfd);
    printf("\nServer stopped. Alert history saved in %s\n", logger_path());
    return 0;
}
