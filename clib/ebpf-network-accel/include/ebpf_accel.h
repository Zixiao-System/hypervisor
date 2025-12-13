/**
 * Zixiao Hypervisor - eBPF Network Acceleration
 *
 * This library provides eBPF-based network acceleration for VMs,
 * including XDP redirect, TC filtering, and VM-to-VM fast path.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#ifndef ZIXIAO_EBPF_ACCEL_H
#define ZIXIAO_EBPF_ACCEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define EBPF_OK                 0
#define EBPF_ERR_INIT          -1
#define EBPF_ERR_NOT_INIT      -2
#define EBPF_ERR_MEMORY        -3
#define EBPF_ERR_INVALID       -4
#define EBPF_ERR_LOAD          -5
#define EBPF_ERR_ATTACH        -6
#define EBPF_ERR_MAP           -7
#define EBPF_ERR_PERMISSION    -8

/* XDP action types */
typedef enum {
    XDP_ACTION_PASS = 0,
    XDP_ACTION_DROP,
    XDP_ACTION_REDIRECT,
    XDP_ACTION_TX
} xdp_action_t;

/* Network interface info */
typedef struct {
    uint32_t ifindex;
    char ifname[64];
    uint8_t mac[6];
    uint32_t mtu;
} netif_info_t;

/* XDP redirect rule */
typedef struct {
    uint32_t src_ifindex;       /* Source interface index */
    uint32_t dst_ifindex;       /* Destination interface index */
    uint8_t src_mac[6];         /* Source MAC address */
    uint8_t dst_mac[6];         /* Destination MAC address */
    bool rewrite_mac;           /* Whether to rewrite MAC */
} xdp_redirect_rule_t;

/* TC filter rule */
typedef struct {
    uint32_t ifindex;           /* Interface index */
    uint32_t priority;          /* Filter priority */
    uint32_t protocol;          /* Protocol (ETH_P_IP, etc.) */
    uint32_t src_ip;            /* Source IP (network order) */
    uint32_t dst_ip;            /* Destination IP (network order) */
    uint16_t src_port;          /* Source port (host order) */
    uint16_t dst_port;          /* Destination port (host order) */
    xdp_action_t action;        /* Action to take */
} tc_filter_rule_t;

/* VM fast path entry */
typedef struct {
    char vm1_tap[64];           /* First VM TAP interface */
    char vm2_tap[64];           /* Second VM TAP interface */
    uint32_t vm1_ifindex;       /* First VM interface index */
    uint32_t vm2_ifindex;       /* Second VM interface index */
} vm_fastpath_entry_t;

/* Statistics */
typedef struct {
    uint64_t packets_received;
    uint64_t packets_redirected;
    uint64_t packets_dropped;
    uint64_t packets_passed;
    uint64_t bytes_received;
    uint64_t bytes_redirected;
    uint64_t errors;
} ebpf_stats_t;

/* Per-interface statistics */
typedef struct {
    uint32_t ifindex;
    ebpf_stats_t stats;
} ebpf_if_stats_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize eBPF acceleration subsystem
 *
 * @return EBPF_OK on success, error code on failure
 */
int ebpf_accel_init(void);

/**
 * Cleanup eBPF acceleration subsystem
 */
void ebpf_accel_cleanup(void);

/**
 * Check if eBPF acceleration is supported
 *
 * @return true if supported
 */
bool ebpf_accel_supported(void);

/**
 * Check if initialized
 *
 * @return true if initialized
 */
bool ebpf_accel_is_initialized(void);

/* ============================================================================
 * XDP Redirect
 * ============================================================================ */

/**
 * Add XDP redirect rule
 *
 * @param rule Redirect rule
 * @return EBPF_OK on success
 */
int ebpf_xdp_add_redirect(const xdp_redirect_rule_t *rule);

/**
 * Remove XDP redirect rule
 *
 * @param src_ifindex Source interface index
 * @return EBPF_OK on success
 */
int ebpf_xdp_del_redirect(uint32_t src_ifindex);

