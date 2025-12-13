/**
 * Zixiao Hypervisor - vhost-user Integration
 *
 * Provides vhost-user socket management for QEMU/KVM VMs.
 * Enables direct VM-to-DPDK datapath for high-performance networking.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "dpdk_port.h"
#include "ovs_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

/* vhost-user socket directory */
#define VHOST_SOCKET_DIR "/var/run/zixiao/vhost"

/* VM connection tracking */
#define MAX_VM_CONNECTIONS 256

typedef struct {
    char vm_id[64];             /* VM identifier */
    char socket_path[256];      /* vhost-user socket path */
    uint16_t dpdk_port_id;      /* Associated DPDK port */
    char bridge[64];            /* OVS bridge name */
    char ovs_port[64];          /* OVS port name */
    bool connected;             /* VM connection status */
    uint64_t rx_packets;        /* Received packets */
    uint64_t tx_packets;        /* Transmitted packets */
    uint64_t rx_bytes;          /* Received bytes */
    uint64_t tx_bytes;          /* Transmitted bytes */
} vm_vhost_connection_t;

static vm_vhost_connection_t vm_connections[MAX_VM_CONNECTIONS];
static uint32_t vm_connection_count = 0;

static char vhost_last_error[256] = {0};

static void set_vhost_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(vhost_last_error, sizeof(vhost_last_error), fmt, args);
    va_end(args);
}

/* ============================================================================
 * Socket Directory Management
 * ============================================================================ */

/**
 * Ensure vhost socket directory exists
 */
static int ensure_socket_dir(void) {
    struct stat st;

    if (stat(VHOST_SOCKET_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        set_vhost_error("Socket path exists but is not a directory: %s",
                        VHOST_SOCKET_DIR);
        return -1;
    }

    /* Create directory recursively */
    char path[256];
    char *p = path;
    strncpy(path, VHOST_SOCKET_DIR, sizeof(path) - 1);

    while (*p) {
        if (*p == '/' && p != path) {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                set_vhost_error("Failed to create directory %s: %s",
                                path, strerror(errno));
                return -1;
            }
            *p = '/';
        }
        p++;
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        set_vhost_error("Failed to create directory %s: %s",
                        path, strerror(errno));
        return -1;
    }

    return 0;
}

/* ============================================================================
 * VM vhost-user Connection Management
 * ============================================================================ */

/**
 * Create vhost-user port for a VM
 *
 * @param vm_id Unique VM identifier
 * @param bridge OVS bridge to attach to
 * @param queues Number of queue pairs (0 for default)
 * @param socket_path Output socket path
 * @param path_len Socket path buffer length
 * @return 0 on success, negative error code on failure
 */
