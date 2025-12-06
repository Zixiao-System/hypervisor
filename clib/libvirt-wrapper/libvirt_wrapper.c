/*
 * libvirt_wrapper.c - C wrapper implementation for libvirt API
 */

#include "libvirt_wrapper.h"
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Global connection handle */
static virConnectPtr g_conn = NULL;
static char g_last_error[1024] = {0};

/* Helper to set error message */
static void set_error(const char* msg) {
    virErrorPtr err = virGetLastError();
    if (err && err->message) {
        snprintf(g_last_error, sizeof(g_last_error), "%s: %s", msg, err->message);
    } else {
        strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    }
}

/*
 * Connection management
 */

int lv_connect(const char* uri) {
    if (g_conn != NULL) {
        return LV_OK; /* Already connected */
    }

    g_conn = virConnectOpen(uri);
    if (g_conn == NULL) {
        set_error("Failed to connect to hypervisor");
        return LV_ERR_CONNECT;
    }

    return LV_OK;
}

void lv_disconnect(void) {
    if (g_conn != NULL) {
        virConnectClose(g_conn);
        g_conn = NULL;
    }
}

int lv_is_connected(void) {
    return g_conn != NULL ? 1 : 0;
}

const char* lv_get_last_error(void) {
    return g_last_error;
}

/*
 * Host information
 */

int lv_get_host_info(lv_host_info_t* info) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    if (info == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(lv_host_info_t));

    /* Get hostname */
    char* hostname = virConnectGetHostname(g_conn);
    if (hostname) {
        info->hostname = strdup(hostname);
        free(hostname);
    }

    /* Get hypervisor type */
    const char* type = virConnectGetType(g_conn);
    if (type) {
        info->hypervisor_type = strdup(type);
    }

    /* Get hypervisor version */
    unsigned long version;
    if (virConnectGetVersion(g_conn, &version) == 0) {
        info->hypervisor_version = version;
    }

    /* Get node info */
    virNodeInfo node_info;
    if (virNodeGetInfo(g_conn, &node_info) == 0) {
        info->cpus = node_info.cpus;
        info->memory_kb = node_info.memory;
    }

    /* Get free memory */
    unsigned long long free_mem = virNodeGetFreeMemory(g_conn);
    info->free_memory_kb = free_mem / 1024;

    return LV_OK;
}

void lv_free_host_info(lv_host_info_t* info) {
    if (info == NULL) return;

    if (info->hostname) {
        free(info->hostname);
        info->hostname = NULL;
    }
    if (info->hypervisor_type) {
        free(info->hypervisor_type);
        info->hypervisor_type = NULL;
    }
}

/*
 * Domain lifecycle
 */

int lv_domain_create(const char* xml) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainCreateXML(g_conn, xml, 0);
    if (dom == NULL) {
        set_error("Failed to create domain");
        return LV_ERR_DOMAIN;
    }

    virDomainFree(dom);
    return LV_OK;
}

int lv_domain_define(const char* xml) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainDefineXML(g_conn, xml);
    if (dom == NULL) {
        set_error("Failed to define domain");
        return LV_ERR_DOMAIN;
    }

    virDomainFree(dom);
    return LV_OK;
}

