/**
 * Zixiao Hypervisor - eBPF Loader Implementation
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "ebpf_accel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/if_link.h>

/* Internal state */
static struct {
    bool initialized;
    bool supported;
    ebpf_stats_t global_stats;
    char last_error[256];
} ebpf_state = {0};

/* Hash map for redirect rules */
#define MAX_REDIRECT_RULES 1024
static xdp_redirect_rule_t redirect_rules[MAX_REDIRECT_RULES];
static uint32_t redirect_count = 0;

/* Hash map for VM fast paths */
#define MAX_FASTPATH_ENTRIES 256
static vm_fastpath_entry_t fastpath_entries[MAX_FASTPATH_ENTRIES];
static uint32_t fastpath_count = 0;

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(ebpf_state.last_error, sizeof(ebpf_state.last_error), fmt, args);
    va_end(args);
}

/* Check if eBPF is supported */
static bool check_ebpf_support(void) {
    /* Check kernel version and BPF support */
    /* In real implementation, use bpf() syscall to verify */
    return access("/sys/fs/bpf", F_OK) == 0;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

int ebpf_accel_init(void) {
    if (ebpf_state.initialized) {
        set_error("eBPF acceleration already initialized");
        return EBPF_ERR_INIT;
    }

    ebpf_state.supported = check_ebpf_support();
    if (!ebpf_state.supported) {
        set_error("eBPF not supported on this system");
        return EBPF_ERR_INIT;
    }

    memset(&ebpf_state.global_stats, 0, sizeof(ebpf_state.global_stats));
    memset(redirect_rules, 0, sizeof(redirect_rules));
    memset(fastpath_entries, 0, sizeof(fastpath_entries));
    redirect_count = 0;
    fastpath_count = 0;

    ebpf_state.initialized = true;
    return EBPF_OK;
}

void ebpf_accel_cleanup(void) {
    if (!ebpf_state.initialized) return;

    /* Detach all XDP programs */
    for (uint32_t i = 0; i < redirect_count; i++) {
        ebpf_xdp_detach(redirect_rules[i].src_ifindex);
    }

    /* Clear state */
    memset(&ebpf_state, 0, sizeof(ebpf_state));
    redirect_count = 0;
    fastpath_count = 0;
}

bool ebpf_accel_supported(void) {
    return check_ebpf_support();
}

bool ebpf_accel_is_initialized(void) {
    return ebpf_state.initialized;
}

/* ============================================================================
 * XDP Redirect
 * ============================================================================ */

int ebpf_xdp_add_redirect(const xdp_redirect_rule_t *rule) {
    if (!ebpf_state.initialized) {
        set_error("eBPF not initialized");
        return EBPF_ERR_NOT_INIT;
    }

    if (!rule) {
        set_error("Invalid rule pointer");
        return EBPF_ERR_INVALID;
    }

    if (redirect_count >= MAX_REDIRECT_RULES) {
        set_error("Maximum redirect rules reached");
        return EBPF_ERR_MEMORY;
    }

    /* Check if rule already exists */
    for (uint32_t i = 0; i < redirect_count; i++) {
        if (redirect_rules[i].src_ifindex == rule->src_ifindex) {
            /* Update existing rule */
            redirect_rules[i] = *rule;
            return EBPF_OK;
        }
    }

    /* Add new rule */
    redirect_rules[redirect_count++] = *rule;

    /* In real implementation, update BPF map here */

    return EBPF_OK;
}

int ebpf_xdp_del_redirect(uint32_t src_ifindex) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    for (uint32_t i = 0; i < redirect_count; i++) {
        if (redirect_rules[i].src_ifindex == src_ifindex) {
            /* Remove by shifting */
            memmove(&redirect_rules[i], &redirect_rules[i + 1],
                    (redirect_count - i - 1) * sizeof(xdp_redirect_rule_t));
            redirect_count--;
            return EBPF_OK;
        }
    }

    set_error("Rule not found");
    return EBPF_ERR_INVALID;
}

