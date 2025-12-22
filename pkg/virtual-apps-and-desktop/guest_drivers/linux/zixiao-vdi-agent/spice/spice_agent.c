/*
 * Zixiao VDI Agent for Linux - SPICE Agent Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements SPICE vdagent protocol over VirtIO-Serial.
 */

#include "spice_agent.h"
#include <fcntl.h>
#include <poll.h>

/* SPICE vdagent message types */
#define VD_AGENT_MOUSE_STATE             1
#define VD_AGENT_MONITORS_CONFIG         2
#define VD_AGENT_REPLY                   3
#define VD_AGENT_CLIPBOARD               4
#define VD_AGENT_DISPLAY_CONFIG          5
#define VD_AGENT_ANNOUNCE_CAPABILITIES   6
#define VD_AGENT_CLIPBOARD_GRAB          7
#define VD_AGENT_CLIPBOARD_REQUEST       8
#define VD_AGENT_CLIPBOARD_RELEASE       9

/* Clipboard types */
#define VD_AGENT_CLIPBOARD_NONE          0
#define VD_AGENT_CLIPBOARD_UTF8_TEXT     1
#define VD_AGENT_CLIPBOARD_IMAGE_PNG     2
#define VD_AGENT_CLIPBOARD_IMAGE_BMP     3

/* Capabilities */
#define VD_AGENT_CAP_MOUSE_STATE         (1 << 0)
#define VD_AGENT_CAP_MONITORS_CONFIG     (1 << 1)
#define VD_AGENT_CAP_REPLY               (1 << 2)
#define VD_AGENT_CAP_CLIPBOARD           (1 << 3)
#define VD_AGENT_CAP_DISPLAY_CONFIG      (1 << 4)
#define VD_AGENT_CAP_CLIPBOARD_BY_DEMAND (1 << 5)

#define VD_AGENT_PORT                    1

#pragma pack(push, 1)
typedef struct {
    uint32_t port;
    uint32_t size;
} VDAgentHeader;

typedef struct {
    uint32_t type;
    uint32_t opaque;
    uint32_t size;
} VDAgentMessage;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t buttons;
    uint8_t  display_id;
} VDAgentMouseState;

typedef struct {
    uint32_t num_monitors;
    uint32_t flags;
} VDAgentMonitorsConfig;

typedef struct {
    uint32_t height;
    uint32_t width;
    int32_t  depth;
    int32_t  x;
    int32_t  y;
} VDAgentMonitor;

typedef struct {
    uint32_t request;
    uint32_t caps[1];
} VDAgentAnnounceCapabilities;

typedef struct {
    uint32_t type;
} VDAgentClipboard;
#pragma pack(pop)

struct SpiceAgent {
    /* Configuration */
    SpiceAgentConfig config;
    InputCallback    input_callback;
    void            *input_callback_data;

    /* State */
    bool             running;
    bool             stopping;
    pthread_t        read_thread;

    /* VirtIO-Serial */
    int              fd;
    char             port_path[256];

    /* Capabilities */
    uint32_t         host_caps;
    uint32_t         guest_caps;
};

static void *read_thread_func(void *arg);
static bool send_message(SpiceAgent *sa, uint32_t type, const void *data, size_t size);
static void process_message(SpiceAgent *sa, const uint8_t *data, size_t size);

SpiceAgent *spice_agent_create(void) {
    SpiceAgent *sa = zixiao_calloc(1, sizeof(SpiceAgent));
    if (!sa) return NULL;

    sa->fd = -1;
    sa->running = false;
    sa->stopping = false;

    return sa;
}

void spice_agent_destroy(SpiceAgent *sa) {
    if (!sa) return;

    spice_agent_shutdown(sa);
    zixiao_free(sa);
}

bool spice_agent_init(SpiceAgent *sa, const SpiceAgentConfig *config) {
    if (!sa) return false;

    LOG_INFO("Initializing SPICE agent...");

    if (config) {
        sa->config = *config;
    }

    /* Default port paths to try */
    const char *ports[] = {
        config && config->virtio_port ? config->virtio_port : NULL,
        "/dev/virtio-ports/org.zixiao.vdi.0",
        "/dev/virtio-ports/com.redhat.spice.0",
        "/dev/vport0p1",
        NULL
    };

    /* Try to open VirtIO-Serial port */
    for (int i = 0; ports[i]; i++) {
        sa->fd = open(ports[i], O_RDWR | O_NONBLOCK);
        if (sa->fd >= 0) {
            strncpy(sa->port_path, ports[i], sizeof(sa->port_path) - 1);
            LOG_INFO("Opened VirtIO-Serial port: %s", ports[i]);
            break;
        }
    }

    if (sa->fd < 0) {
        LOG_ERROR("Failed to open VirtIO-Serial port");
        return false;
    }

    /* Set guest capabilities */
    sa->guest_caps =
        VD_AGENT_CAP_MOUSE_STATE |
        VD_AGENT_CAP_MONITORS_CONFIG |
        VD_AGENT_CAP_REPLY |
        VD_AGENT_CAP_CLIPBOARD |
        VD_AGENT_CAP_CLIPBOARD_BY_DEMAND;

    LOG_INFO("SPICE agent initialized");
    return true;
}

