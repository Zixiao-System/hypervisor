/**
 * Zixiao Hypervisor - Memory Balloon Driver Implementation
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "balloon.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#endif

/* Internal state */
static struct {
    bool initialized;
    balloon_config_t config;
    uint32_t current_pages;
    void **inflated_pages;
    size_t inflated_capacity;
    balloon_request_callback_t request_callback;
    void *request_userdata;
    balloon_stats_callback_t stats_callback;
    void *stats_userdata;
    char last_error[256];
} balloon_state = {0};

/* Page size */
static size_t page_size = 0;

/* Set error message */
static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(balloon_state.last_error, sizeof(balloon_state.last_error), fmt, args);
    va_end(args);
}

/* Initialize page size */
static void init_page_size(void) {
    if (page_size == 0) {
#ifdef __linux__
        page_size = sysconf(_SC_PAGESIZE);
#else
        page_size = 4096; /* Default to 4KB */
#endif
    }
}

int balloon_init(const balloon_config_t *config) {
    if (balloon_state.initialized) {
        set_error("Balloon driver already initialized");
        return BALLOON_ERR_INIT;
    }

    init_page_size();

    /* Set default configuration */
    memset(&balloon_state, 0, sizeof(balloon_state));

    if (config) {
        balloon_state.config = *config;
    } else {
        balloon_state.config.num_pages = 0;
        balloon_state.config.actual = 0;
        balloon_state.config.deflate_on_oom = true;
        balloon_state.config.free_page_reporting = false;
        balloon_state.config.page_poison = false;
        balloon_state.config.stats_enabled = true;
        balloon_state.config.stats_interval_ms = 1000;
    }

    /* Allocate page tracking array */
    balloon_state.inflated_capacity = 1024;
    balloon_state.inflated_pages = calloc(balloon_state.inflated_capacity, sizeof(void*));
    if (!balloon_state.inflated_pages) {
        set_error("Failed to allocate page tracking array");
        return BALLOON_ERR_MEMORY;
    }

    balloon_state.initialized = true;
    return BALLOON_OK;
}

void balloon_cleanup(void) {
    if (!balloon_state.initialized) {
        return;
    }

    /* Release all inflated pages */
    if (balloon_state.inflated_pages) {
        for (uint32_t i = 0; i < balloon_state.current_pages; i++) {
            if (balloon_state.inflated_pages[i]) {
#ifdef __linux__
                munmap(balloon_state.inflated_pages[i], page_size);
#else
                free(balloon_state.inflated_pages[i]);
#endif
            }
        }
        free(balloon_state.inflated_pages);
    }

    memset(&balloon_state, 0, sizeof(balloon_state));
}

bool balloon_is_initialized(void) {
    return balloon_state.initialized;
}

int balloon_set_num_pages(uint32_t num_pages) {
    if (!balloon_state.initialized) {
        set_error("Balloon driver not initialized");
        return BALLOON_ERR_NOT_INIT;
    }

    if (num_pages > balloon_state.current_pages) {
        return balloon_inflate(num_pages - balloon_state.current_pages);
    } else if (num_pages < balloon_state.current_pages) {
        return balloon_deflate(balloon_state.current_pages - num_pages);
    }

    return BALLOON_OK;
}

uint32_t balloon_get_num_pages(void) {
    return balloon_state.current_pages;
}

int balloon_inflate(uint32_t num_pages) {
    if (!balloon_state.initialized) {
        set_error("Balloon driver not initialized");
        return BALLOON_ERR_NOT_INIT;
    }

    /* Expand capacity if needed */
    uint32_t new_total = balloon_state.current_pages + num_pages;
    if (new_total > balloon_state.inflated_capacity) {
        size_t new_capacity = balloon_state.inflated_capacity * 2;
        while (new_capacity < new_total) {
            new_capacity *= 2;
        }

        void **new_array = realloc(balloon_state.inflated_pages,
                                   new_capacity * sizeof(void*));
        if (!new_array) {
            set_error("Failed to expand page tracking array");
            return BALLOON_ERR_MEMORY;
        }

        balloon_state.inflated_pages = new_array;
        balloon_state.inflated_capacity = new_capacity;
    }

    /* Allocate pages */
    for (uint32_t i = 0; i < num_pages; i++) {
        void *page = NULL;

#ifdef __linux__
        page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) {
            set_error("Failed to allocate page: %s", strerror(errno));
            return BALLOON_ERR_MEMORY;
        }

        /* Advise kernel we don't need this memory */
        madvise(page, page_size, MADV_DONTNEED);
#else
        page = malloc(page_size);
        if (!page) {
            set_error("Failed to allocate page");
            return BALLOON_ERR_MEMORY;
        }
#endif

        balloon_state.inflated_pages[balloon_state.current_pages++] = page;
    }

    balloon_state.config.actual = balloon_state.current_pages;
    return BALLOON_OK;
}

