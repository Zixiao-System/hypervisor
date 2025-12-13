/**
 * Zixiao Hypervisor - Memory Balloon Driver
 *
 * This driver implements the virtio balloon device for dynamic memory management
 * in guest VMs. It allows the host to reclaim memory from guests when needed.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#ifndef ZIXIAO_BALLOON_H
#define ZIXIAO_BALLOON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define BALLOON_OK              0
#define BALLOON_ERR_INIT       -1
#define BALLOON_ERR_NOT_INIT   -2
#define BALLOON_ERR_MEMORY     -3
#define BALLOON_ERR_INVALID    -4
#define BALLOON_ERR_DEVICE     -5
#define BALLOON_ERR_OPERATION  -6

/* Balloon statistics */
typedef struct balloon_stats {
    uint64_t swap_in;           /* Number of swap in pages */
    uint64_t swap_out;          /* Number of swap out pages */
    uint64_t major_faults;      /* Major page faults */
    uint64_t minor_faults;      /* Minor page faults */
    uint64_t free_memory;       /* Free memory in bytes */
    uint64_t total_memory;      /* Total memory in bytes */
    uint64_t available_memory;  /* Available memory in bytes */
    uint64_t disk_caches;       /* Disk caches in bytes */
    uint64_t hugetlb_allocations; /* Huge TLB allocations */
    uint64_t hugetlb_failures;  /* Huge TLB allocation failures */
} balloon_stats_t;

/* Balloon configuration */
typedef struct balloon_config {
    uint32_t num_pages;         /* Number of memory pages */
    uint32_t actual;            /* Actual number of pages in balloon */
    bool deflate_on_oom;        /* Deflate balloon on OOM */
    bool free_page_reporting;   /* Enable free page reporting */
    bool page_poison;           /* Enable page poisoning */
    bool stats_enabled;         /* Enable statistics collection */
    uint32_t stats_interval_ms; /* Statistics polling interval */
} balloon_config_t;

/* Callback for balloon resize requests from host */
typedef void (*balloon_request_callback_t)(uint64_t target_pages, void *userdata);

/* Callback for statistics reporting */
typedef void (*balloon_stats_callback_t)(const balloon_stats_t *stats, void *userdata);

/**
 * Initialize the balloon driver
 *
 * @param config Initial configuration (can be NULL for defaults)
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_init(const balloon_config_t *config);

/**
 * Cleanup and shutdown the balloon driver
 */
void balloon_cleanup(void);

/**
 * Check if balloon driver is initialized
 *
 * @return true if initialized
 */
bool balloon_is_initialized(void);

/**
 * Set target balloon size
 *
 * @param num_pages Target number of pages in balloon
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_set_num_pages(uint32_t num_pages);

/**
 * Get current balloon size
 *
 * @return Current number of pages in balloon
 */
uint32_t balloon_get_num_pages(void);

/**
 * Inflate balloon by specified number of pages
 *
 * @param num_pages Number of pages to inflate
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_inflate(uint32_t num_pages);

/**
 * Deflate balloon by specified number of pages
 *
 * @param num_pages Number of pages to deflate
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_deflate(uint32_t num_pages);

/**
 * Get balloon statistics
 *
 * @param stats Pointer to stats structure to fill
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_get_stats(balloon_stats_t *stats);

/**
 * Enable or disable balloon statistics
 *
 * @param enable true to enable, false to disable
 * @param interval_ms Polling interval in milliseconds (0 for default)
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_enable_stats(bool enable, uint32_t interval_ms);

/**
 * Register callback for host resize requests
 *
 * @param callback Callback function
 * @param userdata User data passed to callback
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_register_request_callback(balloon_request_callback_t callback, void *userdata);

/**
 * Register callback for statistics updates
 *
 * @param callback Callback function
 * @param userdata User data passed to callback
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_register_stats_callback(balloon_stats_callback_t callback, void *userdata);

/**
 * Enable deflate on OOM
 *
 * @param enable true to enable automatic deflation on OOM
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_set_deflate_on_oom(bool enable);

/**
 * Enable free page reporting
 *
 * @param enable true to enable free page reporting to host
 * @return BALLOON_OK on success, error code on failure
 */
int balloon_set_free_page_reporting(bool enable);

/**
 * Get last error message
 *
 * @return Static string describing last error
 */
const char *balloon_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_BALLOON_H */
