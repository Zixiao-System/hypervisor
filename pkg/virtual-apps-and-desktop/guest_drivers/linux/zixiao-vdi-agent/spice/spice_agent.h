/*
 * Zixiao VDI Agent for Linux - SPICE Agent
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_SPICE_AGENT_H
#define ZIXIAO_VDI_SPICE_AGENT_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPICE agent configuration */
typedef struct {
    const char *virtio_port;
} SpiceAgentConfig;

/* SPICE agent context (opaque) */
typedef struct SpiceAgent SpiceAgent;

/* Create/destroy */
SpiceAgent *spice_agent_create(void);
void spice_agent_destroy(SpiceAgent *sa);

/* Initialize/shutdown */
bool spice_agent_init(SpiceAgent *sa, const SpiceAgentConfig *config);
void spice_agent_shutdown(SpiceAgent *sa);

/* Control */
bool spice_agent_start(SpiceAgent *sa);
void spice_agent_stop(SpiceAgent *sa);
bool spice_agent_is_running(SpiceAgent *sa);

/* Callbacks */
void spice_agent_set_input_callback(SpiceAgent *sa, InputCallback callback, void *user_data);

/* Send data to host */
bool spice_agent_send_frame(SpiceAgent *sa, const FrameData *frame);
bool spice_agent_send_clipboard(SpiceAgent *sa, const ClipboardData *data);
bool spice_agent_send_monitor_config(SpiceAgent *sa, const MonitorInfo *monitors, int count);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_SPICE_AGENT_H */
