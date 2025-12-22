/*
 * Zixiao VDI Agent for Linux - Agent Core
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_AGENT_H
#define ZIXIAO_VDI_AGENT_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct DisplayCapture DisplayCapture;
typedef struct AudioCapture AudioCapture;
typedef struct InputHandler InputHandler;
typedef struct ClipboardManager ClipboardManager;
typedef struct SpiceAgent SpiceAgent;
typedef struct WebRTCAgent WebRTCAgent;

/* Agent configuration */
typedef struct {
    bool        spice_enabled;
    bool        webrtc_enabled;
    const char *virtio_port;
    const char *signaling_url;
    uint32_t    target_fps;
    bool        capture_audio;
    bool        daemonize;
    const char *pid_file;
    const char *log_file;
    LogLevel    log_level;
} AgentConfig;

/* VDI Agent */
typedef struct {
    AgentConfig       config;
    bool              running;
    bool              stopping;
    pthread_t         main_thread;

    /* Subsystems */
    DisplayCapture   *display;
    AudioCapture     *audio;
    InputHandler     *input;
    ClipboardManager *clipboard;
    SpiceAgent       *spice;
    WebRTCAgent      *webrtc;
} VDIAgent;

/* Agent lifecycle */
VDIAgent *agent_create(const AgentConfig *config);
void agent_destroy(VDIAgent *agent);
bool agent_init(VDIAgent *agent);
bool agent_start(VDIAgent *agent);
void agent_stop(VDIAgent *agent);
void agent_run(VDIAgent *agent);

/* Get default config */
void agent_config_default(AgentConfig *config);

/* Signal handling */
void agent_setup_signals(VDIAgent *agent);

/* Daemonize */
bool agent_daemonize(VDIAgent *agent);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_AGENT_H */
