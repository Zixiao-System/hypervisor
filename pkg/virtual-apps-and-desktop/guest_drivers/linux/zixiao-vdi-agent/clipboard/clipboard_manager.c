/*
 * Zixiao VDI Agent for Linux - Clipboard Manager Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Uses X11 selections (CLIPBOARD and PRIMARY) for clipboard sync.
 */

#include "clipboard_manager.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>

struct ClipboardManager {
    /* Callback */
    ClipboardCallback callback;
    void             *callback_data;

    /* State */
    bool              running;
    bool              stopping;
    pthread_t         monitor_thread;

    /* X11 */
    Display          *display;
    Window            window;
    Atom              clipboard_atom;
    Atom              targets_atom;
    Atom              utf8_string_atom;
    Atom              incr_atom;

    /* Last clipboard content (to detect changes) */
    char             *last_text;
    size_t            last_text_len;

    /* Ignore flag for self-set */
    bool              ignore_next;
};

static void *monitor_thread_func(void *arg);
static char *get_clipboard_text(ClipboardManager *cm);

ClipboardManager *clipboard_manager_create(void) {
    ClipboardManager *cm = zixiao_calloc(1, sizeof(ClipboardManager));
    if (!cm) return NULL;

    cm->running = false;
    cm->stopping = false;
    cm->ignore_next = false;

    return cm;
}

void clipboard_manager_destroy(ClipboardManager *cm) {
    if (!cm) return;

    clipboard_manager_shutdown(cm);

    if (cm->last_text) {
        zixiao_free(cm->last_text);
    }

    zixiao_free(cm);
}

bool clipboard_manager_init(ClipboardManager *cm) {
    if (!cm) return false;

    LOG_INFO("Initializing clipboard manager...");

    /* Open X display */
    cm->display = XOpenDisplay(NULL);
    if (!cm->display) {
        LOG_ERROR("Failed to open X display for clipboard");
        return false;
    }

    /* Create a window for selection ownership */
    cm->window = XCreateSimpleWindow(
        cm->display,
        DefaultRootWindow(cm->display),
        0, 0, 1, 1, 0, 0, 0
    );

    if (!cm->window) {
        LOG_ERROR("Failed to create clipboard window");
        XCloseDisplay(cm->display);
        cm->display = NULL;
        return false;
    }

    /* Get atoms */
    cm->clipboard_atom = XInternAtom(cm->display, "CLIPBOARD", False);
    cm->targets_atom = XInternAtom(cm->display, "TARGETS", False);
    cm->utf8_string_atom = XInternAtom(cm->display, "UTF8_STRING", False);
    cm->incr_atom = XInternAtom(cm->display, "INCR", False);

    LOG_INFO("Clipboard manager initialized");
    return true;
}

void clipboard_manager_shutdown(ClipboardManager *cm) {
    if (!cm) return;

    clipboard_manager_stop(cm);

    if (cm->window) {
        XDestroyWindow(cm->display, cm->window);
        cm->window = 0;
    }

    if (cm->display) {
        XCloseDisplay(cm->display);
        cm->display = NULL;
    }

    LOG_INFO("Clipboard manager shutdown");
}

bool clipboard_manager_start(ClipboardManager *cm) {
    if (!cm || !cm->display || cm->running) return false;

    LOG_INFO("Starting clipboard manager...");

    cm->stopping = false;
    cm->running = true;

    if (pthread_create(&cm->monitor_thread, NULL, monitor_thread_func, cm) != 0) {
        LOG_ERROR("Failed to create clipboard monitor thread");
        cm->running = false;
        return false;
    }

    LOG_INFO("Clipboard manager started");
    return true;
}

void clipboard_manager_stop(ClipboardManager *cm) {
    if (!cm || !cm->running) return;

    LOG_INFO("Stopping clipboard manager...");

    cm->stopping = true;
    cm->running = false;

    /* Send dummy event to wake up monitor thread */
    if (cm->display && cm->window) {
        XEvent ev = {0};
        ev.type = ClientMessage;
        XSendEvent(cm->display, cm->window, False, 0, &ev);
        XFlush(cm->display);
    }

    pthread_join(cm->monitor_thread, NULL);

    LOG_INFO("Clipboard manager stopped");
}

bool clipboard_manager_is_running(ClipboardManager *cm) {
    return cm && cm->running;
}

void clipboard_manager_set_callback(ClipboardManager *cm, ClipboardCallback callback, void *user_data) {
    if (!cm) return;
    cm->callback = callback;
    cm->callback_data = user_data;
}

bool clipboard_manager_set(ClipboardManager *cm, const ClipboardData *data) {
    if (!cm || !cm->display || !data) return false;

    if (data->format != CLIPBOARD_FORMAT_TEXT &&
        data->format != CLIPBOARD_FORMAT_UTF8) {
        LOG_WARNING("Only text clipboard supported currently");
        return false;
    }

    /* Update our stored text */
    if (cm->last_text) {
        zixiao_free(cm->last_text);
    }

    cm->last_text = zixiao_malloc(data->data_size + 1);
    if (!cm->last_text) return false;

    memcpy(cm->last_text, data->data, data->data_size);
    cm->last_text[data->data_size] = '\0';
    cm->last_text_len = data->data_size;

    /* Take ownership of CLIPBOARD selection */
    XSetSelectionOwner(cm->display, cm->clipboard_atom, cm->window, CurrentTime);
    XFlush(cm->display);

    cm->ignore_next = true;

    LOG_DEBUG("Set clipboard: %zu bytes", data->data_size);
    return true;
}

