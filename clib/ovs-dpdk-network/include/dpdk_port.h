/**
 * Zixiao Hypervisor - DPDK Port Management
 *
 * High-performance networking using DPDK for VM networking.
 * Provides vhost-user support for direct VM-to-NIC datapath.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#ifndef ZIXIAO_DPDK_PORT_H
#define ZIXIAO_DPDK_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (shared with ovs_bridge.h) */
#ifndef OVS_OK
#define OVS_OK                  0
#define OVS_ERR_INIT           -1
#define OVS_ERR_NOT_INIT       -2
#define OVS_ERR_MEMORY         -3
#define OVS_ERR_INVALID        -4
#define OVS_ERR_NOT_FOUND      -5
#define OVS_ERR_EXISTS         -6
#define OVS_ERR_DPDK           -9
#endif

/* DPDK port state */
typedef enum {
    DPDK_PORT_STATE_STOPPED = 0,
    DPDK_PORT_STATE_STARTED,
    DPDK_PORT_STATE_CONFIGURED,
    DPDK_PORT_STATE_ERROR
} dpdk_port_state_t;

/* DPDK device type */
typedef enum {
    DPDK_DEV_PHYSICAL = 0,      /* Physical NIC */
    DPDK_DEV_VHOST_USER,        /* vhost-user (VM) */
    DPDK_DEV_VIRTIO,            /* virtio-user */
    DPDK_DEV_RING,              /* Ring PMD */
    DPDK_DEV_AF_PACKET,         /* AF_PACKET */
    DPDK_DEV_TAP                /* TAP PMD */
} dpdk_dev_type_t;

/* vhost-user configuration */
typedef struct {
    char socket_path[256];      /* Unix socket path */
    bool server_mode;           /* true = create socket, false = connect */
    bool reconnect;             /* Auto-reconnect on disconnect */
    uint32_t reconnect_time;    /* Reconnect interval (ms) */
    uint16_t queues;            /* Number of queue pairs */
    bool linear_buffers;        /* Use linear buffers only */
    bool packed_ring;           /* Enable packed virtqueue */
} dpdk_vhost_config_t;

/* DPDK port configuration */
typedef struct {
    char name[64];              /* Port name */
    dpdk_dev_type_t type;       /* Device type */
    uint16_t port_id;           /* DPDK port ID */

    /* Device arguments */
    char pci_addr[32];          /* PCI address (for physical NICs) */
    char devargs[256];          /* Additional device arguments */

    /* Queue configuration */
    uint16_t rx_queues;         /* Number of RX queues */
    uint16_t tx_queues;         /* Number of TX queues */
    uint16_t rx_desc;           /* RX descriptors per queue */
    uint16_t tx_desc;           /* TX descriptors per queue */

    /* Buffer configuration */
    uint32_t mtu;               /* MTU size */
    uint16_t mbuf_size;         /* mbuf size */
    uint32_t mempool_size;      /* Memory pool size */

    /* RSS configuration */
    bool rss_enabled;           /* Enable RSS */
    uint64_t rss_hf;            /* RSS hash functions */

    /* Offload features */
    bool rx_checksum;           /* RX checksum offload */
    bool tx_checksum;           /* TX checksum offload */
    bool tso;                   /* TCP segmentation offload */
    bool lro;                   /* Large receive offload */

    /* vhost-user specific */
    dpdk_vhost_config_t vhost;
} dpdk_port_config_t;

/* DPDK port statistics */
typedef struct {
    uint64_t ipackets;          /* Total RX packets */
    uint64_t opackets;          /* Total TX packets */
    uint64_t ibytes;            /* Total RX bytes */
    uint64_t obytes;            /* Total TX bytes */
    uint64_t imissed;           /* RX packets dropped by NIC */
    uint64_t ierrors;           /* Total RX errors */
    uint64_t oerrors;           /* Total TX errors */
    uint64_t rx_nombuf;         /* RX mbuf allocation failures */

    /* Per-queue stats (first 8 queues) */
    uint64_t q_ipackets[8];     /* Per-queue RX packets */
    uint64_t q_opackets[8];     /* Per-queue TX packets */
    uint64_t q_ibytes[8];       /* Per-queue RX bytes */
    uint64_t q_obytes[8];       /* Per-queue TX bytes */
    uint64_t q_errors[8];       /* Per-queue errors */
} dpdk_port_stats_t;

/* DPDK memory info */
typedef struct {
    uint64_t total_memory;      /* Total hugepage memory */
    uint64_t free_memory;       /* Free hugepage memory */
    uint32_t socket_count;      /* Number of NUMA sockets */
    uint32_t channel_count;     /* Memory channels per socket */
} dpdk_memory_info_t;

/* DPDK device info */
typedef struct {
    char name[64];              /* Device name */
    char driver[64];            /* Driver name */
    uint16_t port_id;           /* DPDK port ID */
    dpdk_port_state_t state;    /* Current state */
    uint8_t mac_addr[6];        /* MAC address */
    uint32_t max_rx_queues;     /* Max RX queues */
    uint32_t max_tx_queues;     /* Max TX queues */
    uint16_t max_mtu;           /* Maximum MTU */
    uint16_t min_mtu;           /* Minimum MTU */
    uint64_t rx_offload_capa;   /* RX offload capabilities */
    uint64_t tx_offload_capa;   /* TX offload capabilities */
} dpdk_device_info_t;

