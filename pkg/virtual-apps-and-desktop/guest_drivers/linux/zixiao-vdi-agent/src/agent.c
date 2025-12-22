/*
 * Zixiao VDI Agent for Linux - Agent Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent.h"
#include "../display/display_capture.h"
#include "../audio/audio_capture.h"
#include "../input/input_handler.h"
#include "../clipboard/clipboard_manager.h"
#include "../spice/spice_agent.h"
#include "../webrtc/webrtc_agent.h"

#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

static VDIAgent *g_agent = NULL;

/* Log function declaration */
void zixiao_log_init(LogLevel min_level, bool use_syslog, const char *log_file);
void zixiao_log_shutdown(void);

void agent_config_default(AgentConfig *config) {
    memset(config, 0, sizeof(AgentConfig));
    config->spice_enabled = true;
    config->webrtc_enabled = true;
    config->virtio_port = "/dev/virtio-ports/org.zixiao.vdi.0";
    config->signaling_url = "ws://localhost:8080/signaling";
    config->target_fps = 30;
    config->capture_audio = true;
    config->daemonize = false;
    config->pid_file = "/var/run/zixiao-vdi-agent.pid";
    config->log_file = NULL;
    config->log_level = LOG_LEVEL_INFO;
}

VDIAgent *agent_create(const AgentConfig *config) {
    VDIAgent *agent = zixiao_calloc(1, sizeof(VDIAgent));
    if (!agent) {
        return NULL;
    }

    if (config) {
        memcpy(&agent->config, config, sizeof(AgentConfig));
    } else {
        agent_config_default(&agent->config);
    }

    agent->running = false;
    agent->stopping = false;

    return agent;
}

void agent_destroy(VDIAgent *agent) {
    if (!agent) return;

    agent_stop(agent);

    /* Destroy subsystems */
    if (agent->webrtc) {
        webrtc_agent_destroy(agent->webrtc);
    }
    if (agent->spice) {
        spice_agent_destroy(agent->spice);
    }
    if (agent->clipboard) {
        clipboard_manager_destroy(agent->clipboard);
    }
    if (agent->input) {
        input_handler_destroy(agent->input);
    }
    if (agent->audio) {
        audio_capture_destroy(agent->audio);
    }
    if (agent->display) {
        display_capture_destroy(agent->display);
    }

    zixiao_free(agent);
}

/* Frame callback for display capture */
static void on_frame_captured(const FrameData *frame, void *user_data) {
    VDIAgent *agent = (VDIAgent *)user_data;

    if (agent->spice) {
        spice_agent_send_frame(agent->spice, frame);
    }
    if (agent->webrtc) {
        webrtc_agent_send_frame(agent->webrtc, frame);
    }
}

/* Audio callback */
static void on_audio_captured(const AudioData *audio, void *user_data) {
    VDIAgent *agent = (VDIAgent *)user_data;

    if (agent->webrtc) {
        webrtc_agent_send_audio(agent->webrtc, audio);
    }
}

/* Input callback from SPICE/WebRTC */
static void on_input_received(const InputEvent *event, void *user_data) {
    VDIAgent *agent = (VDIAgent *)user_data;

    if (agent->input) {
        input_handler_inject(agent->input, event);
    }
}

/* Clipboard callback */
static void on_clipboard_changed(const ClipboardData *data, void *user_data) {
    VDIAgent *agent = (VDIAgent *)user_data;

    if (agent->spice) {
        spice_agent_send_clipboard(agent->spice, data);
    }
}

