/**
 * Zixiao Hypervisor - DPDK Port Implementation
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "dpdk_port.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Note: This is a stub implementation for build purposes.
 * Real implementation would include DPDK headers:
 * #include <rte_eal.h>
 * #include <rte_ethdev.h>
 * #include <rte_vhost.h>
 */

/* Internal state */
static struct {
    bool initialized;
    char eal_args[1024];
    char last_error[256];
} dpdk_state = {0};

/* Port storage */
#define MAX_DPDK_PORTS 64
static struct {
    bool in_use;
    dpdk_port_config_t config;
    dpdk_port_state_t state;
    dpdk_port_stats_t stats;
} dpdk_ports[MAX_DPDK_PORTS];

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(dpdk_state.last_error, sizeof(dpdk_state.last_error), fmt, args);
    va_end(args);
}

/* ============================================================================
 * DPDK Initialization
 * ============================================================================ */

int dpdk_init(const char *eal_args) {
    if (dpdk_state.initialized) {
        set_error("DPDK already initialized");
        return OVS_ERR_INIT;
    }

    if (eal_args) {
        strncpy(dpdk_state.eal_args, eal_args, sizeof(dpdk_state.eal_args) - 1);
    }

    /* In real implementation:
     * int argc;
     * char **argv = parse_eal_args(eal_args, &argc);
     * int ret = rte_eal_init(argc, argv);
     * if (ret < 0) {
     *     set_error("EAL initialization failed");
     *     return OVS_ERR_DPDK;
     * }
     */

    memset(dpdk_ports, 0, sizeof(dpdk_ports));
    dpdk_state.initialized = true;

    return OVS_OK;
}

void dpdk_cleanup(void) {
    if (!dpdk_state.initialized) return;

    /* Stop and destroy all ports */
    for (int i = 0; i < MAX_DPDK_PORTS; i++) {
        if (dpdk_ports[i].in_use) {
            dpdk_port_stop(i);
            dpdk_port_destroy(i);
        }
    }

    /* In real implementation:
     * rte_eal_cleanup();
     */

    memset(&dpdk_state, 0, sizeof(dpdk_state));
}

bool dpdk_is_initialized(void) {
    return dpdk_state.initialized;
}

int dpdk_get_memory_info(dpdk_memory_info_t *info) {
    if (!info) {
        return OVS_ERR_INVALID;
    }

    memset(info, 0, sizeof(*info));

    /* In real implementation:
     * struct rte_malloc_socket_stats stats;
     * for each socket...
     */

    info->total_memory = 2ULL * 1024 * 1024 * 1024;  /* Placeholder: 2GB */
    info->free_memory = 1ULL * 1024 * 1024 * 1024;
    info->socket_count = 1;
    info->channel_count = 4;

    return OVS_OK;
}

/* ============================================================================
 * Port Management
 * ============================================================================ */

int dpdk_port_create(const dpdk_port_config_t *config) {
    if (!dpdk_state.initialized) {
        set_error("DPDK not initialized");
        return OVS_ERR_NOT_INIT;
    }

    if (!config || !config->name[0]) {
        set_error("Invalid port configuration");
        return OVS_ERR_INVALID;
    }

    /* Find free slot */
    int port_id = -1;
    for (int i = 0; i < MAX_DPDK_PORTS; i++) {
        if (!dpdk_ports[i].in_use) {
            port_id = i;
            break;
        }
    }

    if (port_id < 0) {
        set_error("No free port slots");
        return OVS_ERR_MEMORY;
    }

    /* In real implementation, create device based on type:
     * - Physical: rte_eth_dev_configure()
     * - vhost-user: rte_vhost_driver_register()
     * - virtio-user: rte_eal_hotplug_add()
     */

    dpdk_ports[port_id].in_use = true;
    dpdk_ports[port_id].config = *config;
    dpdk_ports[port_id].config.port_id = port_id;
    dpdk_ports[port_id].state = DPDK_PORT_STATE_CONFIGURED;
    memset(&dpdk_ports[port_id].stats, 0, sizeof(dpdk_port_stats_t));

    return port_id;
}