int ebpf_xdp_get_redirect(uint32_t src_ifindex, xdp_redirect_rule_t *rule) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!rule) {
        return EBPF_ERR_INVALID;
    }

    for (uint32_t i = 0; i < redirect_count; i++) {
        if (redirect_rules[i].src_ifindex == src_ifindex) {
            *rule = redirect_rules[i];
            return EBPF_OK;
        }
    }

    set_error("Rule not found");
    return EBPF_ERR_INVALID;
}

int ebpf_xdp_list_redirects(xdp_redirect_rule_t *rules, uint32_t max_rules) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!rules) {
        return EBPF_ERR_INVALID;
    }

    uint32_t count = redirect_count < max_rules ? redirect_count : max_rules;
    memcpy(rules, redirect_rules, count * sizeof(xdp_redirect_rule_t));

    return (int)count;
}

int ebpf_xdp_attach(uint32_t ifindex, uint32_t flags) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    /* In real implementation:
     * 1. Load XDP program from .o file
     * 2. Attach to interface using bpf_set_link_xdp_fd()
     */

    (void)ifindex;
    (void)flags;

    return EBPF_OK;
}

int ebpf_xdp_detach(uint32_t ifindex) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    /* In real implementation:
     * bpf_set_link_xdp_fd(ifindex, -1, flags)
     */

    (void)ifindex;

    return EBPF_OK;
}

/* ============================================================================
 * TC Filtering
 * ============================================================================ */

int ebpf_tc_add_filter(const tc_filter_rule_t *rule) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!rule) {
        return EBPF_ERR_INVALID;
    }

    /* In real implementation, add TC filter via netlink */
    static uint32_t rule_id = 1;
    return rule_id++;
}

int ebpf_tc_del_filter(uint32_t ifindex, uint32_t rule_id) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    (void)ifindex;
    (void)rule_id;

    return EBPF_OK;
}

int ebpf_tc_attach(uint32_t ifindex, bool ingress) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    (void)ifindex;
    (void)ingress;

    return EBPF_OK;
}

int ebpf_tc_detach(uint32_t ifindex, bool ingress) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    (void)ifindex;
    (void)ingress;

    return EBPF_OK;
}

/* ============================================================================
 * VM Fast Path
 * ============================================================================ */

int ebpf_enable_vm_fastpath(const char *vm1_tap, const char *vm2_tap) {
    if (!ebpf_state.initialized) {
        set_error("eBPF not initialized");
        return EBPF_ERR_NOT_INIT;
    }

    if (!vm1_tap || !vm2_tap) {
        set_error("Invalid TAP interface names");
        return EBPF_ERR_INVALID;
    }

    if (fastpath_count >= MAX_FASTPATH_ENTRIES) {
        set_error("Maximum fast path entries reached");
        return EBPF_ERR_MEMORY;
    }

    /* Get interface indices */
    uint32_t vm1_idx = if_nametoindex(vm1_tap);
    uint32_t vm2_idx = if_nametoindex(vm2_tap);

    if (vm1_idx == 0 || vm2_idx == 0) {
        set_error("Failed to get interface index");
        return EBPF_ERR_INVALID;
    }

    /* Check if already exists */
    for (uint32_t i = 0; i < fastpath_count; i++) {
        if ((strcmp(fastpath_entries[i].vm1_tap, vm1_tap) == 0 &&
             strcmp(fastpath_entries[i].vm2_tap, vm2_tap) == 0) ||
            (strcmp(fastpath_entries[i].vm1_tap, vm2_tap) == 0 &&
             strcmp(fastpath_entries[i].vm2_tap, vm1_tap) == 0)) {
            return EBPF_OK; /* Already exists */
        }
    }

    /* Add entry */
    vm_fastpath_entry_t *entry = &fastpath_entries[fastpath_count++];
    strncpy(entry->vm1_tap, vm1_tap, sizeof(entry->vm1_tap) - 1);
    strncpy(entry->vm2_tap, vm2_tap, sizeof(entry->vm2_tap) - 1);
    entry->vm1_ifindex = vm1_idx;
    entry->vm2_ifindex = vm2_idx;

    /* Setup bidirectional XDP redirect */
    xdp_redirect_rule_t rule1 = {
        .src_ifindex = vm1_idx,
        .dst_ifindex = vm2_idx,
        .rewrite_mac = false
    };
    ebpf_xdp_add_redirect(&rule1);

    xdp_redirect_rule_t rule2 = {
        .src_ifindex = vm2_idx,
        .dst_ifindex = vm1_idx,
        .rewrite_mac = false
    };
    ebpf_xdp_add_redirect(&rule2);

    return EBPF_OK;
}

