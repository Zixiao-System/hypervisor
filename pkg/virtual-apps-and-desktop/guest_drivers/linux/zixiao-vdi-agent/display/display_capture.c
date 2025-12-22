/*
 * Zixiao VDI Agent for Linux - Display Capture Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Supports X11 (XShm) and PipeWire (for Wayland) screen capture.
 */

#include "display_capture.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>
#include <sys/shm.h>
#include <sys/ipc.h>

struct DisplayCapture {
    /* Configuration */
    DisplayCaptureConfig config;
    FrameCallback        callback;
    void                *callback_data;

    /* State */
    bool                 running;
    bool                 stopping;
    pthread_t            capture_thread;

    /* X11 */
    Display             *display;
    Window               root;
    int                  screen;
    XImage              *image;
    XShmSegmentInfo      shm_info;
    bool                 use_shm;

    /* Screen info */
    int                  width;
    int                  height;
    int                  depth;

    /* Timing */
    uint64_t             frame_interval_us;
    uint64_t             frame_count;
};

static void *capture_thread_func(void *arg);

DisplayCapture *display_capture_create(void) {
    DisplayCapture *dc = zixiao_calloc(1, sizeof(DisplayCapture));
    if (!dc) return NULL;

    dc->config.target_fps = 30;
    dc->config.monitor_id = 0;
    dc->running = false;
    dc->stopping = false;
    dc->use_shm = false;

    return dc;
}

void display_capture_destroy(DisplayCapture *dc) {
    if (!dc) return;

    display_capture_shutdown(dc);
    zixiao_free(dc);
}

bool display_capture_init(DisplayCapture *dc, const DisplayCaptureConfig *config) {
    if (!dc) return false;

    if (config) {
        dc->config = *config;
    }

    LOG_INFO("Initializing display capture...");

    /* Open X display */
    dc->display = XOpenDisplay(NULL);
    if (!dc->display) {
        LOG_ERROR("Failed to open X display");
        return false;
    }

    dc->screen = DefaultScreen(dc->display);
    dc->root = RootWindow(dc->display, dc->screen);
    dc->width = DisplayWidth(dc->display, dc->screen);
    dc->height = DisplayHeight(dc->display, dc->screen);
    dc->depth = DefaultDepth(dc->display, dc->screen);

    LOG_INFO("Display: %dx%d, depth=%d", dc->width, dc->height, dc->depth);

    /* Check for XShm extension */
    int major, minor;
    Bool pixmaps;
    if (XShmQueryVersion(dc->display, &major, &minor, &pixmaps)) {
        LOG_INFO("XShm version %d.%d available", major, minor);

        /* Create shared memory image */
        dc->image = XShmCreateImage(
            dc->display,
            DefaultVisual(dc->display, dc->screen),
            dc->depth,
            ZPixmap,
            NULL,
            &dc->shm_info,
            dc->width,
            dc->height
        );

        if (dc->image) {
            dc->shm_info.shmid = shmget(
                IPC_PRIVATE,
                dc->image->bytes_per_line * dc->image->height,
                IPC_CREAT | 0600
            );

            if (dc->shm_info.shmid >= 0) {
                dc->shm_info.shmaddr = dc->image->data = shmat(dc->shm_info.shmid, NULL, 0);
                dc->shm_info.readOnly = False;

                if (XShmAttach(dc->display, &dc->shm_info)) {
                    dc->use_shm = true;
                    LOG_INFO("XShm initialized successfully");
                } else {
                    LOG_WARNING("XShmAttach failed");
                    shmdt(dc->shm_info.shmaddr);
                    shmctl(dc->shm_info.shmid, IPC_RMID, NULL);
                    XDestroyImage(dc->image);
                    dc->image = NULL;
                }
            } else {
                XDestroyImage(dc->image);
                dc->image = NULL;
            }
        }
    }

    if (!dc->use_shm) {
        LOG_INFO("Using XGetImage fallback (slower)");
    }

    dc->frame_interval_us = 1000000 / dc->config.target_fps;

    LOG_INFO("Display capture initialized");
    return true;
}

void display_capture_shutdown(DisplayCapture *dc) {
    if (!dc) return;

    display_capture_stop(dc);

    if (dc->use_shm && dc->image) {
        XShmDetach(dc->display, &dc->shm_info);
        shmdt(dc->shm_info.shmaddr);
        shmctl(dc->shm_info.shmid, IPC_RMID, NULL);
        XDestroyImage(dc->image);
        dc->image = NULL;
    }

    if (dc->display) {
        XCloseDisplay(dc->display);
        dc->display = NULL;
    }

    LOG_INFO("Display capture shutdown");
}

bool display_capture_start(DisplayCapture *dc) {
    if (!dc || dc->running) return false;

    LOG_INFO("Starting display capture...");

    dc->stopping = false;
    dc->running = true;
    dc->frame_count = 0;

    if (pthread_create(&dc->capture_thread, NULL, capture_thread_func, dc) != 0) {
        LOG_ERROR("Failed to create capture thread");
        dc->running = false;
        return false;
    }

    LOG_INFO("Display capture started");
    return true;
}