int vhost_create_vm_port(const char *vm_id, const char *bridge,
                          uint16_t queues, char *socket_path, size_t path_len) {
    if (!vm_id || !bridge) {
        set_vhost_error("Invalid parameters");
        return OVS_ERR_INVALID;
    }

    /* Check for existing connection */
    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            if (socket_path && path_len > 0) {
                strncpy(socket_path, vm_connections[i].socket_path, path_len - 1);
            }
            return OVS_OK;  /* Already exists */
        }
    }

    if (vm_connection_count >= MAX_VM_CONNECTIONS) {
        set_vhost_error("Maximum VM connections reached");
        return OVS_ERR_MEMORY;
    }

    /* Ensure socket directory exists */
    if (ensure_socket_dir() != 0) {
        return OVS_ERR_INIT;
    }

    /* Generate socket path */
    char sock_path[256];
    snprintf(sock_path, sizeof(sock_path), "%s/%s.sock", VHOST_SOCKET_DIR, vm_id);

    /* Create DPDK vhost-user port */
    dpdk_vhost_config_t vhost_config = {
        .server_mode = true,
        .reconnect = true,
        .reconnect_time = 1000,
        .queues = queues > 0 ? queues : 1,
        .linear_buffers = false,
        .packed_ring = false
    };
    strncpy(vhost_config.socket_path, sock_path, sizeof(vhost_config.socket_path) - 1);

    int port_id = dpdk_vhost_create(&vhost_config);
    if (port_id < 0) {
        set_vhost_error("Failed to create DPDK vhost port: %s", dpdk_get_last_error());
        return port_id;
    }

    /* Generate OVS port name */
    char ovs_port_name[64];
    snprintf(ovs_port_name, sizeof(ovs_port_name), "vhost-%s", vm_id);

    /* Add port to OVS bridge */
    ovs_port_config_t ovs_config = {
        .type = OVS_PORT_DPDKVHOSTUSER
    };
    strncpy(ovs_config.name, ovs_port_name, sizeof(ovs_config.name) - 1);
    strncpy(ovs_config.bridge, bridge, sizeof(ovs_config.bridge) - 1);
    strncpy(ovs_config.vhost.socket_path, sock_path, sizeof(ovs_config.vhost.socket_path) - 1);
    ovs_config.vhost.server_mode = true;

    int ret = ovs_port_add(&ovs_config);
    if (ret != OVS_OK) {
        dpdk_vhost_destroy(port_id);
        set_vhost_error("Failed to add OVS port: %s", ovs_get_last_error());
        return ret;
    }

    /* Store connection info */
    vm_vhost_connection_t *conn = &vm_connections[vm_connection_count++];
    strncpy(conn->vm_id, vm_id, sizeof(conn->vm_id) - 1);
    strncpy(conn->socket_path, sock_path, sizeof(conn->socket_path) - 1);
    strncpy(conn->bridge, bridge, sizeof(conn->bridge) - 1);
    strncpy(conn->ovs_port, ovs_port_name, sizeof(conn->ovs_port) - 1);
    conn->dpdk_port_id = port_id;
    conn->connected = false;

    /* Start the DPDK port */
    dpdk_port_start(port_id);

    if (socket_path && path_len > 0) {
        strncpy(socket_path, sock_path, path_len - 1);
    }

    return OVS_OK;
}

/**
 * Destroy vhost-user port for a VM
 *
 * @param vm_id VM identifier
 * @return 0 on success
 */
int vhost_destroy_vm_port(const char *vm_id) {
    if (!vm_id) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            vm_vhost_connection_t *conn = &vm_connections[i];

            /* Remove OVS port */
            ovs_port_delete(conn->bridge, conn->ovs_port);

            /* Destroy DPDK port */
            dpdk_vhost_destroy(conn->dpdk_port_id);

            /* Remove socket file */
            unlink(conn->socket_path);

            /* Remove from list */
            memmove(&vm_connections[i], &vm_connections[i + 1],
                    (vm_connection_count - i - 1) * sizeof(vm_vhost_connection_t));
            vm_connection_count--;

            return OVS_OK;
        }
    }

    set_vhost_error("VM connection not found: %s", vm_id);
    return OVS_ERR_NOT_FOUND;
}

/**
 * Get vhost-user socket path for a VM
 *
 * @param vm_id VM identifier
 * @param socket_path Output buffer
 * @param path_len Buffer length
 * @return 0 on success
 */
int vhost_get_socket_path(const char *vm_id, char *socket_path, size_t path_len) {
    if (!vm_id || !socket_path || path_len == 0) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            strncpy(socket_path, vm_connections[i].socket_path, path_len - 1);
            socket_path[path_len - 1] = '\0';
            return OVS_OK;
        }
    }

    return OVS_ERR_NOT_FOUND;
}

/**
 * Check if VM is connected to vhost-user socket
 *
 * @param vm_id VM identifier
 * @return true if connected
 */
bool vhost_is_vm_connected(const char *vm_id) {
    if (!vm_id) return false;

    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            /* Check DPDK port connection status */
            return dpdk_vhost_is_connected(vm_connections[i].dpdk_port_id);
        }
    }

    return false;
}

/**
 * Get VM connection statistics
 *
 * @param vm_id VM identifier
 * @param rx_packets Output RX packets
 * @param tx_packets Output TX packets
 * @param rx_bytes Output RX bytes
 * @param tx_bytes Output TX bytes
 * @return 0 on success
 */