bool clipboard_manager_get(ClipboardManager *cm, ClipboardData *data) {
    if (!cm || !cm->display || !data) return false;

    char *text = get_clipboard_text(cm);
    if (!text) return false;

    data->format = CLIPBOARD_FORMAT_UTF8;
    data->data_size = strlen(text);
    data->data = (uint8_t *)zixiao_malloc(data->data_size);
    if (!data->data) {
        XFree(text);
        return false;
    }

    memcpy(data->data, text, data->data_size);
    data->mime_type = zixiao_strdup("text/plain;charset=utf-8");

    XFree(text);
    return true;
}

static char *get_clipboard_text(ClipboardManager *cm) {
    Atom property = XInternAtom(cm->display, "ZIXIAO_CLIP", False);

    /* Request clipboard content */
    XConvertSelection(
        cm->display,
        cm->clipboard_atom,
        cm->utf8_string_atom,
        property,
        cm->window,
        CurrentTime
    );
    XFlush(cm->display);

    /* Wait for SelectionNotify event */
    XEvent ev;
    int timeout = 100;
    while (timeout-- > 0) {
        if (XCheckTypedWindowEvent(cm->display, cm->window, SelectionNotify, &ev)) {
            break;
        }
        usleep(10000);
    }

    if (timeout <= 0) {
        return NULL;
    }

    /* Check if conversion succeeded */
    if (ev.xselection.property == None) {
        return NULL;
    }

    /* Get property data */
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    XGetWindowProperty(
        cm->display,
        cm->window,
        property,
        0, 1024 * 1024,
        True,
        AnyPropertyType,
        &type,
        &format,
        &nitems,
        &bytes_after,
        &data
    );

    if (!data || nitems == 0) {
        if (data) XFree(data);
        return NULL;
    }

    /* Make a copy */
    char *result = zixiao_malloc(nitems + 1);
    if (result) {
        memcpy(result, data, nitems);
        result[nitems] = '\0';
    }

    XFree(data);
    return result;
}

static void *monitor_thread_func(void *arg) {
    ClipboardManager *cm = (ClipboardManager *)arg;

    LOG_DEBUG("Clipboard monitor thread started");

    /* Select for property changes and selection events */
    XSelectInput(cm->display, cm->window, PropertyChangeMask);

    while (!cm->stopping) {
        /* Poll for clipboard changes */
        char *text = get_clipboard_text(cm);

        if (text) {
            bool changed = false;

            if (!cm->last_text) {
                changed = true;
            } else if (strcmp(text, cm->last_text) != 0) {
                changed = true;
            }

            if (changed && !cm->ignore_next && cm->callback) {
                /* Update stored text */
                if (cm->last_text) {
                    zixiao_free(cm->last_text);
                }
                cm->last_text = zixiao_strdup(text);
                cm->last_text_len = strlen(text);

                /* Notify callback */
                ClipboardData data = {
                    .format = CLIPBOARD_FORMAT_UTF8,
                    .data = (uint8_t *)text,
                    .data_size = strlen(text),
                    .mime_type = "text/plain;charset=utf-8"
                };

                cm->callback(&data, cm->callback_data);
            }

            cm->ignore_next = false;
            zixiao_free(text);
        }

        /* Handle X events (for selection requests) */
        while (XPending(cm->display)) {
            XEvent ev;
            XNextEvent(cm->display, &ev);

            if (ev.type == SelectionRequest) {
                /* Someone requested our clipboard content */
                XSelectionRequestEvent *req = &ev.xselectionrequest;
                XEvent response = {0};
                response.xselection.type = SelectionNotify;
                response.xselection.requestor = req->requestor;
                response.xselection.selection = req->selection;
                response.xselection.target = req->target;
                response.xselection.time = req->time;
                response.xselection.property = None;

                if (cm->last_text && req->target == cm->utf8_string_atom) {
                    XChangeProperty(
                        cm->display,
                        req->requestor,
                        req->property,
                        cm->utf8_string_atom,
                        8,
                        PropModeReplace,
                        (unsigned char *)cm->last_text,
                        cm->last_text_len
                    );
                    response.xselection.property = req->property;
                } else if (req->target == cm->targets_atom) {
                    Atom targets[] = { cm->targets_atom, cm->utf8_string_atom, XA_STRING };
                    XChangeProperty(
                        cm->display,
                        req->requestor,
                        req->property,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (unsigned char *)targets,
                        3
                    );
                    response.xselection.property = req->property;
                }

                XSendEvent(cm->display, req->requestor, False, 0, &response);
                XFlush(cm->display);
            }
        }

        /* Sleep before next poll */
        usleep(500000);  /* 500ms */
    }

    LOG_DEBUG("Clipboard monitor thread exiting");
    return NULL;
}
