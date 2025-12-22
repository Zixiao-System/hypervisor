/*
 * Zixiao VDI Agent for Linux - Common Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include <stdarg.h>
#include <time.h>
#include <syslog.h>

/* Logging configuration */
static LogLevel g_min_log_level = LOG_LEVEL_INFO;
static bool g_use_syslog = false;
static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *log_level_str[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static int log_level_to_syslog[] = {
    LOG_DEBUG, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR, LOG_CRIT
};

void zixiao_log_init(LogLevel min_level, bool use_syslog, const char *log_file) {
    g_min_log_level = min_level;
    g_use_syslog = use_syslog;

    if (use_syslog) {
        openlog(ZIXIAO_VDI_AGENT_NAME, LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }

    if (log_file) {
        g_log_file = fopen(log_file, "a");
        if (!g_log_file) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
        }
    }
}

void zixiao_log_shutdown(void) {
    if (g_use_syslog) {
        closelog();
    }
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void zixiao_log(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_min_log_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    pthread_mutex_lock(&g_log_mutex);

    /* Get timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Extract filename */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    /* Format message */
    char message[2048];
    vsnprintf(message, sizeof(message), fmt, args);

    /* Output to stderr */
    fprintf(stderr, "[%s] [%s] %s:%d: %s\n",
            timestamp, log_level_str[level], basename, line, message);

    /* Output to log file */
    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%s] %s:%d: %s\n",
                timestamp, log_level_str[level], basename, line, message);
        fflush(g_log_file);
    }

    /* Output to syslog */
    if (g_use_syslog) {
        syslog(log_level_to_syslog[level], "%s:%d: %s", basename, line, message);
    }

    pthread_mutex_unlock(&g_log_mutex);
    va_end(args);
}

/* Thread-safe queue implementation */
ThreadSafeQueue *queue_create(void) {
    ThreadSafeQueue *q = zixiao_calloc(1, sizeof(ThreadSafeQueue));
    if (!q) return NULL;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;

    return q;
}

void queue_destroy(ThreadSafeQueue *q, void (*free_func)(void *)) {
    if (!q) return;

    queue_clear(q, free_func);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    zixiao_free(q);
}

void queue_push(ThreadSafeQueue *q, void *data) {
    if (!q) return;

    QueueNode *node = zixiao_malloc(sizeof(QueueNode));
    if (!node) return;

    node->data = data;
    node->next = NULL;

    pthread_mutex_lock(&q->mutex);

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->size++;

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

void *queue_pop(ThreadSafeQueue *q, int timeout_ms) {
    if (!q) return NULL;

    pthread_mutex_lock(&q->mutex);

    if (timeout_ms > 0 && !q->head) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&q->cond, &q->mutex, &ts);
    }

    void *data = NULL;
    if (q->head) {
        QueueNode *node = q->head;
        data = node->data;
        q->head = node->next;
        if (!q->head) {
            q->tail = NULL;
        }
        q->size--;
        zixiao_free(node);
    }

    pthread_mutex_unlock(&q->mutex);
    return data;
}

size_t queue_size(ThreadSafeQueue *q) {
    if (!q) return 0;

    pthread_mutex_lock(&q->mutex);
    size_t size = q->size;
    pthread_mutex_unlock(&q->mutex);

    return size;
}

void queue_clear(ThreadSafeQueue *q, void (*free_func)(void *)) {
    if (!q) return;

    pthread_mutex_lock(&q->mutex);

    while (q->head) {
        QueueNode *node = q->head;
        q->head = node->next;
        if (free_func && node->data) {
            free_func(node->data);
        }
        zixiao_free(node);
    }
    q->tail = NULL;
    q->size = 0;

    pthread_mutex_unlock(&q->mutex);
}