bool agent_init(VDIAgent *agent) {
    if (!agent) return false;

    LOG_INFO("Initializing Zixiao VDI Agent v%d.%d.%d",
             ZIXIAO_VDI_VERSION_MAJOR, ZIXIAO_VDI_VERSION_MINOR, ZIXIAO_VDI_VERSION_PATCH);

    /* Initialize display capture */
    agent->display = display_capture_create();
    if (!agent->display) {
        LOG_ERROR("Failed to create display capture");
        return false;
    }

    DisplayCaptureConfig display_config = {
        .target_fps = agent->config.target_fps,
        .monitor_id = 0  /* Primary monitor */
    };

    if (!display_capture_init(agent->display, &display_config)) {
        LOG_ERROR("Failed to initialize display capture");
        return false;
    }

    display_capture_set_callback(agent->display, on_frame_captured, agent);

    /* Initialize audio capture */
    if (agent->config.capture_audio) {
        agent->audio = audio_capture_create();
        if (agent->audio) {
            if (!audio_capture_init(agent->audio)) {
                LOG_WARNING("Failed to initialize audio capture (non-fatal)");
                audio_capture_destroy(agent->audio);
                agent->audio = NULL;
            } else {
                audio_capture_set_callback(agent->audio, on_audio_captured, agent);
            }
        }
    }

    /* Initialize input handler */
    agent->input = input_handler_create();
    if (!agent->input) {
        LOG_ERROR("Failed to create input handler");
        return false;
    }

    if (!input_handler_init(agent->input)) {
        LOG_ERROR("Failed to initialize input handler");
        return false;
    }

    /* Initialize clipboard manager */
    agent->clipboard = clipboard_manager_create();
    if (agent->clipboard) {
        if (!clipboard_manager_init(agent->clipboard)) {
            LOG_WARNING("Failed to initialize clipboard manager (non-fatal)");
            clipboard_manager_destroy(agent->clipboard);
            agent->clipboard = NULL;
        } else {
            clipboard_manager_set_callback(agent->clipboard, on_clipboard_changed, agent);
        }
    }

    /* Initialize SPICE agent */
    if (agent->config.spice_enabled) {
        agent->spice = spice_agent_create();
        if (agent->spice) {
            SpiceAgentConfig spice_config = {
                .virtio_port = agent->config.virtio_port
            };

            if (!spice_agent_init(agent->spice, &spice_config)) {
                LOG_WARNING("Failed to initialize SPICE agent - VirtIO port may not be available");
                spice_agent_destroy(agent->spice);
                agent->spice = NULL;
            } else {
                spice_agent_set_input_callback(agent->spice, on_input_received, agent);
            }
        }
    }

    /* Initialize WebRTC agent */
    if (agent->config.webrtc_enabled) {
        agent->webrtc = webrtc_agent_create();
        if (agent->webrtc) {
            WebRTCAgentConfig webrtc_config = {
                .signaling_url = agent->config.signaling_url
            };

            if (!webrtc_agent_init(agent->webrtc, &webrtc_config)) {
                LOG_WARNING("Failed to initialize WebRTC agent");
                webrtc_agent_destroy(agent->webrtc);
                agent->webrtc = NULL;
            } else {
                webrtc_agent_set_input_callback(agent->webrtc, on_input_received, agent);
            }
        }
    }

    LOG_INFO("Zixiao VDI Agent initialized successfully");
    return true;
}

bool agent_start(VDIAgent *agent) {
    if (!agent || agent->running) return false;

    LOG_INFO("Starting Zixiao VDI Agent...");

    agent->stopping = false;
    agent->running = true;

    /* Start display capture */
    if (agent->display && !display_capture_start(agent->display)) {
        LOG_ERROR("Failed to start display capture");
        return false;
    }

    /* Start audio capture */
    if (agent->audio) {
        audio_capture_start(agent->audio);
    }

    /* Start clipboard manager */
    if (agent->clipboard) {
        clipboard_manager_start(agent->clipboard);
    }

    /* Start SPICE agent */
    if (agent->spice) {
        spice_agent_start(agent->spice);
    }

    /* Start WebRTC agent */
    if (agent->webrtc) {
        webrtc_agent_start(agent->webrtc);
    }

    LOG_INFO("Zixiao VDI Agent started");
    return true;
}

void agent_stop(VDIAgent *agent) {
    if (!agent || !agent->running) return;

    LOG_INFO("Stopping Zixiao VDI Agent...");

    agent->stopping = true;
    agent->running = false;

    /* Stop subsystems in reverse order */
    if (agent->webrtc) {
        webrtc_agent_stop(agent->webrtc);
    }

    if (agent->spice) {
        spice_agent_stop(agent->spice);
    }

    if (agent->clipboard) {
        clipboard_manager_stop(agent->clipboard);
    }

    if (agent->audio) {
        audio_capture_stop(agent->audio);
    }

    if (agent->display) {
        display_capture_stop(agent->display);
    }

    LOG_INFO("Zixiao VDI Agent stopped");
}

void agent_run(VDIAgent *agent) {
    if (!agent) return;

    while (agent->running && !agent->stopping) {
        /* Main loop - just sleep and let subsystem threads do work */
        usleep(100000);  /* 100ms */
    }
}

/* Signal handlers */
static void signal_handler(int sig) {
    if (g_agent) {
        LOG_INFO("Received signal %d, stopping...", sig);
        agent_stop(g_agent);
    }
}

void agent_setup_signals(VDIAgent *agent) {
    g_agent = agent;

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
}

bool agent_daemonize(VDIAgent *agent) {
    if (!agent) return false;

    LOG_INFO("Daemonizing...");

    /* First fork */
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("First fork failed: %s", strerror(errno));
        return false;
    }
    if (pid > 0) {
        /* Parent exits */
        exit(0);
    }

    /* Create new session */
    if (setsid() < 0) {
        LOG_ERROR("setsid failed: %s", strerror(errno));
        return false;
    }

    /* Second fork */
    pid = fork();
    if (pid < 0) {
        LOG_ERROR("Second fork failed: %s", strerror(errno));
        return false;
    }
    if (pid > 0) {
        /* Parent exits */
        exit(0);
    }

    /* Change working directory */
    if (chdir("/") < 0) {
        LOG_WARNING("chdir failed: %s", strerror(errno));
    }

    /* Reset file mode mask */
    umask(0);

    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Redirect to /dev/null */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }

    /* Write PID file */
    if (agent->config.pid_file) {
        FILE *f = fopen(agent->config.pid_file, "w");
        if (f) {
            fprintf(f, "%d\n", getpid());
            fclose(f);
        }
    }

    LOG_INFO("Daemonized successfully, PID=%d", getpid());
    return true;
}