void spice_agent_shutdown(SpiceAgent *sa) {
    if (!sa) return;

    spice_agent_stop(sa);

    if (sa->fd >= 0) {
        close(sa->fd);
        sa->fd = -1;
    }

    LOG_INFO("SPICE agent shutdown");
}

bool spice_agent_start(SpiceAgent *sa) {
    if (!sa || sa->fd < 0 || sa->running) return false;

    LOG_INFO("Starting SPICE agent...");

    sa->stopping = false;
    sa->running = true;

    /* Send capabilities announcement */
    VDAgentAnnounceCapabilities caps = {0};
    caps.request = 1;
    caps.caps[0] = sa->guest_caps;
    send_message(sa, VD_AGENT_ANNOUNCE_CAPABILITIES, &caps, sizeof(caps));

    /* Start read thread */
    if (pthread_create(&sa->read_thread, NULL, read_thread_func, sa) != 0) {
        LOG_ERROR("Failed to create SPICE read thread");
        sa->running = false;
        return false;
    }

    LOG_INFO("SPICE agent started");
    return true;
}

void spice_agent_stop(SpiceAgent *sa) {
    if (!sa || !sa->running) return;

    LOG_INFO("Stopping SPICE agent...");

    sa->stopping = true;
    sa->running = false;

    pthread_join(sa->read_thread, NULL);

    LOG_INFO("SPICE agent stopped");
}

bool spice_agent_is_running(SpiceAgent *sa) {
    return sa && sa->running;
}

void spice_agent_set_input_callback(SpiceAgent *sa, InputCallback callback, void *user_data) {
    if (!sa) return;
    sa->input_callback = callback;
    sa->input_callback_data = user_data;
}

static bool send_message(SpiceAgent *sa, uint32_t type, const void *data, size_t size) {
    if (!sa || sa->fd < 0) return false;

    size_t total_size = sizeof(VDAgentHeader) + sizeof(VDAgentMessage) + size;
    uint8_t *buf = zixiao_malloc(total_size);
    if (!buf) return false;

    VDAgentHeader *header = (VDAgentHeader *)buf;
    header->port = VD_AGENT_PORT;
    header->size = sizeof(VDAgentMessage) + size;

    VDAgentMessage *msg = (VDAgentMessage *)(buf + sizeof(VDAgentHeader));
    msg->type = type;
    msg->opaque = 0;
    msg->size = size;

    if (size > 0 && data) {
        memcpy(buf + sizeof(VDAgentHeader) + sizeof(VDAgentMessage), data, size);
    }

    ssize_t written = write(sa->fd, buf, total_size);
    zixiao_free(buf);

    return written == (ssize_t)total_size;
}

static void process_message(SpiceAgent *sa, const uint8_t *data, size_t size) {
    if (size < sizeof(VDAgentMessage)) return;

    const VDAgentMessage *msg = (const VDAgentMessage *)data;
    const uint8_t *payload = data + sizeof(VDAgentMessage);

    LOG_DEBUG("SPICE message type=%u size=%u", msg->type, msg->size);

    switch (msg->type) {
        case VD_AGENT_MOUSE_STATE: {
            if (msg->size >= sizeof(VDAgentMouseState)) {
                const VDAgentMouseState *mouse = (const VDAgentMouseState *)payload;
                if (sa->input_callback) {
                    InputEvent event = {0};
                    event.type = INPUT_EVENT_MOUSE_MOVE;
                    event.mouse.x = mouse->x;
                    event.mouse.y = mouse->y;
                    event.mouse.button = mouse->buttons;
                    event.timestamp = get_timestamp_ms();
                    sa->input_callback(&event, sa->input_callback_data);
                }
            }
            break;
        }

        case VD_AGENT_ANNOUNCE_CAPABILITIES: {
            if (msg->size >= sizeof(VDAgentAnnounceCapabilities)) {
                const VDAgentAnnounceCapabilities *caps =
                    (const VDAgentAnnounceCapabilities *)payload;
                sa->host_caps = caps->caps[0];
                LOG_INFO("Host capabilities: 0x%08X", sa->host_caps);

                if (caps->request) {
                    VDAgentAnnounceCapabilities resp = {0};
                    resp.request = 0;
                    resp.caps[0] = sa->guest_caps;
                    send_message(sa, VD_AGENT_ANNOUNCE_CAPABILITIES, &resp, sizeof(resp));
                }
            }
            break;
        }

        case VD_AGENT_CLIPBOARD_GRAB:
            LOG_DEBUG("Clipboard grab from host");
            break;

        case VD_AGENT_CLIPBOARD_REQUEST:
            LOG_DEBUG("Clipboard request from host");
            break;

        default:
            LOG_DEBUG("Unhandled SPICE message type: %u", msg->type);
            break;
    }
}

