/**
 * Zixiao Hypervisor - XDP Redirect BPF Program
 *
 * High-performance packet redirect using XDP for VM-to-VM fast path.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* Redirect map: src_ifindex -> dst_ifindex */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u32);
} redirect_map SEC(".maps");

/* MAC rewrite map: ifindex -> new MAC addresses */
struct mac_entry {
    __u8 src_mac[6];
    __u8 dst_mac[6];
    __u8 rewrite;
    __u8 pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, struct mac_entry);
} mac_rewrite_map SEC(".maps");

/* Statistics per interface */
struct stats {
    __u64 packets;
    __u64 bytes;
    __u64 drops;
    __u64 redirects;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct stats);
} stats_map SEC(".maps");

/* Device map for redirect */
struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, __u32);
} devmap SEC(".maps");

static __always_inline void update_stats(__u32 ifindex, __u64 bytes, int redirected) {
    struct stats *s = bpf_map_lookup_elem(&stats_map, &ifindex);
    if (s) {
        s->packets++;
        s->bytes += bytes;
        if (redirected)
            s->redirects++;
    } else {
        struct stats new_stats = {
            .packets = 1,
            .bytes = bytes,
            .drops = 0,
            .redirects = redirected ? 1 : 0,
        };
        bpf_map_update_elem(&stats_map, &ifindex, &new_stats, BPF_ANY);
    }
}

static __always_inline int rewrite_mac(void *data, void *data_end, __u32 ifindex) {
    struct mac_entry *entry = bpf_map_lookup_elem(&mac_rewrite_map, &ifindex);
    if (!entry || !entry->rewrite)
        return 0;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return -1;

    /* Rewrite source and destination MAC */
    __builtin_memcpy(eth->h_source, entry->src_mac, ETH_ALEN);
    __builtin_memcpy(eth->h_dest, entry->dst_mac, ETH_ALEN);

    return 0;
}

SEC("xdp")
int xdp_redirect_prog(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u32 ifindex = ctx->ingress_ifindex;
    __u64 pkt_len = data_end - data;

    /* Parse Ethernet header */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }

    /* Look up redirect target */
    __u32 *dst_ifindex = bpf_map_lookup_elem(&redirect_map, &ifindex);
    if (!dst_ifindex) {
        /* No redirect rule, pass to kernel */
        update_stats(ifindex, pkt_len, 0);
        return XDP_PASS;
    }

    /* Optional MAC rewrite */
    if (rewrite_mac(data, data_end, ifindex) < 0) {
        return XDP_DROP;
    }

    /* Update statistics */
    update_stats(ifindex, pkt_len, 1);

    /* Redirect packet to destination interface */
    return bpf_redirect_map(&devmap, *dst_ifindex, 0);
}

/* VM fast path program - optimized for same-host VM traffic */
SEC("xdp")
int xdp_vm_fastpath(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u32 ifindex = ctx->ingress_ifindex;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    /* Only process IPv4/IPv6 */
    __u16 proto = bpf_ntohs(eth->h_proto);
    if (proto != ETH_P_IP && proto != ETH_P_IPV6)
        return XDP_PASS;

    /* Look up redirect target */
    __u32 *dst = bpf_map_lookup_elem(&redirect_map, &ifindex);
    if (!dst)
        return XDP_PASS;

    /* Direct redirect without modification for VM-to-VM */
    return bpf_redirect_map(&devmap, *dst, 0);
}

char _license[] SEC("license") = "GPL";
