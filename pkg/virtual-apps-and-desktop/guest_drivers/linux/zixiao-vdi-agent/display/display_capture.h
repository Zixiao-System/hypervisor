/*
 * Zixiao VDI Agent for Linux - Display Capture
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_DISPLAY_CAPTURE_H
#define ZIXIAO_VDI_DISPLAY_CAPTURE_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Display capture configuration */
typedef struct {
    uint32_t target_fps;
    uint32_t monitor_id;
} DisplayCaptureConfig;

/* Display capture context (opaque) */
typedef struct DisplayCapture DisplayCapture;

/* Create/destroy */
DisplayCapture *display_capture_create(void);
void display_capture_destroy(DisplayCapture *dc);

/* Initialize/shutdown */
bool display_capture_init(DisplayCapture *dc, const DisplayCaptureConfig *config);
void display_capture_shutdown(DisplayCapture *dc);

/* Control */
bool display_capture_start(DisplayCapture *dc);
void display_capture_stop(DisplayCapture *dc);
bool display_capture_is_running(DisplayCapture *dc);

/* Configuration */
void display_capture_set_callback(DisplayCapture *dc, FrameCallback callback, void *user_data);
void display_capture_set_fps(DisplayCapture *dc, uint32_t fps);

/* Query */
bool display_capture_get_monitor_info(DisplayCapture *dc, MonitorInfo *info);
int display_capture_enumerate_monitors(DisplayCapture *dc, MonitorInfo *monitors, int max_count);

/* Manual capture */
bool display_capture_capture_frame(DisplayCapture *dc, FrameData *frame);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_DISPLAY_CAPTURE_H */