/* ============================================================================
 * DPDK Initialization
 * ============================================================================ */

/**
 * Initialize DPDK
 *
 * @param eal_args EAL arguments (NULL for default)
 * @return OVS_OK on success
 */
int dpdk_init(const char *eal_args);

/**
 * Cleanup DPDK
 */
void dpdk_cleanup(void);

/**
 * Check if DPDK is initialized
 *
 * @return true if initialized
 */
bool dpdk_is_initialized(void);

/**
 * Get DPDK memory info
 *
 * @param info Output memory info
 * @return OVS_OK on success
 */
int dpdk_get_memory_info(dpdk_memory_info_t *info);

/* ============================================================================
 * Port Management
 * ============================================================================ */

/**
 * Create a DPDK port
 *
 * @param config Port configuration
 * @return Port ID on success, negative error code on failure
 */
int dpdk_port_create(const dpdk_port_config_t *config);

/**
 * Destroy a DPDK port
 *
 * @param port_id Port ID
 * @return OVS_OK on success
 */
int dpdk_port_destroy(uint16_t port_id);

/**
 * Start a DPDK port
 *
 * @param port_id Port ID
 * @return OVS_OK on success
 */
int dpdk_port_start(uint16_t port_id);

/**
 * Stop a DPDK port
 *
 * @param port_id Port ID
 * @return OVS_OK on success
 */
int dpdk_port_stop(uint16_t port_id);

/**
 * Get port configuration
 *
 * @param port_id Port ID
 * @param config Output configuration
 * @return OVS_OK on success
 */
int dpdk_port_get_config(uint16_t port_id, dpdk_port_config_t *config);

/**
 * Get device info
 *
 * @param port_id Port ID
 * @param info Output device info
 * @return OVS_OK on success
 */
int dpdk_port_get_info(uint16_t port_id, dpdk_device_info_t *info);

/**
 * Get port statistics
 *
 * @param port_id Port ID
 * @param stats Output statistics
 * @return OVS_OK on success
 */
int dpdk_port_get_stats(uint16_t port_id, dpdk_port_stats_t *stats);

/**
 * Reset port statistics
 *
 * @param port_id Port ID
 * @return OVS_OK on success
 */
int dpdk_port_reset_stats(uint16_t port_id);

/**
 * Set port MTU
 *
 * @param port_id Port ID
 * @param mtu New MTU value
 * @return OVS_OK on success
 */
int dpdk_port_set_mtu(uint16_t port_id, uint32_t mtu);

/**
 * Set port promiscuous mode
 *
 * @param port_id Port ID
 * @param enable true to enable
 * @return OVS_OK on success
 */
int dpdk_port_set_promisc(uint16_t port_id, bool enable);

/**
 * Get number of available ports
 *
 * @return Number of ports
 */
uint16_t dpdk_port_count(void);

/**
 * List all DPDK ports
 *
 * @param ports Output array of port IDs
 * @param max_ports Maximum ports to return
 * @return Number of ports
 */
int dpdk_port_list(uint16_t *ports, uint32_t max_ports);

/* ============================================================================
 * vhost-user Management
 * ============================================================================ */

/**
 * Create vhost-user port for VM
 *
 * @param config vhost-user configuration
 * @return Port ID on success, negative error code on failure
 */
int dpdk_vhost_create(const dpdk_vhost_config_t *config);

/**
 * Destroy vhost-user port
 *
 * @param port_id Port ID
 * @return OVS_OK on success
 */
int dpdk_vhost_destroy(uint16_t port_id);

/**
 * Check if vhost-user port is connected
 *
 * @param port_id Port ID
 * @return true if connected to VM
 */
bool dpdk_vhost_is_connected(uint16_t port_id);

/**
 * Get vhost-user socket path
 *
 * @param port_id Port ID
 * @param path Output path buffer
 * @param path_len Path buffer length
 * @return OVS_OK on success
 */
int dpdk_vhost_get_socket_path(uint16_t port_id, char *path, size_t path_len);

/* ============================================================================
 * Queue Management
 * ============================================================================ */

/**
 * Get queue statistics
 *
 * @param port_id Port ID
 * @param queue_id Queue ID
 * @param rx true for RX queue, false for TX
 * @param packets Output packet count
 * @param bytes Output byte count
 * @return OVS_OK on success
 */
int dpdk_queue_get_stats(uint16_t port_id, uint16_t queue_id, bool rx,
                          uint64_t *packets, uint64_t *bytes);

/**
 * Get number of queues
 *
 * @param port_id Port ID
 * @param rx_queues Output RX queue count
 * @param tx_queues Output TX queue count
 * @return OVS_OK on success
 */
int dpdk_queue_count(uint16_t port_id, uint16_t *rx_queues, uint16_t *tx_queues);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get device type name
 *
 * @param type Device type
 * @return Static string name
 */
const char *dpdk_dev_type_name(dpdk_dev_type_t type);

/**
 * Get port state name
 *
 * @param state Port state
 * @return Static string name
 */
const char *dpdk_port_state_name(dpdk_port_state_t state);

/**
 * Get last error message
 *
 * @return Static error string
 */
const char *dpdk_get_last_error(void);

/**
 * Format MAC address to string
 *
 * @param mac MAC address bytes
 * @param buf Output buffer (at least 18 bytes)
 * @return buf pointer
 */
char *dpdk_format_mac(const uint8_t *mac, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_DPDK_PORT_H */