int vhost_get_vm_stats(const char *vm_id,
                        uint64_t *rx_packets, uint64_t *tx_packets,
                        uint64_t *rx_bytes, uint64_t *tx_bytes) {
    if (!vm_id) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            dpdk_port_stats_t stats;
            if (dpdk_port_get_stats(vm_connections[i].dpdk_port_id, &stats) == OVS_OK) {
                if (rx_packets) *rx_packets = stats.ipackets;
                if (tx_packets) *tx_packets = stats.opackets;
                if (rx_bytes) *rx_bytes = stats.ibytes;
                if (tx_bytes) *tx_bytes = stats.obytes;
                return OVS_OK;
            }
            return OVS_ERR_DPDK;
        }
    }

    return OVS_ERR_NOT_FOUND;
}

/**
 * List all VM vhost-user connections
 *
 * @param vm_ids Output array of VM IDs
 * @param max_vms Maximum VMs to return
 * @return Number of VMs
 */
int vhost_list_vm_connections(char (*vm_ids)[64], uint32_t max_vms) {
    if (!vm_ids) {
        return OVS_ERR_INVALID;
    }

    uint32_t count = vm_connection_count < max_vms ? vm_connection_count : max_vms;
    for (uint32_t i = 0; i < count; i++) {
        strncpy(vm_ids[i], vm_connections[i].vm_id, 63);
        vm_ids[i][63] = '\0';
    }

    return (int)count;
}

/**
 * Set VLAN tag for VM port
 *
 * @param vm_id VM identifier
 * @param vlan_id VLAN ID (0 to clear)
 * @return 0 on success
 */
int vhost_set_vm_vlan(const char *vm_id, uint16_t vlan_id) {
    if (!vm_id) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < vm_connection_count; i++) {
        if (strcmp(vm_connections[i].vm_id, vm_id) == 0) {
            return ovs_port_set_vlan(vm_connections[i].bridge,
                                     vm_connections[i].ovs_port, vlan_id);
        }
    }

    return OVS_ERR_NOT_FOUND;
}

/**
 * Get last vhost error message
 *
 * @return Static error string
 */
const char *vhost_get_last_error(void) {
    return vhost_last_error[0] ? vhost_last_error : "No error";
}

/* ============================================================================
 * QEMU Command Line Helpers
 * ============================================================================ */

/**
 * Generate QEMU vhost-user netdev argument
 *
 * @param vm_id VM identifier
 * @param netdev_id QEMU netdev ID
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return 0 on success
 */
int vhost_qemu_netdev_arg(const char *vm_id, const char *netdev_id,
                           char *buf, size_t buf_len) {
    if (!vm_id || !netdev_id || !buf || buf_len == 0) {
        return OVS_ERR_INVALID;
    }

    char socket_path[256];
    int ret = vhost_get_socket_path(vm_id, socket_path, sizeof(socket_path));
    if (ret != OVS_OK) {
        return ret;
    }

    snprintf(buf, buf_len,
             "-netdev type=vhost-user,id=%s,chardev=char%s,vhostforce=on "
             "-chardev socket,id=char%s,path=%s",
             netdev_id, netdev_id, netdev_id, socket_path);

    return OVS_OK;
}

/**
 * Generate QEMU device argument for virtio-net
 *
 * @param netdev_id QEMU netdev ID
 * @param mac MAC address (NULL for auto)
 * @param queues Number of queues (0 for single queue)
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return 0 on success
 */
int vhost_qemu_device_arg(const char *netdev_id, const char *mac,
                           uint16_t queues, char *buf, size_t buf_len) {
    if (!netdev_id || !buf || buf_len == 0) {
        return OVS_ERR_INVALID;
    }

    if (queues > 1) {
        snprintf(buf, buf_len,
                 "-device virtio-net-pci,netdev=%s%s%s,mq=on,vectors=%u",
                 netdev_id,
                 mac ? ",mac=" : "",
                 mac ? mac : "",
                 2 * queues + 2);
    } else {
        snprintf(buf, buf_len,
                 "-device virtio-net-pci,netdev=%s%s%s",
                 netdev_id,
                 mac ? ",mac=" : "",
                 mac ? mac : "");
    }

    return OVS_OK;
}
