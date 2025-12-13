/**
 * Zixiao Hypervisor - OVS Bridge Management
 *
 * Open vSwitch bridge management for virtual networking.
 * Supports DPDK-accelerated datapath for high-performance networking.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#ifndef ZIXIAO_OVS_BRIDGE_H
#define ZIXIAO_OVS_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define OVS_OK                  0
#define OVS_ERR_INIT           -1
#define OVS_ERR_NOT_INIT       -2
#define OVS_ERR_MEMORY         -3
#define OVS_ERR_INVALID        -4
#define OVS_ERR_NOT_FOUND      -5
#define OVS_ERR_EXISTS         -6
#define OVS_ERR_OVSDB          -7
#define OVS_ERR_OPENFLOW       -8
#define OVS_ERR_DPDK           -9

/* Port types */
typedef enum {
    OVS_PORT_INTERNAL = 0,      /* Internal OVS port */
    OVS_PORT_SYSTEM,            /* System interface (physical NIC) */
    OVS_PORT_TAP,               /* TAP interface for VM */
    OVS_PORT_VXLAN,             /* VXLAN tunnel */
    OVS_PORT_GENEVE,            /* Geneve tunnel */
    OVS_PORT_GRE,               /* GRE tunnel */
    OVS_PORT_PATCH,             /* Patch port between bridges */
    OVS_PORT_DPDK,              /* DPDK port */
    OVS_PORT_DPDKVHOSTUSER,     /* DPDK vhost-user for VM */
    OVS_PORT_DPDKVHOSTUSERCLIENT /* DPDK vhost-user client mode */
} ovs_port_type_t;

/* Bridge configuration */
typedef struct {
    char name[64];              /* Bridge name */
    char datapath_type[32];     /* "system" or "netdev" (DPDK) */
    bool stp_enabled;           /* Spanning tree protocol */
    bool rstp_enabled;          /* Rapid spanning tree */
    uint16_t fail_mode;         /* 0=standalone, 1=secure */
    char controller[256];       /* OpenFlow controller address */
} ovs_bridge_config_t;

/* Port configuration */
typedef struct {
    char name[64];              /* Port name */
    char bridge[64];            /* Parent bridge name */
    ovs_port_type_t type;       /* Port type */
    uint16_t tag;               /* VLAN tag (0 = trunk) */
    uint16_t trunks[4096/16];   /* Trunk VLAN bitmap */
    uint32_t ofport;            /* OpenFlow port number */

    /* Tunnel options (for VXLAN, Geneve, GRE) */
    struct {
        char remote_ip[64];     /* Remote tunnel endpoint */
        char local_ip[64];      /* Local tunnel endpoint */
        uint32_t key;           /* Tunnel key/VNI */
        uint16_t dst_port;      /* Destination port (VXLAN=4789) */
    } tunnel;

    /* DPDK options */
    struct {
        char devargs[256];      /* DPDK device arguments */
        uint16_t rxq;           /* Number of RX queues */
        uint16_t txq;           /* Number of TX queues */
        uint32_t mtu;           /* MTU size */
    } dpdk;

    /* vhost-user options */
    struct {
        char socket_path[256];  /* vhost-user socket path */
        bool server_mode;       /* true = server, false = client */
    } vhost;
} ovs_port_config_t;

/* OpenFlow flow rule */
typedef struct {
    uint32_t table_id;          /* Flow table ID */
    uint16_t priority;          /* Rule priority */
    uint64_t cookie;            /* Cookie for identification */
    uint32_t idle_timeout;      /* Idle timeout seconds */
    uint32_t hard_timeout;      /* Hard timeout seconds */

    /* Match fields */
    struct {
        uint32_t in_port;       /* Input port */
        uint8_t dl_src[6];      /* Source MAC */
        uint8_t dl_dst[6];      /* Destination MAC */
        uint16_t dl_type;       /* Ethertype */
        uint16_t dl_vlan;       /* VLAN ID */
        uint32_t nw_src;        /* Source IP */
        uint32_t nw_dst;        /* Destination IP */
        uint8_t nw_proto;       /* IP protocol */
        uint16_t tp_src;        /* L4 source port */
        uint16_t tp_dst;        /* L4 destination port */
        uint32_t tun_id;        /* Tunnel ID */
    } match;

    /* Actions (simplified) */
    struct {
        uint32_t output_port;   /* Output port (0 = drop) */
        uint16_t set_vlan;      /* Set VLAN ID (0 = no change) */
        bool strip_vlan;        /* Strip VLAN tag */
        uint32_t set_tunnel;    /* Set tunnel ID */
        uint8_t set_dl_src[6];  /* Rewrite source MAC */
        uint8_t set_dl_dst[6];  /* Rewrite destination MAC */
        uint32_t goto_table;    /* Go to table (0 = no goto) */
    } actions;
} ovs_flow_t;

/* Flow statistics */
typedef struct {
    uint64_t packet_count;
    uint64_t byte_count;
    uint64_t duration_sec;
    uint64_t duration_nsec;
} ovs_flow_stats_t;

/* Bridge statistics */
typedef struct {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t rx_errors;
    uint64_t tx_errors;
} ovs_bridge_stats_t;

