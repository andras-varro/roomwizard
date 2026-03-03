/**
 * Logger for RoomWizard native apps
 *
 * Lightweight file logger with automatic rotation.  Output goes to a
 * per-application log file under /var/log/roomwizard/ and optionally
 * also to stderr (for interactive debugging).
 *
 * Log levels:  DEBUG < INFO < WARN < ERROR
 *
 * Each message is prefixed with an ISO-8601 timestamp, level tag, and
 * the application name so that log files are self-describing even when
 * rotated or concatenated.
 *
 * Rotation: when the log file exceeds LOG_MAX_SIZE bytes the current
 * file is renamed to <name>.log.1 (overwriting any previous backup)
 * and a fresh file is started.  This keeps disk usage bounded on the
 * small RoomWizard eMMC.
 *
 * Typical use:
 *   Logger logger;
 *   logger_init(&logger, "vnc_client", LOG_INFO, true);
 *   LOG_INFO(&logger, "Connected to %s:%d", host, port);
 *   LOG_ERROR(&logger, "Connection lost: %s", strerror(errno));
 *   logger_close(&logger);
 *
 * If the log directory or file cannot be opened, the logger silently
 * falls back to stderr-only mode so apps never fail due to logging.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdbool.h>

/* ── Log levels ─────────────────────────────────────────────────────── */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
} LogLevel;

/* ── Logger state ───────────────────────────────────────────────────── */

#define LOG_DIR         "/var/log/roomwizard"
#define LOG_MAX_SIZE    (256 * 1024)   /* 256 KB per log file              */
#define LOG_APP_NAME_MAX 32
#define LOG_PATH_MAX     128

typedef struct {
    FILE      *fp;                          /* Log file handle (NULL = stderr only) */
    char       app_name[LOG_APP_NAME_MAX];  /* e.g. "vnc_client"                    */
    char       path[LOG_PATH_MAX];          /* e.g. "/var/log/roomwizard/vnc_client.log" */
    LogLevel   min_level;                   /* Messages below this are suppressed   */
    bool       echo_stderr;                 /* Also print to stderr?                */
    long       bytes_written;               /* Bytes since last rotation check      */
} Logger;

/* ── Core API ───────────────────────────────────────────────────────── */

/**
 * Initialise the logger.
 *
 * @param log          Logger struct to initialise.
 * @param app_name     Short name (used in log path and prefixes).
 * @param min_level    Minimum level to record (LOG_LEVEL_INFO recommended).
 * @param echo_stderr  If true, also write every message to stderr.
 * @return 0 on success, -1 if log file could not be opened (stderr-only).
 */
int  logger_init(Logger *log, const char *app_name,
                 LogLevel min_level, bool echo_stderr);

/**
 * Write a log message (printf-style).
 * Prefer using the LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR macros.
 */
void logger_write(Logger *log, LogLevel level,
                  const char *file, int line,
                  const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

/**
 * Flush and close the log file.
 */
void logger_close(Logger *log);

/* ── Convenience macros ─────────────────────────────────────────────── */

#define LOG_DEBUG(log, fmt, ...) \
    logger_write((log), LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(log, fmt, ...) \
    logger_write((log), LOG_LEVEL_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(log, fmt, ...) \
    logger_write((log), LOG_LEVEL_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(log, fmt, ...) \
    logger_write((log), LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */
