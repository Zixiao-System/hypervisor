/*
 * Zixiao VDI Agent for Linux - Main Entry Point
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "agent.h"
#include <getopt.h>

void zixiao_log_init(LogLevel min_level, bool use_syslog, const char *log_file);
void zixiao_log_shutdown(void);

static void print_version(void) {
    printf("Zixiao VDI Agent v%d.%d.%d\n",
           ZIXIAO_VDI_VERSION_MAJOR,
           ZIXIAO_VDI_VERSION_MINOR,
           ZIXIAO_VDI_VERSION_PATCH);
    printf("Copyright (c) 2025 Zixiao System\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -d, --daemon           Run as daemon\n");
    printf("  -p, --pid-file FILE    PID file path (default: /var/run/zixiao-vdi-agent.pid)\n");
    printf("  -l, --log-file FILE    Log file path\n");
    printf("  -v, --verbose          Enable debug logging\n");
    printf("  -q, --quiet            Quiet mode (errors only)\n");
    printf("  --no-spice             Disable SPICE agent\n");
    printf("  --no-webrtc            Disable WebRTC agent\n");
    printf("  --no-audio             Disable audio capture\n");
    printf("  --virtio-port PATH     VirtIO serial port (default: /dev/virtio-ports/org.zixiao.vdi.0)\n");
    printf("  --signaling-url URL    WebRTC signaling server URL\n");
    printf("  --fps N                Target FPS (default: 30)\n");
    printf("  -h, --help             Show this help\n");
    printf("  -V, --version          Show version\n");
}

static struct option long_options[] = {
    {"daemon",       no_argument,       NULL, 'd'},
    {"pid-file",     required_argument, NULL, 'p'},
    {"log-file",     required_argument, NULL, 'l'},
    {"verbose",      no_argument,       NULL, 'v'},
    {"quiet",        no_argument,       NULL, 'q'},
    {"no-spice",     no_argument,       NULL, 1000},
    {"no-webrtc",    no_argument,       NULL, 1001},
    {"no-audio",     no_argument,       NULL, 1002},
    {"virtio-port",  required_argument, NULL, 1003},
    {"signaling-url",required_argument, NULL, 1004},
    {"fps",          required_argument, NULL, 1005},
    {"help",         no_argument,       NULL, 'h'},
    {"version",      no_argument,       NULL, 'V'},
    {NULL,           0,                 NULL, 0}
};

int main(int argc, char *argv[]) {
    AgentConfig config;
    agent_config_default(&config);

    int opt;
    while ((opt = getopt_long(argc, argv, "dp:l:vqhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                config.daemonize = true;
                break;
            case 'p':
                config.pid_file = optarg;
                break;
            case 'l':
                config.log_file = optarg;
                break;
            case 'v':
                config.log_level = LOG_LEVEL_DEBUG;
                break;
            case 'q':
                config.log_level = LOG_LEVEL_ERROR;
                break;
            case 1000:  /* --no-spice */
                config.spice_enabled = false;
                break;
            case 1001:  /* --no-webrtc */
                config.webrtc_enabled = false;
                break;
            case 1002:  /* --no-audio */
                config.capture_audio = false;
                break;
            case 1003:  /* --virtio-port */
                config.virtio_port = optarg;
                break;
            case 1004:  /* --signaling-url */
                config.signaling_url = optarg;
                break;
            case 1005:  /* --fps */
                config.target_fps = (uint32_t)atoi(optarg);
                if (config.target_fps < 1) config.target_fps = 1;
                if (config.target_fps > 60) config.target_fps = 60;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'V':
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Initialize logging */
    zixiao_log_init(config.log_level, config.daemonize, config.log_file);

    LOG_INFO("Zixiao VDI Agent starting...");

    /* Create agent */
    VDIAgent *agent = agent_create(&config);
    if (!agent) {
        LOG_FATAL("Failed to create agent");
        zixiao_log_shutdown();
        return 1;
    }

    /* Daemonize if requested */
    if (config.daemonize) {
        if (!agent_daemonize(agent)) {
            LOG_FATAL("Failed to daemonize");
            agent_destroy(agent);
            zixiao_log_shutdown();
            return 1;
        }
    }

    /* Setup signal handlers */
    agent_setup_signals(agent);

    /* Initialize agent */
    if (!agent_init(agent)) {
        LOG_FATAL("Failed to initialize agent");
        agent_destroy(agent);
        zixiao_log_shutdown();
        return 1;
    }

    /* Start agent */
    if (!agent_start(agent)) {
        LOG_FATAL("Failed to start agent");
        agent_destroy(agent);
        zixiao_log_shutdown();
        return 1;
    }

    /* Run main loop */
    agent_run(agent);

    /* Cleanup */
    agent_destroy(agent);
    zixiao_log_shutdown();

    LOG_INFO("Zixiao VDI Agent exited");
    return 0;
}