int ebpf_disable_vm_fastpath(const char *vm1_tap, const char *vm2_tap) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!vm1_tap || !vm2_tap) {
        return EBPF_ERR_INVALID;
    }

    for (uint32_t i = 0; i < fastpath_count; i++) {
        if ((strcmp(fastpath_entries[i].vm1_tap, vm1_tap) == 0 &&
             strcmp(fastpath_entries[i].vm2_tap, vm2_tap) == 0) ||
            (strcmp(fastpath_entries[i].vm1_tap, vm2_tap) == 0 &&
             strcmp(fastpath_entries[i].vm2_tap, vm1_tap) == 0)) {

            /* Remove redirect rules */
            ebpf_xdp_del_redirect(fastpath_entries[i].vm1_ifindex);
            ebpf_xdp_del_redirect(fastpath_entries[i].vm2_ifindex);

            /* Remove entry */
            memmove(&fastpath_entries[i], &fastpath_entries[i + 1],
                    (fastpath_count - i - 1) * sizeof(vm_fastpath_entry_t));
            fastpath_count--;
            return EBPF_OK;
        }
    }

    set_error("Fast path entry not found");
    return EBPF_ERR_INVALID;
}

int ebpf_list_vm_fastpaths(vm_fastpath_entry_t *entries, uint32_t max_entries) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!entries) {
        return EBPF_ERR_INVALID;
    }

    uint32_t count = fastpath_count < max_entries ? fastpath_count : max_entries;
    memcpy(entries, fastpath_entries, count * sizeof(vm_fastpath_entry_t));

    return (int)count;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

int ebpf_get_stats(ebpf_stats_t *stats) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!stats) {
        return EBPF_ERR_INVALID;
    }

    /* In real implementation, read from BPF maps */
    *stats = ebpf_state.global_stats;
    return EBPF_OK;
}

int ebpf_get_if_stats(uint32_t ifindex, ebpf_stats_t *stats) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    if (!stats) {
        return EBPF_ERR_INVALID;
    }

    /* In real implementation, read per-interface stats from BPF maps */
    (void)ifindex;
    memset(stats, 0, sizeof(*stats));

    return EBPF_OK;
}

int ebpf_reset_stats(void) {
    if (!ebpf_state.initialized) {
        return EBPF_ERR_NOT_INIT;
    }

    memset(&ebpf_state.global_stats, 0, sizeof(ebpf_state.global_stats));
    return EBPF_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int ebpf_get_interface_info(const char *ifname, netif_info_t *info) {
    if (!ifname || !info) {
        return EBPF_ERR_INVALID;
    }

    memset(info, 0, sizeof(*info));

    info->ifindex = if_nametoindex(ifname);
    if (info->ifindex == 0) {
        set_error("Interface not found: %s", ifname);
        return EBPF_ERR_INVALID;
    }

    strncpy(info->ifname, ifname, sizeof(info->ifname) - 1);
    info->mtu = 1500; /* Default, should read from sysfs */

    return EBPF_OK;
}

uint32_t ebpf_get_ifindex(const char *ifname) {
    if (!ifname) return 0;
    return if_nametoindex(ifname);
}

const char *ebpf_get_last_error(void) {
    return ebpf_state.last_error[0] ? ebpf_state.last_error : "No error";
}