/* Port statistics */
typedef struct {
    char name[64];
    uint32_t ofport;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t collisions;
} ovs_port_stats_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize OVS management library
 *
 * @param ovsdb_socket Path to OVSDB socket (NULL for default)
 * @return OVS_OK on success
 */
int ovs_init(const char *ovsdb_socket);

/**
 * Cleanup OVS management library
 */
void ovs_cleanup(void);

/**
 * Check if OVS is available
 *
 * @return true if OVS is available
 */
bool ovs_available(void);

/**
 * Check if DPDK support is available
 *
 * @return true if DPDK is available
 */
bool ovs_dpdk_available(void);

/* ============================================================================
 * Bridge Management
 * ============================================================================ */

/**
 * Create a new bridge
 *
 * @param config Bridge configuration
 * @return OVS_OK on success
 */
int ovs_bridge_create(const ovs_bridge_config_t *config);

/**
 * Delete a bridge
 *
 * @param name Bridge name
 * @return OVS_OK on success
 */
int ovs_bridge_delete(const char *name);

/**
 * Get bridge configuration
 *
 * @param name Bridge name
 * @param config Output configuration
 * @return OVS_OK on success
 */
int ovs_bridge_get(const char *name, ovs_bridge_config_t *config);

/**
 * List all bridges
 *
 * @param bridges Output array
 * @param max_bridges Maximum bridges to return
 * @return Number of bridges, or negative error code
 */
int ovs_bridge_list(ovs_bridge_config_t *bridges, uint32_t max_bridges);

/**
 * Get bridge statistics
 *
 * @param name Bridge name
 * @param stats Output statistics
 * @return OVS_OK on success
 */
int ovs_bridge_get_stats(const char *name, ovs_bridge_stats_t *stats);

/**
 * Set OpenFlow controller for bridge
 *
 * @param bridge Bridge name
 * @param controller Controller address (tcp:ip:port or unix:path)
 * @return OVS_OK on success
 */
int ovs_bridge_set_controller(const char *bridge, const char *controller);

/* ============================================================================
 * Port Management
 * ============================================================================ */

/**
 * Add a port to a bridge
 *
 * @param config Port configuration
 * @return OVS_OK on success
 */
int ovs_port_add(const ovs_port_config_t *config);

/**
 * Delete a port from a bridge
 *
 * @param bridge Bridge name
 * @param port Port name
 * @return OVS_OK on success
 */
int ovs_port_delete(const char *bridge, const char *port);

/**
 * Get port configuration
 *
 * @param bridge Bridge name
 * @param port Port name
 * @param config Output configuration
 * @return OVS_OK on success
 */
int ovs_port_get(const char *bridge, const char *port, ovs_port_config_t *config);

/**
 * List all ports on a bridge
 *
 * @param bridge Bridge name
 * @param ports Output array
 * @param max_ports Maximum ports to return
 * @return Number of ports, or negative error code
 */
int ovs_port_list(const char *bridge, ovs_port_config_t *ports, uint32_t max_ports);

/**
 * Get port statistics
 *
 * @param bridge Bridge name
 * @param port Port name
 * @param stats Output statistics
 * @return OVS_OK on success
 */
int ovs_port_get_stats(const char *bridge, const char *port, ovs_port_stats_t *stats);

/**
 * Set port VLAN tag
 *
 * @param bridge Bridge name
 * @param port Port name
 * @param tag VLAN tag (0 for trunk)
 * @return OVS_OK on success
 */
int ovs_port_set_vlan(const char *bridge, const char *port, uint16_t tag);

/* ============================================================================
 * OpenFlow Management
 * ============================================================================ */

/**
 * Add a flow rule
 *
 * @param bridge Bridge name
 * @param flow Flow rule
 * @return OVS_OK on success
 */
int ovs_flow_add(const char *bridge, const ovs_flow_t *flow);

/**
 * Delete a flow rule
 *
 * @param bridge Bridge name
 * @param flow Flow match criteria
 * @return OVS_OK on success
 */
int ovs_flow_delete(const char *bridge, const ovs_flow_t *flow);

/**
 * Delete all flows in a table
 *
 * @param bridge Bridge name
 * @param table_id Table ID (-1 for all tables)
 * @return OVS_OK on success
 */
int ovs_flow_delete_all(const char *bridge, int table_id);

/**
 * Dump all flows
 *
 * @param bridge Bridge name
 * @param flows Output array
 * @param max_flows Maximum flows to return
 * @return Number of flows, or negative error code
 */
int ovs_flow_dump(const char *bridge, ovs_flow_t *flows, uint32_t max_flows);

/**
 * Get flow statistics
 *
 * @param bridge Bridge name
 * @param flow Flow match criteria
 * @param stats Output statistics
 * @return OVS_OK on success
 */
int ovs_flow_get_stats(const char *bridge, const ovs_flow_t *flow, ovs_flow_stats_t *stats);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get last error message
 *
 * @return Static error string
 */
const char *ovs_get_last_error(void);

/**
 * Get port type name
 *
 * @param type Port type
 * @return Static string name
 */
const char *ovs_port_type_name(ovs_port_type_t type);

/**
 * Parse port type from string
 *
 * @param name Port type name
 * @return Port type, or -1 on error
 */
int ovs_port_type_parse(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_OVS_BRIDGE_H */