int balloon_deflate(uint32_t num_pages) {
    if (!balloon_state.initialized) {
        set_error("Balloon driver not initialized");
        return BALLOON_ERR_NOT_INIT;
    }

    if (num_pages > balloon_state.current_pages) {
        num_pages = balloon_state.current_pages;
    }

    /* Release pages */
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t idx = balloon_state.current_pages - 1;
        void *page = balloon_state.inflated_pages[idx];

        if (page) {
#ifdef __linux__
            munmap(page, page_size);
#else
            free(page);
#endif
        }

        balloon_state.inflated_pages[idx] = NULL;
        balloon_state.current_pages--;
    }

    balloon_state.config.actual = balloon_state.current_pages;
    return BALLOON_OK;
}

int balloon_get_stats(balloon_stats_t *stats) {
    if (!balloon_state.initialized) {
        set_error("Balloon driver not initialized");
        return BALLOON_ERR_NOT_INIT;
    }

    if (!stats) {
        set_error("Invalid stats pointer");
        return BALLOON_ERR_INVALID;
    }

    memset(stats, 0, sizeof(*stats));

#ifdef __linux__
    /* Read memory info from /proc/meminfo */
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            uint64_t value;
            if (sscanf(line, "MemTotal: %lu kB", &value) == 1) {
                stats->total_memory = value * 1024;
            } else if (sscanf(line, "MemFree: %lu kB", &value) == 1) {
                stats->free_memory = value * 1024;
            } else if (sscanf(line, "MemAvailable: %lu kB", &value) == 1) {
                stats->available_memory = value * 1024;
            } else if (sscanf(line, "Cached: %lu kB", &value) == 1) {
                stats->disk_caches = value * 1024;
            } else if (sscanf(line, "SwapTotal: %lu kB", &value) == 1) {
                /* Track swap info if needed */
            }
        }
        fclose(fp);
    }

    /* Read vmstat for page fault info */
    fp = fopen("/proc/vmstat", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            uint64_t value;
            if (sscanf(line, "pgmajfault %lu", &value) == 1) {
                stats->major_faults = value;
            } else if (sscanf(line, "pgfault %lu", &value) == 1) {
                stats->minor_faults = value - stats->major_faults;
            } else if (sscanf(line, "pswpin %lu", &value) == 1) {
                stats->swap_in = value;
            } else if (sscanf(line, "pswpout %lu", &value) == 1) {
                stats->swap_out = value;
            }
        }
        fclose(fp);
    }
#endif

    return BALLOON_OK;
}

int balloon_enable_stats(bool enable, uint32_t interval_ms) {
    if (!balloon_state.initialized) {
        return BALLOON_ERR_NOT_INIT;
    }

    balloon_state.config.stats_enabled = enable;
    if (interval_ms > 0) {
        balloon_state.config.stats_interval_ms = interval_ms;
    }

    return BALLOON_OK;
}

int balloon_register_request_callback(balloon_request_callback_t callback, void *userdata) {
    if (!balloon_state.initialized) {
        return BALLOON_ERR_NOT_INIT;
    }

    balloon_state.request_callback = callback;
    balloon_state.request_userdata = userdata;
    return BALLOON_OK;
}

int balloon_register_stats_callback(balloon_stats_callback_t callback, void *userdata) {
    if (!balloon_state.initialized) {
        return BALLOON_ERR_NOT_INIT;
    }

    balloon_state.stats_callback = callback;
    balloon_state.stats_userdata = userdata;
    return BALLOON_OK;
}

int balloon_set_deflate_on_oom(bool enable) {
    if (!balloon_state.initialized) {
        return BALLOON_ERR_NOT_INIT;
    }

    balloon_state.config.deflate_on_oom = enable;
    return BALLOON_OK;
}

int balloon_set_free_page_reporting(bool enable) {
    if (!balloon_state.initialized) {
        return BALLOON_ERR_NOT_INIT;
    }

    balloon_state.config.free_page_reporting = enable;
    return BALLOON_OK;
}

const char *balloon_get_last_error(void) {
    return balloon_state.last_error[0] ? balloon_state.last_error : "No error";
}