void display_capture_stop(DisplayCapture *dc) {
    if (!dc || !dc->running) return;

    LOG_INFO("Stopping display capture...");

    dc->stopping = true;
    dc->running = false;

    pthread_join(dc->capture_thread, NULL);

    LOG_INFO("Display capture stopped");
}

bool display_capture_is_running(DisplayCapture *dc) {
    return dc && dc->running;
}

void display_capture_set_callback(DisplayCapture *dc, FrameCallback callback, void *user_data) {
    if (!dc) return;
    dc->callback = callback;
    dc->callback_data = user_data;
}

void display_capture_set_fps(DisplayCapture *dc, uint32_t fps) {
    if (!dc || fps == 0) return;
    dc->config.target_fps = fps;
    dc->frame_interval_us = 1000000 / fps;
}

bool display_capture_capture_frame(DisplayCapture *dc, FrameData *frame) {
    if (!dc || !dc->display || !frame) return false;

    XImage *img = NULL;

    if (dc->use_shm && dc->image) {
        /* Use shared memory for fast capture */
        if (!XShmGetImage(dc->display, dc->root, dc->image, 0, 0, AllPlanes)) {
            LOG_ERROR("XShmGetImage failed");
            return false;
        }
        img = dc->image;
    } else {
        /* Fallback to XGetImage */
        img = XGetImage(dc->display, dc->root, 0, 0, dc->width, dc->height, AllPlanes, ZPixmap);
        if (!img) {
            LOG_ERROR("XGetImage failed");
            return false;
        }
    }

    /* Copy image data */
    frame->width = img->width;
    frame->height = img->height;
    frame->stride = img->bytes_per_line;
    frame->timestamp = get_timestamp_ms();
    frame->key_frame = (dc->frame_count % 30 == 0);
    frame->data_size = img->bytes_per_line * img->height;

    frame->data = zixiao_malloc(frame->data_size);
    if (!frame->data) {
        if (!dc->use_shm) {
            XDestroyImage(img);
        }
        return false;
    }

    memcpy(frame->data, img->data, frame->data_size);

    if (!dc->use_shm) {
        XDestroyImage(img);
    }

    dc->frame_count++;
    return true;
}

static void *capture_thread_func(void *arg) {
    DisplayCapture *dc = (DisplayCapture *)arg;

    LOG_DEBUG("Capture thread started");

    while (!dc->stopping) {
        uint64_t start = get_timestamp_ms() * 1000;

        FrameData frame = {0};
        if (display_capture_capture_frame(dc, &frame)) {
            if (dc->callback) {
                dc->callback(&frame, dc->callback_data);
            }
            zixiao_free(frame.data);
        }

        /* Rate limiting */
        uint64_t elapsed = get_timestamp_ms() * 1000 - start;
        if (elapsed < dc->frame_interval_us) {
            usleep(dc->frame_interval_us - elapsed);
        }
    }

    LOG_DEBUG("Capture thread exiting");
    return NULL;
}

bool display_capture_get_monitor_info(DisplayCapture *dc, MonitorInfo *info) {
    if (!dc || !dc->display || !info) return false;

    info->id = 0;
    info->x = 0;
    info->y = 0;
    info->width = dc->width;
    info->height = dc->height;
    info->depth = dc->depth;
    info->primary = true;
    snprintf(info->name, sizeof(info->name), "Screen %d", dc->screen);

    return true;
}

int display_capture_enumerate_monitors(DisplayCapture *dc, MonitorInfo *monitors, int max_count) {
    if (!dc || !dc->display || !monitors || max_count <= 0) return 0;

    int count = 0;

    /* Try XRandR for multi-monitor support */
    int event_base, error_base;
    if (XRRQueryExtension(dc->display, &event_base, &error_base)) {
        XRRScreenResources *res = XRRGetScreenResources(dc->display, dc->root);
        if (res) {
            for (int i = 0; i < res->ncrtc && count < max_count; i++) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dc->display, res, res->crtcs[i]);
                if (crtc && crtc->mode != None) {
                    monitors[count].id = count;
                    monitors[count].x = crtc->x;
                    monitors[count].y = crtc->y;
                    monitors[count].width = crtc->width;
                    monitors[count].height = crtc->height;
                    monitors[count].depth = dc->depth;
                    monitors[count].primary = (crtc->x == 0 && crtc->y == 0);
                    snprintf(monitors[count].name, sizeof(monitors[count].name),
                             "Monitor %d", count);
                    count++;
                }
                if (crtc) XRRFreeCrtcInfo(crtc);
            }
            XRRFreeScreenResources(res);
        }
    }

    /* Fallback to single monitor */
    if (count == 0) {
        display_capture_get_monitor_info(dc, &monitors[0]);
        count = 1;
    }

    return count;
}