/**
 * Get XDP redirect rule
 *
 * @param src_ifindex Source interface index
 * @param rule Output rule
 * @return EBPF_OK on success
 */
int ebpf_xdp_get_redirect(uint32_t src_ifindex, xdp_redirect_rule_t *rule);

/**
 * List all XDP redirect rules
 *
 * @param rules Output array
 * @param max_rules Maximum rules to return
 * @return Number of rules, or negative error code
 */
int ebpf_xdp_list_redirects(xdp_redirect_rule_t *rules, uint32_t max_rules);

/**
 * Attach XDP program to interface
 *
 * @param ifindex Interface index
 * @param flags Attach flags (XDP_FLAGS_*)
 * @return EBPF_OK on success
 */
int ebpf_xdp_attach(uint32_t ifindex, uint32_t flags);

/**
 * Detach XDP program from interface
 *
 * @param ifindex Interface index
 * @return EBPF_OK on success
 */
int ebpf_xdp_detach(uint32_t ifindex);

/* ============================================================================
 * TC Filtering
 * ============================================================================ */

/**
 * Add TC filter rule
 *
 * @param rule Filter rule
 * @return Rule ID on success, negative error code on failure
 */
int ebpf_tc_add_filter(const tc_filter_rule_t *rule);

/**
 * Remove TC filter rule
 *
 * @param ifindex Interface index
 * @param rule_id Rule ID
 * @return EBPF_OK on success
 */
int ebpf_tc_del_filter(uint32_t ifindex, uint32_t rule_id);

/**
 * Attach TC program to interface
 *
 * @param ifindex Interface index
 * @param ingress true for ingress, false for egress
 * @return EBPF_OK on success
 */
int ebpf_tc_attach(uint32_t ifindex, bool ingress);

/**
 * Detach TC program from interface
 *
 * @param ifindex Interface index
 * @param ingress true for ingress, false for egress
 * @return EBPF_OK on success
 */
int ebpf_tc_detach(uint32_t ifindex, bool ingress);

/* ============================================================================
 * VM Fast Path
 * ============================================================================ */

/**
 * Enable fast path between two VMs
 *
 * @param vm1_tap First VM TAP interface name
 * @param vm2_tap Second VM TAP interface name
 * @return EBPF_OK on success
 */
int ebpf_enable_vm_fastpath(const char *vm1_tap, const char *vm2_tap);

/**
 * Disable fast path between two VMs
 *
 * @param vm1_tap First VM TAP interface name
 * @param vm2_tap Second VM TAP interface name
 * @return EBPF_OK on success
 */
int ebpf_disable_vm_fastpath(const char *vm1_tap, const char *vm2_tap);

/**
 * List all VM fast path entries
 *
 * @param entries Output array
 * @param max_entries Maximum entries to return
 * @return Number of entries, or negative error code
 */
int ebpf_list_vm_fastpaths(vm_fastpath_entry_t *entries, uint32_t max_entries);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * Get global statistics
 *
 * @param stats Output statistics
 * @return EBPF_OK on success
 */
int ebpf_get_stats(ebpf_stats_t *stats);

/**
 * Get per-interface statistics
 *
 * @param ifindex Interface index
 * @param stats Output statistics
 * @return EBPF_OK on success
 */
int ebpf_get_if_stats(uint32_t ifindex, ebpf_stats_t *stats);

/**
 * Reset statistics
 *
 * @return EBPF_OK on success
 */
int ebpf_reset_stats(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get interface info by name
 *
 * @param ifname Interface name
 * @param info Output info
 * @return EBPF_OK on success
 */
int ebpf_get_interface_info(const char *ifname, netif_info_t *info);

/**
 * Get interface index by name
 *
 * @param ifname Interface name
 * @return Interface index, or 0 on error
 */
uint32_t ebpf_get_ifindex(const char *ifname);

/**
 * Get last error message
 *
 * @return Static error string
 */
const char *ebpf_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_EBPF_ACCEL_H */
