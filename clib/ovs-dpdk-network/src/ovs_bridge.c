/**
 * Zixiao Hypervisor - OVS Bridge Implementation
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "ovs_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

/* Default OVSDB socket path */
#define DEFAULT_OVSDB_SOCKET "/var/run/openvswitch/db.sock"

/* Internal state */
static struct {
    bool initialized;
    char ovsdb_socket[256];
    char last_error[256];
} ovs_state = {0};

/* Bridge storage (simplified - real impl would use OVSDB) */
#define MAX_BRIDGES 64
static ovs_bridge_config_t bridges[MAX_BRIDGES];
static uint32_t bridge_count = 0;

/* Port storage */
#define MAX_PORTS 1024
static ovs_port_config_t ports[MAX_PORTS];
static uint32_t port_count = 0;

/* Flow storage */
#define MAX_FLOWS 4096
static struct {
    char bridge[64];
    ovs_flow_t flow;
    ovs_flow_stats_t stats;
} flows[MAX_FLOWS];
static uint32_t flow_count = 0;

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(ovs_state.last_error, sizeof(ovs_state.last_error), fmt, args);
    va_end(args);
}

/* Execute ovs-vsctl command (simplified) */
static int ovs_vsctl(const char *cmd, char *output, size_t output_len) {
    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "ovs-vsctl --db=unix:%s %s 2>&1",
             ovs_state.ovsdb_socket, cmd);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        set_error("Failed to execute ovs-vsctl: %s", strerror(errno));
        return OVS_ERR_OVSDB;
    }

    if (output && output_len > 0) {
        size_t total = 0;
        while (fgets(output + total, output_len - total, fp) && total < output_len - 1) {
            total = strlen(output);
        }
    }

    int ret = pclose(fp);
    return ret == 0 ? OVS_OK : OVS_ERR_OVSDB;
}

/* Execute ovs-ofctl command */
static int ovs_ofctl(const char *bridge, const char *cmd, char *output, size_t output_len) {
    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "ovs-ofctl %s %s 2>&1", cmd, bridge);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        set_error("Failed to execute ovs-ofctl: %s", strerror(errno));
        return OVS_ERR_OPENFLOW;
    }

    if (output && output_len > 0) {
        size_t total = 0;
        while (fgets(output + total, output_len - total, fp) && total < output_len - 1) {
            total = strlen(output);
        }
    }

    int ret = pclose(fp);
    return ret == 0 ? OVS_OK : OVS_ERR_OPENFLOW;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

int ovs_init(const char *ovsdb_socket) {
    if (ovs_state.initialized) {
        set_error("OVS already initialized");
        return OVS_ERR_INIT;
    }

    if (ovsdb_socket) {
        strncpy(ovs_state.ovsdb_socket, ovsdb_socket,
                sizeof(ovs_state.ovsdb_socket) - 1);
    } else {
        strncpy(ovs_state.ovsdb_socket, DEFAULT_OVSDB_SOCKET,
                sizeof(ovs_state.ovsdb_socket) - 1);
    }

    /* Check if OVSDB is accessible */
    if (access(ovs_state.ovsdb_socket, F_OK) != 0) {
        set_error("OVSDB socket not found: %s", ovs_state.ovsdb_socket);
        return OVS_ERR_OVSDB;
    }

    /* Clear state */
    memset(bridges, 0, sizeof(bridges));
    memset(ports, 0, sizeof(ports));
    memset(flows, 0, sizeof(flows));
    bridge_count = 0;
    port_count = 0;
    flow_count = 0;

    ovs_state.initialized = true;
    return OVS_OK;
}

void ovs_cleanup(void) {
    if (!ovs_state.initialized) return;

    memset(&ovs_state, 0, sizeof(ovs_state));
    bridge_count = 0;
    port_count = 0;
    flow_count = 0;
}

bool ovs_available(void) {
    return access("/usr/bin/ovs-vsctl", X_OK) == 0 ||
           access("/usr/local/bin/ovs-vsctl", X_OK) == 0;
}

bool ovs_dpdk_available(void) {
    char output[256] = {0};
    if (ovs_vsctl("get Open_vSwitch . dpdk_initialized", output, sizeof(output)) != OVS_OK) {
        return false;
    }
    return strstr(output, "true") != NULL;
}

/* ============================================================================
 * Bridge Management
 * ============================================================================ */

