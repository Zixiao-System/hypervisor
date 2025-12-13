/**
 * Zixiao Hypervisor - TC Filter BPF Program
 *
 * Traffic control filtering for rate limiting, QoS, and security.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* Filter rule structure */
struct filter_rule {
    __u32 src_ip;           /* Source IP (0 = any) */
    __u32 dst_ip;           /* Destination IP (0 = any) */
    __u16 src_port;         /* Source port (0 = any) */
    __u16 dst_port;         /* Destination port (0 = any) */
    __u8  protocol;         /* IP protocol (0 = any) */
    __u8  action;           /* 0=pass, 1=drop, 2=redirect */
    __u16 priority;         /* Rule priority */
    __u32 redirect_ifindex; /* Redirect target (if action=2) */
};

/* Filter rules map: rule_id -> filter_rule */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, struct filter_rule);
} filter_rules SEC(".maps");

/* Per-interface rule list */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, __u32);  /* rule_id */
} if_rules SEC(".maps");

/* Rate limiting state */
struct rate_limit {
    __u64 tokens;           /* Available tokens */
    __u64 last_update;      /* Last update timestamp (ns) */
    __u64 rate;             /* Tokens per second */
    __u64 burst;            /* Maximum burst size */
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);     /* ifindex or flow hash */
    __type(value, struct rate_limit);
} rate_limits SEC(".maps");

/* Statistics */
struct tc_stats {
    __u64 packets_passed;
    __u64 packets_dropped;
    __u64 packets_redirected;
    __u64 bytes_passed;
    __u64 bytes_dropped;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct tc_stats);
} tc_stats_map SEC(".maps");

static __always_inline void update_tc_stats(__u32 ifindex, __u64 bytes, int action) {
    struct tc_stats *s = bpf_map_lookup_elem(&tc_stats_map, &ifindex);
    if (!s) {
        struct tc_stats new_stats = {0};
        if (action == TC_ACT_OK) {
            new_stats.packets_passed = 1;
            new_stats.bytes_passed = bytes;
        } else if (action == TC_ACT_SHOT) {
            new_stats.packets_dropped = 1;
            new_stats.bytes_dropped = bytes;
        } else {
            new_stats.packets_redirected = 1;
        }
        bpf_map_update_elem(&tc_stats_map, &ifindex, &new_stats, BPF_ANY);
        return;
    }

    if (action == TC_ACT_OK) {
        s->packets_passed++;
        s->bytes_passed += bytes;
    } else if (action == TC_ACT_SHOT) {
        s->packets_dropped++;
        s->bytes_dropped += bytes;
    } else {
        s->packets_redirected++;
    }
}

static __always_inline int check_rate_limit(__u32 key, __u64 pkt_len) {
    struct rate_limit *rl = bpf_map_lookup_elem(&rate_limits, &key);
    if (!rl)
        return 1;  /* No rate limit, allow */

    __u64 now = bpf_ktime_get_ns();
    __u64 elapsed = now - rl->last_update;

    /* Refill tokens */
    __u64 new_tokens = (elapsed * rl->rate) / 1000000000ULL;
    rl->tokens += new_tokens;
    if (rl->tokens > rl->burst)
        rl->tokens = rl->burst;

    rl->last_update = now;

    /* Check if we have enough tokens */
    if (rl->tokens >= pkt_len) {
        rl->tokens -= pkt_len;
        return 1;  /* Allow */
    }

    return 0;  /* Drop */
}

static __always_inline int match_rule(struct filter_rule *rule,
                                       __u32 src_ip, __u32 dst_ip,
                                       __u16 src_port, __u16 dst_port,
                                       __u8 protocol) {
    if (rule->src_ip && rule->src_ip != src_ip)
        return 0;
    if (rule->dst_ip && rule->dst_ip != dst_ip)
        return 0;
    if (rule->src_port && rule->src_port != src_port)
        return 0;
    if (rule->dst_port && rule->dst_port != dst_port)
        return 0;
    if (rule->protocol && rule->protocol != protocol)
        return 0;
    return 1;
}

SEC("tc")
int tc_filter_prog(struct __sk_buff *skb) {
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 ifindex = skb->ifindex;
    __u64 pkt_len = skb->len;

    /* Parse Ethernet header */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;

    /* Only process IPv4 */
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return TC_ACT_OK;

    /* Parse IP header */
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;

    __u32 src_ip = ip->saddr;
    __u32 dst_ip = ip->daddr;
    __u8 protocol = ip->protocol;
    __u16 src_port = 0, dst_port = 0;

    /* Parse L4 header for ports */
    if (protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = (void *)ip + (ip->ihl * 4);
        if ((void *)(tcp + 1) > data_end)
            return TC_ACT_OK;
        src_port = bpf_ntohs(tcp->source);
        dst_port = bpf_ntohs(tcp->dest);
    } else if (protocol == IPPROTO_UDP) {
        struct udphdr *udp = (void *)ip + (ip->ihl * 4);
        if ((void *)(udp + 1) > data_end)
            return TC_ACT_OK;
        src_port = bpf_ntohs(udp->source);
        dst_port = bpf_ntohs(udp->dest);
    }

    /* Check rate limiting */
    if (!check_rate_limit(ifindex, pkt_len)) {
        update_tc_stats(ifindex, pkt_len, TC_ACT_SHOT);
        return TC_ACT_SHOT;
    }

    /* Check filter rules (simplified - real impl would iterate) */
    __u32 *rule_id = bpf_map_lookup_elem(&if_rules, &ifindex);
    if (rule_id) {
        struct filter_rule *rule = bpf_map_lookup_elem(&filter_rules, rule_id);
        if (rule && match_rule(rule, src_ip, dst_ip, src_port, dst_port, protocol)) {
            int action;
            switch (rule->action) {
                case 1:  /* Drop */
                    action = TC_ACT_SHOT;
                    update_tc_stats(ifindex, pkt_len, action);
                    return action;
                case 2:  /* Redirect */
                    action = bpf_redirect(rule->redirect_ifindex, 0);
                    update_tc_stats(ifindex, pkt_len, TC_ACT_REDIRECT);
                    return action;
                default: /* Pass */
                    break;
            }
        }
    }

    update_tc_stats(ifindex, pkt_len, TC_ACT_OK);
    return TC_ACT_OK;
}

/* Egress filter for outbound traffic shaping */
SEC("tc")
int tc_egress_filter(struct __sk_buff *skb) {
    __u32 ifindex = skb->ifindex;
    __u64 pkt_len = skb->len;

    /* Apply egress rate limiting */
    __u32 egress_key = ifindex | 0x80000000;  /* Mark as egress */
    if (!check_rate_limit(egress_key, pkt_len)) {
        return TC_ACT_SHOT;
    }

    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
