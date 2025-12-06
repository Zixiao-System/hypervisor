/*
 * libvirt_wrapper.h - C wrapper for libvirt API
 *
 * This header provides a simplified C interface to libvirt,
 * designed for easy integration with Go via cgo.
 */

#ifndef LIBVIRT_WRAPPER_H
#define LIBVIRT_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define LV_OK                 0
#define LV_ERR_CONNECT       -1
#define LV_ERR_DOMAIN        -2
#define LV_ERR_MEMORY        -3
#define LV_ERR_NOT_FOUND     -4
#define LV_ERR_INVALID_ARG   -5
#define LV_ERR_OPERATION     -6

/* Domain states (mirrors libvirt VIR_DOMAIN_*) */
#define LV_DOMAIN_NOSTATE     0
#define LV_DOMAIN_RUNNING     1
#define LV_DOMAIN_BLOCKED     2
#define LV_DOMAIN_PAUSED      3
#define LV_DOMAIN_SHUTDOWN    4
#define LV_DOMAIN_SHUTOFF     5
#define LV_DOMAIN_CRASHED     6
#define LV_DOMAIN_PMSUSPENDED 7

/* Domain info structure */
typedef struct {
    char*    uuid;
    char*    name;
    int      state;
    uint32_t vcpus;
    uint64_t memory_kb;
    uint64_t max_memory_kb;
    uint64_t cpu_time_ns;
} lv_domain_info_t;

/* Domain statistics */
typedef struct {
    uint64_t cpu_time_ns;
    uint64_t memory_used_kb;
    uint64_t memory_max_kb;
    uint64_t disk_read_bytes;
    uint64_t disk_write_bytes;
    uint64_t net_rx_bytes;
    uint64_t net_tx_bytes;
} lv_domain_stats_t;

/* Host info structure */
typedef struct {
    char*    hostname;
    uint32_t cpus;
    uint64_t memory_kb;
    uint64_t free_memory_kb;
    char*    hypervisor_type;
    uint64_t hypervisor_version;
} lv_host_info_t;

/*
 * Connection management
 */

/* Connect to hypervisor. URI can be:
 * - "qemu:///system" for local system QEMU
 * - "qemu:///session" for local user QEMU
 * - "qemu+ssh://user@host/system" for remote
 * - NULL for default connection
 */
int lv_connect(const char* uri);

/* Disconnect from hypervisor */
void lv_disconnect(void);

/* Check if connected */
int lv_is_connected(void);

/* Get last error message */
const char* lv_get_last_error(void);

/*
 * Host information
 */

/* Get host information */
int lv_get_host_info(lv_host_info_t* info);

/* Free host info structure */
void lv_free_host_info(lv_host_info_t* info);

/*
 * Domain lifecycle
 */

/* Create a domain from XML definition */
int lv_domain_create(const char* xml);

/* Define a domain (persistent) from XML */
int lv_domain_define(const char* xml);

/* Undefine (remove persistent config) a domain */
int lv_domain_undefine(const char* name);

/* Start a defined domain */
int lv_domain_start(const char* name);

/* Shutdown a domain gracefully */
int lv_domain_shutdown(const char* name);

/* Force stop a domain */
int lv_domain_destroy(const char* name);

/* Reboot a domain */
int lv_domain_reboot(const char* name);

/* Suspend (pause) a domain */
int lv_domain_suspend(const char* name);

/* Resume a suspended domain */
int lv_domain_resume(const char* name);

/*
 * Domain information
 */

/* Get domain info by name */
int lv_domain_get_info(const char* name, lv_domain_info_t* info);

/* Get domain info by UUID */
int lv_domain_get_info_by_uuid(const char* uuid, lv_domain_info_t* info);

/* Free domain info structure */
void lv_free_domain_info(lv_domain_info_t* info);

/* Get domain XML configuration */
char* lv_domain_get_xml(const char* name);

/* Get domain state */
int lv_domain_get_state(const char* name);

/* Get domain statistics */
int lv_domain_get_stats(const char* name, lv_domain_stats_t* stats);

/*
 * Domain listing
 */

/* List all domain names (both running and defined)
 * Returns number of domains, -1 on error
 * Caller must free names array with lv_free_string_list
 */
int lv_domain_list(char*** names, int* count);

/* List running domain names only */
int lv_domain_list_running(char*** names, int* count);

/* List defined (not running) domain names */
int lv_domain_list_defined(char*** names, int* count);

/* Free a string list returned by list functions */
void lv_free_string_list(char** list, int count);

/*
 * Domain modification
 */

/* Set domain vCPU count */
int lv_domain_set_vcpus(const char* name, uint32_t count);

/* Set domain memory (in KB) */
int lv_domain_set_memory(const char* name, uint64_t memory_kb);

/*
 * Storage (simplified interface)
 */

/* Attach a disk to domain */
int lv_domain_attach_disk(const char* domain, const char* source_path,
                          const char* target_dev, int readonly);

/* Detach a disk from domain */
int lv_domain_detach_disk(const char* domain, const char* target_dev);

/*
 * Network (simplified interface)
 */

/* Attach a network interface to domain */
int lv_domain_attach_network(const char* domain, const char* network_name,
                             const char* mac_address);

/* Detach a network interface from domain */
int lv_domain_detach_network(const char* domain, const char* mac_address);

#ifdef __cplusplus
}
#endif

#endif /* LIBVIRT_WRAPPER_H */