int ovs_bridge_create(const ovs_bridge_config_t *config) {
    if (!ovs_state.initialized) {
        set_error("OVS not initialized");
        return OVS_ERR_NOT_INIT;
    }

    if (!config || !config->name[0]) {
        set_error("Invalid bridge configuration");
        return OVS_ERR_INVALID;
    }

    /* Check if bridge already exists */
    for (uint32_t i = 0; i < bridge_count; i++) {
        if (strcmp(bridges[i].name, config->name) == 0) {
            set_error("Bridge already exists: %s", config->name);
            return OVS_ERR_EXISTS;
        }
    }

    if (bridge_count >= MAX_BRIDGES) {
        set_error("Maximum bridges reached");
        return OVS_ERR_MEMORY;
    }

    /* Build ovs-vsctl command */
    char cmd[512];
    if (config->datapath_type[0]) {
        snprintf(cmd, sizeof(cmd), "add-br %s -- set bridge %s datapath_type=%s",
                 config->name, config->name, config->datapath_type);
    } else {
        snprintf(cmd, sizeof(cmd), "add-br %s", config->name);
    }

    int ret = ovs_vsctl(cmd, NULL, 0);
    if (ret != OVS_OK) {
        return ret;
    }

    /* Set controller if specified */
    if (config->controller[0]) {
        snprintf(cmd, sizeof(cmd), "set-controller %s %s",
                 config->name, config->controller);
        ovs_vsctl(cmd, NULL, 0);
    }

    /* Set fail mode */
    if (config->fail_mode == 1) {
        snprintf(cmd, sizeof(cmd), "set-fail-mode %s secure", config->name);
        ovs_vsctl(cmd, NULL, 0);
    }

    /* Store bridge config */
    bridges[bridge_count++] = *config;

    return OVS_OK;
}

int ovs_bridge_delete(const char *name) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!name) {
        return OVS_ERR_INVALID;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "del-br %s", name);

    int ret = ovs_vsctl(cmd, NULL, 0);
    if (ret != OVS_OK) {
        return ret;
    }

    /* Remove from storage */
    for (uint32_t i = 0; i < bridge_count; i++) {
        if (strcmp(bridges[i].name, name) == 0) {
            memmove(&bridges[i], &bridges[i + 1],
                    (bridge_count - i - 1) * sizeof(ovs_bridge_config_t));
            bridge_count--;
            break;
        }
    }

    /* Remove associated ports */
    for (uint32_t i = 0; i < port_count; ) {
        if (strcmp(ports[i].bridge, name) == 0) {
            memmove(&ports[i], &ports[i + 1],
                    (port_count - i - 1) * sizeof(ovs_port_config_t));
            port_count--;
        } else {
            i++;
        }
    }

    return OVS_OK;
}

int ovs_bridge_get(const char *name, ovs_bridge_config_t *config) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!name || !config) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < bridge_count; i++) {
        if (strcmp(bridges[i].name, name) == 0) {
            *config = bridges[i];
            return OVS_OK;
        }
    }

    set_error("Bridge not found: %s", name);
    return OVS_ERR_NOT_FOUND;
}

int ovs_bridge_list(ovs_bridge_config_t *bridges_out, uint32_t max_bridges) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridges_out) {
        return OVS_ERR_INVALID;
    }

    uint32_t count = bridge_count < max_bridges ? bridge_count : max_bridges;
    memcpy(bridges_out, bridges, count * sizeof(ovs_bridge_config_t));

    return (int)count;
}

int ovs_bridge_get_stats(const char *name, ovs_bridge_stats_t *stats) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!name || !stats) {
        return OVS_ERR_INVALID;
    }

    memset(stats, 0, sizeof(*stats));

    /* In real implementation, aggregate port stats */
    for (uint32_t i = 0; i < port_count; i++) {
        if (strcmp(ports[i].bridge, name) == 0) {
            /* Would read actual stats from kernel/DPDK */
        }
    }

    return OVS_OK;
}

int ovs_bridge_set_controller(const char *bridge, const char *controller) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge) {
        return OVS_ERR_INVALID;
    }

    char cmd[512];
    if (controller && controller[0]) {
        snprintf(cmd, sizeof(cmd), "set-controller %s %s", bridge, controller);
    } else {
        snprintf(cmd, sizeof(cmd), "del-controller %s", bridge);
    }

    return ovs_vsctl(cmd, NULL, 0);
}

