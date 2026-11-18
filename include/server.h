/**
 * @file    server.h
 * @brief   Tiny HTTP server that exposes the C simulation to the web dashboard.
 *
 * The browser is only a display and a remote control: every reading, alert
 * and history record comes from the same C code used by the terminal version
 * (simulation.c, monitor.c, logger.c).
 *
 *   GET /                     web/index.html (and other static files)
 *   GET /api/state            live readings + safety report (JSON)
 *   GET /api/cmd?a=...        send a command, returns the new state
 *   GET /api/history?n=200    alert history, newest first (JSON)
 *   GET /api/export.log       raw alert_history.log
 *   GET /api/export.csv       alert history as CSV
 */
#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

/**
 * Run the server until SIGINT/SIGTERM.
 * @param port       TCP port to listen on (the next 19 are tried if it is busy)
 * @param web_dir    folder holding index.html, style.css, app.js
 * @param all_hosts  false = listen on 127.0.0.1 only (default, safe)
 * @param open_browser true = open the dashboard in the default browser once listening
 * @return 0 on clean shutdown, non-zero if the socket could not be opened
 */
int server_run(int port, const char *web_dir, bool all_hosts, bool open_browser);

#endif /* SERVER_H */
