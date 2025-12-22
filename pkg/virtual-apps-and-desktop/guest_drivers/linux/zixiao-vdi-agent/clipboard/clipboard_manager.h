/*
 * Zixiao VDI Agent for Linux - Clipboard Manager
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_CLIPBOARD_MANAGER_H
#define ZIXIAO_VDI_CLIPBOARD_MANAGER_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clipboard manager context (opaque) */
typedef struct ClipboardManager ClipboardManager;

/* Create/destroy */
ClipboardManager *clipboard_manager_create(void);
void clipboard_manager_destroy(ClipboardManager *cm);

/* Initialize/shutdown */
bool clipboard_manager_init(ClipboardManager *cm);
void clipboard_manager_shutdown(ClipboardManager *cm);

/* Control */
bool clipboard_manager_start(ClipboardManager *cm);
void clipboard_manager_stop(ClipboardManager *cm);
bool clipboard_manager_is_running(ClipboardManager *cm);

/* Callback for clipboard changes */
void clipboard_manager_set_callback(ClipboardManager *cm, ClipboardCallback callback, void *user_data);

/* Set clipboard from remote */
bool clipboard_manager_set(ClipboardManager *cm, const ClipboardData *data);

/* Get current clipboard */
bool clipboard_manager_get(ClipboardManager *cm, ClipboardData *data);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_CLIPBOARD_MANAGER_H */