/* ============================================================================
 * Port Management
 * ============================================================================ */

int ovs_port_add(const ovs_port_config_t *config) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!config || !config->name[0] || !config->bridge[0]) {
        set_error("Invalid port configuration");
        return OVS_ERR_INVALID;
    }

    if (port_count >= MAX_PORTS) {
        set_error("Maximum ports reached");
        return OVS_ERR_MEMORY;
    }

    /* Build ovs-vsctl command based on port type */
    char cmd[1024];
    const char *type_str = "";

    switch (config->type) {
        case OVS_PORT_INTERNAL:
            type_str = "internal";
            break;
        case OVS_PORT_VXLAN:
            type_str = "vxlan";
            break;
        case OVS_PORT_GENEVE:
            type_str = "geneve";
            break;
        case OVS_PORT_GRE:
            type_str = "gre";
            break;
        case OVS_PORT_PATCH:
            type_str = "patch";
            break;
        case OVS_PORT_DPDK:
            type_str = "dpdk";
            break;
        case OVS_PORT_DPDKVHOSTUSER:
            type_str = "dpdkvhostuser";
            break;
        case OVS_PORT_DPDKVHOSTUSERCLIENT:
            type_str = "dpdkvhostuserclient";
            break;
        default:
            type_str = "";
    }

    if (config->type == OVS_PORT_VXLAN || config->type == OVS_PORT_GENEVE ||
        config->type == OVS_PORT_GRE) {
        /* Tunnel port */
        snprintf(cmd, sizeof(cmd),
                 "add-port %s %s -- set interface %s type=%s options:remote_ip=%s",
                 config->bridge, config->name, config->name, type_str,
                 config->tunnel.remote_ip);

        if (config->tunnel.key) {
            char key_opt[64];
            snprintf(key_opt, sizeof(key_opt), " options:key=%u", config->tunnel.key);
            strncat(cmd, key_opt, sizeof(cmd) - strlen(cmd) - 1);
        }
    } else if (config->type == OVS_PORT_DPDKVHOSTUSER ||
               config->type == OVS_PORT_DPDKVHOSTUSERCLIENT) {
        /* vhost-user port */
        snprintf(cmd, sizeof(cmd),
                 "add-port %s %s -- set interface %s type=%s options:vhost-server-path=%s",
                 config->bridge, config->name, config->name, type_str,
                 config->vhost.socket_path);
    } else if (config->type == OVS_PORT_DPDK) {
        /* DPDK physical port */
        snprintf(cmd, sizeof(cmd),
                 "add-port %s %s -- set interface %s type=dpdk options:dpdk-devargs=%s",
                 config->bridge, config->name, config->name, config->dpdk.devargs);
    } else if (type_str[0]) {
        /* Other typed port */
        snprintf(cmd, sizeof(cmd),
                 "add-port %s %s -- set interface %s type=%s",
                 config->bridge, config->name, config->name, type_str);
    } else {
        /* System port */
        snprintf(cmd, sizeof(cmd), "add-port %s %s", config->bridge, config->name);
    }

    int ret = ovs_vsctl(cmd, NULL, 0);
    if (ret != OVS_OK) {
        return ret;
    }

    /* Set VLAN tag if specified */
    if (config->tag > 0) {
        snprintf(cmd, sizeof(cmd), "set port %s tag=%u", config->name, config->tag);
        ovs_vsctl(cmd, NULL, 0);
    }

    /* Store port config */
    ports[port_count++] = *config;

    return OVS_OK;
}

int ovs_port_delete(const char *bridge, const char *port) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !port) {
        return OVS_ERR_INVALID;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "del-port %s %s", bridge, port);

    int ret = ovs_vsctl(cmd, NULL, 0);
    if (ret != OVS_OK) {
        return ret;
    }

    /* Remove from storage */
    for (uint32_t i = 0; i < port_count; i++) {
        if (strcmp(ports[i].bridge, bridge) == 0 &&
            strcmp(ports[i].name, port) == 0) {
            memmove(&ports[i], &ports[i + 1],
                    (port_count - i - 1) * sizeof(ovs_port_config_t));
            port_count--;
            break;
        }
    }

    return OVS_OK;
}