int dpdk_port_destroy(uint16_t port_id) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        set_error("Invalid port ID: %u", port_id);
        return OVS_ERR_NOT_FOUND;
    }

    if (dpdk_ports[port_id].state == DPDK_PORT_STATE_STARTED) {
        dpdk_port_stop(port_id);
    }

    /* In real implementation:
     * rte_eth_dev_close(port_id);
     * or rte_vhost_driver_unregister() for vhost
     */

    dpdk_ports[port_id].in_use = false;
    memset(&dpdk_ports[port_id], 0, sizeof(dpdk_ports[0]));

    return OVS_OK;
}

int dpdk_port_start(uint16_t port_id) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (dpdk_ports[port_id].state == DPDK_PORT_STATE_STARTED) {
        return OVS_OK;  /* Already started */
    }

    /* In real implementation:
     * int ret = rte_eth_dev_start(port_id);
     */

    dpdk_ports[port_id].state = DPDK_PORT_STATE_STARTED;
    return OVS_OK;
}

int dpdk_port_stop(uint16_t port_id) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    /* In real implementation:
     * rte_eth_dev_stop(port_id);
     */

    dpdk_ports[port_id].state = DPDK_PORT_STATE_STOPPED;
    return OVS_OK;
}

int dpdk_port_get_config(uint16_t port_id, dpdk_port_config_t *config) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (!config) {
        return OVS_ERR_INVALID;
    }

    *config = dpdk_ports[port_id].config;
    return OVS_OK;
}

int dpdk_port_get_info(uint16_t port_id, dpdk_device_info_t *info) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (!info) {
        return OVS_ERR_INVALID;
    }

    memset(info, 0, sizeof(*info));
    strncpy(info->name, dpdk_ports[port_id].config.name, sizeof(info->name) - 1);
    info->port_id = port_id;
    info->state = dpdk_ports[port_id].state;
    info->max_rx_queues = 8;
    info->max_tx_queues = 8;
    info->min_mtu = 64;
    info->max_mtu = 9000;

    /* In real implementation:
     * struct rte_eth_dev_info dev_info;
     * rte_eth_dev_info_get(port_id, &dev_info);
     */

    return OVS_OK;
}

int dpdk_port_get_stats(uint16_t port_id, dpdk_port_stats_t *stats) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (!stats) {
        return OVS_ERR_INVALID;
    }

    /* In real implementation:
     * struct rte_eth_stats eth_stats;
     * rte_eth_stats_get(port_id, &eth_stats);
     */

    *stats = dpdk_ports[port_id].stats;
    return OVS_OK;
}

int dpdk_port_reset_stats(uint16_t port_id) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    /* In real implementation:
     * rte_eth_stats_reset(port_id);
     */

    memset(&dpdk_ports[port_id].stats, 0, sizeof(dpdk_port_stats_t));
    return OVS_OK;
}

int dpdk_port_set_mtu(uint16_t port_id, uint32_t mtu) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (mtu < 64 || mtu > 9000) {
        set_error("Invalid MTU: %u (valid range: 64-9000)", mtu);
        return OVS_ERR_INVALID;
    }

    /* In real implementation:
     * rte_eth_dev_set_mtu(port_id, mtu);
     */

    dpdk_ports[port_id].config.mtu = mtu;
    return OVS_OK;
}

int dpdk_port_set_promisc(uint16_t port_id, bool enable) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    /* In real implementation:
     * if (enable)
     *     rte_eth_promiscuous_enable(port_id);
     * else
     *     rte_eth_promiscuous_disable(port_id);
     */

    (void)enable;
    return OVS_OK;
}

uint16_t dpdk_port_count(void) {
    uint16_t count = 0;
    for (int i = 0; i < MAX_DPDK_PORTS; i++) {
        if (dpdk_ports[i].in_use) {
            count++;
        }
    }
    return count;
}

int dpdk_port_list(uint16_t *ports, uint32_t max_ports) {
    if (!ports) {
        return OVS_ERR_INVALID;
    }

    uint32_t count = 0;
    for (int i = 0; i < MAX_DPDK_PORTS && count < max_ports; i++) {
        if (dpdk_ports[i].in_use) {
            ports[count++] = i;
        }
    }

    return (int)count;
}

