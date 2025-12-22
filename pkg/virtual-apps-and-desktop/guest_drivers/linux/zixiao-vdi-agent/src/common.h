/*
 * Zixiao VDI Agent for Linux - Common Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_COMMON_H
#define ZIXIAO_VDI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version */
#define ZIXIAO_VDI_VERSION_MAJOR    1
#define ZIXIAO_VDI_VERSION_MINOR    0
#define ZIXIAO_VDI_VERSION_PATCH    0

#define ZIXIAO_VDI_AGENT_NAME       "zixiao-vdi-agent"

/* Logging */
typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

void zixiao_log(LogLevel level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define LOG_TRACE(...)   zixiao_log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...)   zixiao_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)    zixiao_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) zixiao_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)   zixiao_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)   zixiao_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/* Frame data for display capture */
typedef struct {
    uint8_t  *data;
    size_t    data_size;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
    uint64_t  timestamp;
    bool      key_frame;
} FrameData;

/* Audio data */
typedef struct {
    uint8_t  *data;
    size_t    data_size;
    uint32_t  sample_rate;
    uint16_t  channels;
    uint16_t  bits_per_sample;
    uint64_t  timestamp;
} AudioData;

/* Input event types */
typedef enum {
    INPUT_EVENT_MOUSE_MOVE,
    INPUT_EVENT_MOUSE_BUTTON,
    INPUT_EVENT_MOUSE_WHEEL,
    INPUT_EVENT_KEY_DOWN,
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_KEY_PRESS
} InputEventType;

typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  delta_x;
    int32_t  delta_y;
    uint32_t button;    /* 0=none, 1=left, 2=right, 3=middle */
    bool     pressed;
    int32_t  wheel_delta;
} MouseEvent;

typedef struct {
    uint32_t scan_code;
    uint32_t key_sym;
    bool     extended;
    bool     pressed;
} KeyEvent;

typedef struct {
    InputEventType type;
    uint64_t       timestamp;
    union {
        MouseEvent mouse;
        KeyEvent   key;
    };
} InputEvent;

/* Clipboard formats */
typedef enum {
    CLIPBOARD_FORMAT_NONE = 0,
    CLIPBOARD_FORMAT_TEXT,
    CLIPBOARD_FORMAT_UTF8,
    CLIPBOARD_FORMAT_HTML,
    CLIPBOARD_FORMAT_IMAGE_PNG,
    CLIPBOARD_FORMAT_IMAGE_BMP,
    CLIPBOARD_FORMAT_FILE_LIST
} ClipboardFormat;

typedef struct {
    ClipboardFormat format;
    uint8_t        *data;
    size_t          data_size;
    char           *mime_type;
} ClipboardData;

/* Monitor information */
typedef struct {
    uint32_t id;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    bool     primary;
    char     name[64];
} MonitorInfo;

/* Callbacks */
typedef void (*FrameCallback)(const FrameData *frame, void *user_data);
typedef void (*AudioCallback)(const AudioData *audio, void *user_data);
typedef void (*InputCallback)(const InputEvent *event, void *user_data);
typedef void (*ClipboardCallback)(const ClipboardData *data, void *user_data);

/* Subsystem interface */
typedef struct Subsystem {
    const char *name;
    bool (*init)(struct Subsystem *self);
    void (*shutdown)(struct Subsystem *self);
    bool (*start)(struct Subsystem *self);
    void (*stop)(struct Subsystem *self);
    bool (*is_running)(struct Subsystem *self);
    void *priv;
} Subsystem;

/* Utility functions */
static inline uint64_t get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Memory allocation */
static inline void *zixiao_malloc(size_t size) {
    return malloc(size);
}

static inline void *zixiao_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

static inline void *zixiao_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

static inline void zixiao_free(void *ptr) {
    free(ptr);
}

static inline char *zixiao_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

/* Thread-safe queue */
typedef struct QueueNode {
    void *data;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode       *head;
    QueueNode       *tail;
    size_t           size;
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;
} ThreadSafeQueue;

ThreadSafeQueue *queue_create(void);
void queue_destroy(ThreadSafeQueue *q, void (*free_func)(void *));
void queue_push(ThreadSafeQueue *q, void *data);
void *queue_pop(ThreadSafeQueue *q, int timeout_ms);
size_t queue_size(ThreadSafeQueue *q);
void queue_clear(ThreadSafeQueue *q, void (*free_func)(void *));

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_COMMON_H */