int ovs_port_get(const char *bridge, const char *port, ovs_port_config_t *config) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !port || !config) {
        return OVS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < port_count; i++) {
        if (strcmp(ports[i].bridge, bridge) == 0 &&
            strcmp(ports[i].name, port) == 0) {
            *config = ports[i];
            return OVS_OK;
        }
    }

    set_error("Port not found: %s/%s", bridge, port);
    return OVS_ERR_NOT_FOUND;
}

int ovs_port_list(const char *bridge, ovs_port_config_t *ports_out, uint32_t max_ports) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !ports_out) {
        return OVS_ERR_INVALID;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < port_count && count < max_ports; i++) {
        if (strcmp(ports[i].bridge, bridge) == 0) {
            ports_out[count++] = ports[i];
        }
    }

    return (int)count;
}

int ovs_port_get_stats(const char *bridge, const char *port, ovs_port_stats_t *stats) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !port || !stats) {
        return OVS_ERR_INVALID;
    }

    memset(stats, 0, sizeof(*stats));
    strncpy(stats->name, port, sizeof(stats->name) - 1);

    /* In real implementation, read from ovs-vsctl or kernel */

    return OVS_OK;
}

int ovs_port_set_vlan(const char *bridge, const char *port, uint16_t tag) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !port) {
        return OVS_ERR_INVALID;
    }

    (void)bridge;  /* Not needed for ovs-vsctl */

    char cmd[256];
    if (tag > 0) {
        snprintf(cmd, sizeof(cmd), "set port %s tag=%u", port, tag);
    } else {
        snprintf(cmd, sizeof(cmd), "clear port %s tag", port);
    }

    return ovs_vsctl(cmd, NULL, 0);
}

/* ============================================================================
 * OpenFlow Management
 * ============================================================================ */

int ovs_flow_add(const char *bridge, const ovs_flow_t *flow) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !flow) {
        return OVS_ERR_INVALID;
    }

    if (flow_count >= MAX_FLOWS) {
        set_error("Maximum flows reached");
        return OVS_ERR_MEMORY;
    }

    /* Build flow match string */
    char match[512] = "";
    char *p = match;
    size_t remaining = sizeof(match);

    if (flow->match.in_port) {
        int n = snprintf(p, remaining, "in_port=%u,", flow->match.in_port);
        p += n;
        remaining -= n;
    }

    if (flow->match.dl_type) {
        int n = snprintf(p, remaining, "dl_type=0x%04x,", flow->match.dl_type);
        p += n;
        remaining -= n;
    }

    if (flow->match.nw_src) {
        int n = snprintf(p, remaining, "nw_src=0x%08x,", flow->match.nw_src);
        p += n;
        remaining -= n;
    }

    if (flow->match.nw_dst) {
        int n = snprintf(p, remaining, "nw_dst=0x%08x,", flow->match.nw_dst);
        p += n;
        remaining -= n;
    }

    /* Remove trailing comma */
    if (p > match && *(p - 1) == ',') {
        *(p - 1) = '\0';
    }

    /* Build actions string */
    char actions[256] = "";
    if (flow->actions.output_port) {
        snprintf(actions, sizeof(actions), "output:%u", flow->actions.output_port);
    } else if (flow->actions.goto_table) {
        snprintf(actions, sizeof(actions), "goto_table:%u", flow->actions.goto_table);
    } else {
        strncpy(actions, "drop", sizeof(actions));
    }

    /* Build ovs-ofctl command */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "add-flow -OOpenFlow13 %s \"table=%u,priority=%u,%s,actions=%s\"",
             bridge, flow->table_id, flow->priority,
             match[0] ? match : "priority=0", actions);

    /* Execute via shell since ovs-ofctl needs the bridge name directly */
    char full_cmd[1200];
    snprintf(full_cmd, sizeof(full_cmd), "ovs-ofctl %s 2>&1", cmd + 10); /* Skip "add-flow " */

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        set_error("Failed to execute ovs-ofctl");
        return OVS_ERR_OPENFLOW;
    }

    int ret = pclose(fp);
    if (ret != 0) {
        set_error("ovs-ofctl add-flow failed");
        return OVS_ERR_OPENFLOW;
    }

    /* Store flow */
    strncpy(flows[flow_count].bridge, bridge, sizeof(flows[flow_count].bridge) - 1);
    flows[flow_count].flow = *flow;
    memset(&flows[flow_count].stats, 0, sizeof(ovs_flow_stats_t));
    flow_count++;

    return OVS_OK;
}