static void *read_thread_func(void *arg) {
    SpiceAgent *sa = (SpiceAgent *)arg;
    uint8_t buffer[4096];
    uint8_t msg_buffer[65536];
    size_t msg_offset = 0;

    LOG_DEBUG("SPICE read thread started");

    struct pollfd pfd = {
        .fd = sa->fd,
        .events = POLLIN
    };

    while (!sa->stopping) {
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) {
            if (errno != EINTR) {
                LOG_ERROR("poll failed: %s", strerror(errno));
                break;
            }
            continue;
        }

        if (ret == 0) continue;  /* Timeout */

        if (pfd.revents & POLLIN) {
            ssize_t n = read(sa->fd, buffer, sizeof(buffer));
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_ERROR("read failed: %s", strerror(errno));
                    break;
                }
                continue;
            }

            if (n == 0) {
                LOG_INFO("VirtIO port closed");
                break;
            }

            /* Append to message buffer */
            if (msg_offset + n <= sizeof(msg_buffer)) {
                memcpy(msg_buffer + msg_offset, buffer, n);
                msg_offset += n;
            }

            /* Process complete messages */
            while (msg_offset >= sizeof(VDAgentHeader)) {
                VDAgentHeader *header = (VDAgentHeader *)msg_buffer;
                size_t total_size = sizeof(VDAgentHeader) + header->size;

                if (msg_offset < total_size) break;

                process_message(sa, msg_buffer + sizeof(VDAgentHeader), header->size);

                memmove(msg_buffer, msg_buffer + total_size, msg_offset - total_size);
                msg_offset -= total_size;
            }
        }
    }

    LOG_DEBUG("SPICE read thread exiting");
    return NULL;
}

bool spice_agent_send_frame(SpiceAgent *sa, const FrameData *frame) {
    (void)sa;
    (void)frame;
    /* SPICE display is handled by QXL driver, not vdagent */
    return true;
}

bool spice_agent_send_clipboard(SpiceAgent *sa, const ClipboardData *data) {
    if (!sa || !data || data->format == CLIPBOARD_FORMAT_NONE) return false;

    /* First, grab clipboard */
    uint32_t types[2] = {0};
    if (data->format == CLIPBOARD_FORMAT_UTF8 || data->format == CLIPBOARD_FORMAT_TEXT) {
        types[0] = VD_AGENT_CLIPBOARD_UTF8_TEXT;
    } else if (data->format == CLIPBOARD_FORMAT_IMAGE_PNG) {
        types[0] = VD_AGENT_CLIPBOARD_IMAGE_PNG;
    } else {
        return false;
    }

    send_message(sa, VD_AGENT_CLIPBOARD_GRAB, types, sizeof(uint32_t) * 2);

    /* Then send data */
    size_t clip_size = sizeof(VDAgentClipboard) + data->data_size;
    uint8_t *buf = zixiao_malloc(clip_size);
    if (!buf) return false;

    VDAgentClipboard *clip = (VDAgentClipboard *)buf;
    clip->type = types[0];
    memcpy(buf + sizeof(VDAgentClipboard), data->data, data->data_size);

    bool result = send_message(sa, VD_AGENT_CLIPBOARD, buf, clip_size);
    zixiao_free(buf);

    return result;
}

bool spice_agent_send_monitor_config(SpiceAgent *sa, const MonitorInfo *monitors, int count) {
    if (!sa || !monitors || count <= 0) return false;

    size_t size = sizeof(VDAgentMonitorsConfig) + count * sizeof(VDAgentMonitor);
    uint8_t *buf = zixiao_malloc(size);
    if (!buf) return false;

    VDAgentMonitorsConfig *config = (VDAgentMonitorsConfig *)buf;
    config->num_monitors = count;
    config->flags = 0;

    VDAgentMonitor *mons = (VDAgentMonitor *)(buf + sizeof(VDAgentMonitorsConfig));
    for (int i = 0; i < count; i++) {
        mons[i].width = monitors[i].width;
        mons[i].height = monitors[i].height;
        mons[i].depth = monitors[i].depth;
        mons[i].x = monitors[i].x;
        mons[i].y = monitors[i].y;
    }

    bool result = send_message(sa, VD_AGENT_MONITORS_CONFIG, buf, size);
    zixiao_free(buf);

    return result;
}