/* ============================================================================
 * vhost-user Management
 * ============================================================================ */

int dpdk_vhost_create(const dpdk_vhost_config_t *config) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!config || !config->socket_path[0]) {
        set_error("Invalid vhost configuration");
        return OVS_ERR_INVALID;
    }

    /* Create as a DPDK port */
    dpdk_port_config_t port_config = {0};
    snprintf(port_config.name, sizeof(port_config.name), "vhost_%s",
             config->socket_path);
    port_config.type = DPDK_DEV_VHOST_USER;
    port_config.rx_queues = config->queues > 0 ? config->queues : 1;
    port_config.tx_queues = config->queues > 0 ? config->queues : 1;
    port_config.vhost = *config;

    return dpdk_port_create(&port_config);
}

int dpdk_vhost_destroy(uint16_t port_id) {
    return dpdk_port_destroy(port_id);
}

bool dpdk_vhost_is_connected(uint16_t port_id) {
    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return false;
    }

    if (dpdk_ports[port_id].config.type != DPDK_DEV_VHOST_USER) {
        return false;
    }

    /* In real implementation:
     * Check vhost connection state
     */

    return dpdk_ports[port_id].state == DPDK_PORT_STATE_STARTED;
}

int dpdk_vhost_get_socket_path(uint16_t port_id, char *path, size_t path_len) {
    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (!path || path_len == 0) {
        return OVS_ERR_INVALID;
    }

    strncpy(path, dpdk_ports[port_id].config.vhost.socket_path, path_len - 1);
    path[path_len - 1] = '\0';

    return OVS_OK;
}

/* ============================================================================
 * Queue Management
 * ============================================================================ */

int dpdk_queue_get_stats(uint16_t port_id, uint16_t queue_id, bool rx,
                          uint64_t *packets, uint64_t *bytes) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (queue_id >= 8) {
        return OVS_ERR_INVALID;
    }

    dpdk_port_stats_t *stats = &dpdk_ports[port_id].stats;

    if (packets) {
        *packets = rx ? stats->q_ipackets[queue_id] : stats->q_opackets[queue_id];
    }
    if (bytes) {
        *bytes = rx ? stats->q_ibytes[queue_id] : stats->q_obytes[queue_id];
    }

    return OVS_OK;
}

int dpdk_queue_count(uint16_t port_id, uint16_t *rx_queues, uint16_t *tx_queues) {
    if (!dpdk_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (port_id >= MAX_DPDK_PORTS || !dpdk_ports[port_id].in_use) {
        return OVS_ERR_NOT_FOUND;
    }

    if (rx_queues) {
        *rx_queues = dpdk_ports[port_id].config.rx_queues;
    }
    if (tx_queues) {
        *tx_queues = dpdk_ports[port_id].config.tx_queues;
    }

    return OVS_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *dpdk_dev_type_name(dpdk_dev_type_t type) {
    switch (type) {
        case DPDK_DEV_PHYSICAL:   return "physical";
        case DPDK_DEV_VHOST_USER: return "vhost-user";
        case DPDK_DEV_VIRTIO:     return "virtio-user";
        case DPDK_DEV_RING:       return "ring";
        case DPDK_DEV_AF_PACKET:  return "af_packet";
        case DPDK_DEV_TAP:        return "tap";
        default:                  return "unknown";
    }
}

const char *dpdk_port_state_name(dpdk_port_state_t state) {
    switch (state) {
        case DPDK_PORT_STATE_STOPPED:    return "stopped";
        case DPDK_PORT_STATE_STARTED:    return "started";
        case DPDK_PORT_STATE_CONFIGURED: return "configured";
        case DPDK_PORT_STATE_ERROR:      return "error";
        default:                         return "unknown";
    }
}

const char *dpdk_get_last_error(void) {
    return dpdk_state.last_error[0] ? dpdk_state.last_error : "No error";
}

char *dpdk_format_mac(const uint8_t *mac, char *buf) {
    if (!mac || !buf) return NULL;

    snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return buf;
}