int ovs_flow_delete(const char *bridge, const ovs_flow_t *flow) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !flow) {
        return OVS_ERR_INVALID;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "del-flows -OOpenFlow13 %s \"table=%u,cookie=0x%lx/-1\"",
             bridge, flow->table_id, flow->cookie);

    return ovs_ofctl(bridge, cmd, NULL, 0);
}

int ovs_flow_delete_all(const char *bridge, int table_id) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge) {
        return OVS_ERR_INVALID;
    }

    char cmd[256];
    if (table_id >= 0) {
        snprintf(cmd, sizeof(cmd), "del-flows -OOpenFlow13 %s table=%d", bridge, table_id);
    } else {
        snprintf(cmd, sizeof(cmd), "del-flows -OOpenFlow13 %s", bridge);
    }

    int ret = ovs_ofctl(bridge, cmd, NULL, 0);

    /* Clear stored flows */
    if (ret == OVS_OK) {
        for (uint32_t i = 0; i < flow_count; ) {
            if (strcmp(flows[i].bridge, bridge) == 0 &&
                (table_id < 0 || flows[i].flow.table_id == (uint32_t)table_id)) {
                memmove(&flows[i], &flows[i + 1],
                        (flow_count - i - 1) * sizeof(flows[0]));
                flow_count--;
            } else {
                i++;
            }
        }
    }

    return ret;
}

int ovs_flow_dump(const char *bridge, ovs_flow_t *flows_out, uint32_t max_flows) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !flows_out) {
        return OVS_ERR_INVALID;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < flow_count && count < max_flows; i++) {
        if (strcmp(flows[i].bridge, bridge) == 0) {
            flows_out[count++] = flows[i].flow;
        }
    }

    return (int)count;
}

int ovs_flow_get_stats(const char *bridge, const ovs_flow_t *flow, ovs_flow_stats_t *stats) {
    if (!ovs_state.initialized) {
        return OVS_ERR_NOT_INIT;
    }

    if (!bridge || !flow || !stats) {
        return OVS_ERR_INVALID;
    }

    memset(stats, 0, sizeof(*stats));

    /* In real implementation, parse ovs-ofctl dump-flows output */

    return OVS_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *ovs_get_last_error(void) {
    return ovs_state.last_error[0] ? ovs_state.last_error : "No error";
}

const char *ovs_port_type_name(ovs_port_type_t type) {
    switch (type) {
        case OVS_PORT_INTERNAL:          return "internal";
        case OVS_PORT_SYSTEM:            return "system";
        case OVS_PORT_TAP:               return "tap";
        case OVS_PORT_VXLAN:             return "vxlan";
        case OVS_PORT_GENEVE:            return "geneve";
        case OVS_PORT_GRE:               return "gre";
        case OVS_PORT_PATCH:             return "patch";
        case OVS_PORT_DPDK:              return "dpdk";
        case OVS_PORT_DPDKVHOSTUSER:     return "dpdkvhostuser";
        case OVS_PORT_DPDKVHOSTUSERCLIENT: return "dpdkvhostuserclient";
        default:                         return "unknown";
    }
}

int ovs_port_type_parse(const char *name) {
    if (!name) return -1;

    if (strcmp(name, "internal") == 0) return OVS_PORT_INTERNAL;
    if (strcmp(name, "system") == 0) return OVS_PORT_SYSTEM;
    if (strcmp(name, "tap") == 0) return OVS_PORT_TAP;
    if (strcmp(name, "vxlan") == 0) return OVS_PORT_VXLAN;
    if (strcmp(name, "geneve") == 0) return OVS_PORT_GENEVE;
    if (strcmp(name, "gre") == 0) return OVS_PORT_GRE;
    if (strcmp(name, "patch") == 0) return OVS_PORT_PATCH;
    if (strcmp(name, "dpdk") == 0) return OVS_PORT_DPDK;
    if (strcmp(name, "dpdkvhostuser") == 0) return OVS_PORT_DPDKVHOSTUSER;
    if (strcmp(name, "dpdkvhostuserclient") == 0) return OVS_PORT_DPDKVHOSTUSERCLIENT;

    return -1;
}