int lv_domain_undefine(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainUndefine(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to undefine domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_start(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainCreate(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to start domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_shutdown(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainShutdown(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to shutdown domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_destroy(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainDestroy(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to destroy domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_reboot(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainReboot(dom, 0);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to reboot domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_suspend(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainSuspend(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to suspend domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_resume(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainResume(dom);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to resume domain");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

/*
 * Domain information
 */

int lv_domain_get_info(const char* name, lv_domain_info_t* info) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    if (info == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    memset(info, 0, sizeof(lv_domain_info_t));

    /* Get UUID */
    char uuid[VIR_UUID_STRING_BUFLEN];
    if (virDomainGetUUIDString(dom, uuid) == 0) {
        info->uuid = strdup(uuid);
    }

    /* Get name */
    const char* dom_name = virDomainGetName(dom);
    if (dom_name) {
        info->name = strdup(dom_name);
    }

    /* Get domain info */
    virDomainInfo dom_info;
    if (virDomainGetInfo(dom, &dom_info) == 0) {
        info->state = dom_info.state;
        info->vcpus = dom_info.nrVirtCpu;
        info->memory_kb = dom_info.memory;
        info->max_memory_kb = dom_info.maxMem;
        info->cpu_time_ns = dom_info.cpuTime;
    }

    virDomainFree(dom);
    return LV_OK;
}

int lv_domain_get_info_by_uuid(const char* uuid, lv_domain_info_t* info) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByUUIDString(g_conn, uuid);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    const char* name = virDomainGetName(dom);
    virDomainFree(dom);

    if (name == NULL) {
        return LV_ERR_DOMAIN;
    }

    return lv_domain_get_info(name, info);
}

void lv_free_domain_info(lv_domain_info_t* info) {
    if (info == NULL) return;

    if (info->uuid) {
        free(info->uuid);
        info->uuid = NULL;
    }
    if (info->name) {
        free(info->name);
        info->name = NULL;
    }
}

char* lv_domain_get_xml(const char* name) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return NULL;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return NULL;
    }

    char* xml = virDomainGetXMLDesc(dom, 0);
    virDomainFree(dom);

    return xml;
}

int lv_domain_get_state(const char* name) {
    if (g_conn == NULL) {
        return -1;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        return -1;
    }

    int state, reason;
    int ret = virDomainGetState(dom, &state, &reason, 0);
    virDomainFree(dom);

    if (ret < 0) {
        return -1;
    }

    return state;
}

int lv_domain_get_stats(const char* name, lv_domain_stats_t* stats) {
    if (g_conn == NULL || stats == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    memset(stats, 0, sizeof(lv_domain_stats_t));

    /* Get basic info for CPU time */
    virDomainInfo info;
    if (virDomainGetInfo(dom, &info) == 0) {
        stats->cpu_time_ns = info.cpuTime;
        stats->memory_used_kb = info.memory;
        stats->memory_max_kb = info.maxMem;
    }

    /* Block stats would require iterating over block devices */
    /* Network stats would require iterating over interfaces */
    /* For now, we just return basic stats */

    virDomainFree(dom);
    return LV_OK;
}

/*
 * Domain listing
 */

int lv_domain_list(char*** names, int* count) {
    if (g_conn == NULL || names == NULL || count == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    virDomainPtr* domains = NULL;
    int num_domains = virConnectListAllDomains(g_conn, &domains,
        VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_INACTIVE);

    if (num_domains < 0) {
        set_error("Failed to list domains");
        return LV_ERR_OPERATION;
    }

    *count = num_domains;
    *names = (char**)malloc(sizeof(char*) * num_domains);
    if (*names == NULL) {
        for (int i = 0; i < num_domains; i++) {
            virDomainFree(domains[i]);
        }
        free(domains);
        return LV_ERR_MEMORY;
    }

    for (int i = 0; i < num_domains; i++) {
        const char* name = virDomainGetName(domains[i]);
        (*names)[i] = name ? strdup(name) : NULL;
        virDomainFree(domains[i]);
    }

    free(domains);
    return LV_OK;
}

int lv_domain_list_running(char*** names, int* count) {
    if (g_conn == NULL || names == NULL || count == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    virDomainPtr* domains = NULL;
    int num_domains = virConnectListAllDomains(g_conn, &domains,
        VIR_CONNECT_LIST_DOMAINS_ACTIVE);

    if (num_domains < 0) {
        set_error("Failed to list running domains");
        return LV_ERR_OPERATION;
    }

    *count = num_domains;
    *names = (char**)malloc(sizeof(char*) * num_domains);
    if (*names == NULL) {
        for (int i = 0; i < num_domains; i++) {
            virDomainFree(domains[i]);
        }
        free(domains);
        return LV_ERR_MEMORY;
    }

    for (int i = 0; i < num_domains; i++) {
        const char* name = virDomainGetName(domains[i]);
        (*names)[i] = name ? strdup(name) : NULL;
        virDomainFree(domains[i]);
    }

    free(domains);
    return LV_OK;
}

int lv_domain_list_defined(char*** names, int* count) {
    if (g_conn == NULL || names == NULL || count == NULL) {
        return LV_ERR_INVALID_ARG;
    }

    virDomainPtr* domains = NULL;
    int num_domains = virConnectListAllDomains(g_conn, &domains,
        VIR_CONNECT_LIST_DOMAINS_INACTIVE);

    if (num_domains < 0) {
        set_error("Failed to list defined domains");
        return LV_ERR_OPERATION;
    }

    *count = num_domains;
    *names = (char**)malloc(sizeof(char*) * num_domains);
    if (*names == NULL) {
        for (int i = 0; i < num_domains; i++) {
            virDomainFree(domains[i]);
        }
        free(domains);
        return LV_ERR_MEMORY;
    }

    for (int i = 0; i < num_domains; i++) {
        const char* name = virDomainGetName(domains[i]);
        (*names)[i] = name ? strdup(name) : NULL;
        virDomainFree(domains[i]);
    }

    free(domains);
    return LV_OK;
}

void lv_free_string_list(char** list, int count) {
    if (list == NULL) return;

    for (int i = 0; i < count; i++) {
        if (list[i]) {
            free(list[i]);
        }
    }
    free(list);
}

/*
 * Domain modification
 */

int lv_domain_set_vcpus(const char* name, uint32_t count) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainSetVcpus(dom, count);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to set vCPUs");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_set_memory(const char* name, uint64_t memory_kb) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, name);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    int ret = virDomainSetMemory(dom, memory_kb);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to set memory");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

/*
 * Storage (simplified)
 */

int lv_domain_attach_disk(const char* domain, const char* source_path,
                          const char* target_dev, int readonly) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, domain);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    /* Create disk XML */
    char xml[1024];
    snprintf(xml, sizeof(xml),
        "<disk type='file' device='disk'>"
        "  <driver name='qemu' type='qcow2'/>"
        "  <source file='%s'/>"
        "  <target dev='%s' bus='virtio'/>"
        "  %s"
        "</disk>",
        source_path, target_dev,
        readonly ? "<readonly/>" : "");

    int ret = virDomainAttachDevice(dom, xml);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to attach disk");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_detach_disk(const char* domain, const char* target_dev) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, domain);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    /* Create disk XML for detach */
    char xml[512];
    snprintf(xml, sizeof(xml),
        "<disk type='file' device='disk'>"
        "  <target dev='%s'/>"
        "</disk>",
        target_dev);

    int ret = virDomainDetachDevice(dom, xml);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to detach disk");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

/*
 * Network (simplified)
 */

int lv_domain_attach_network(const char* domain, const char* network_name,
                             const char* mac_address) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, domain);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    /* Create interface XML */
    char xml[512];
    if (mac_address && strlen(mac_address) > 0) {
        snprintf(xml, sizeof(xml),
            "<interface type='network'>"
            "  <source network='%s'/>"
            "  <mac address='%s'/>"
            "  <model type='virtio'/>"
            "</interface>",
            network_name, mac_address);
    } else {
        snprintf(xml, sizeof(xml),
            "<interface type='network'>"
            "  <source network='%s'/>"
            "  <model type='virtio'/>"
            "</interface>",
            network_name);
    }

    int ret = virDomainAttachDevice(dom, xml);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to attach network");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}

int lv_domain_detach_network(const char* domain, const char* mac_address) {
    if (g_conn == NULL) {
        set_error("Not connected");
        return LV_ERR_CONNECT;
    }

    virDomainPtr dom = virDomainLookupByName(g_conn, domain);
    if (dom == NULL) {
        set_error("Domain not found");
        return LV_ERR_NOT_FOUND;
    }

    /* Create interface XML for detach */
    char xml[256];
    snprintf(xml, sizeof(xml),
        "<interface type='network'>"
        "  <mac address='%s'/>"
        "</interface>",
        mac_address);

    int ret = virDomainDetachDevice(dom, xml);
    virDomainFree(dom);

    if (ret < 0) {
        set_error("Failed to detach network");
        return LV_ERR_OPERATION;
    }

    return LV_OK;
}
