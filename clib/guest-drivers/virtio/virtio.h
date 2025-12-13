/**
 * Zixiao Hypervisor - VirtIO Driver
 *
 * This driver implements the VirtIO transport layer for guest VMs,
 * providing high-performance paravirtualized device support.
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#ifndef ZIXIAO_VIRTIO_H
#define ZIXIAO_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VirtIO device types */
#define VIRTIO_DEV_NET          1
#define VIRTIO_DEV_BLK          2
#define VIRTIO_DEV_CONSOLE      3
#define VIRTIO_DEV_ENTROPY      4
#define VIRTIO_DEV_BALLOON      5
#define VIRTIO_DEV_SCSI         8
#define VIRTIO_DEV_9P           9
#define VIRTIO_DEV_GPU          16
#define VIRTIO_DEV_INPUT        18
#define VIRTIO_DEV_VSOCK        19
#define VIRTIO_DEV_CRYPTO       20
#define VIRTIO_DEV_FS           26

/* VirtIO feature bits */
#define VIRTIO_F_NOTIFY_ON_EMPTY    24
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_F_RING_INDIRECT_DESC 28
#define VIRTIO_F_RING_EVENT_IDX     29
#define VIRTIO_F_VERSION_1          32
#define VIRTIO_F_ACCESS_PLATFORM    33
#define VIRTIO_F_RING_PACKED        34
#define VIRTIO_F_IN_ORDER           35
#define VIRTIO_F_ORDER_PLATFORM     36
#define VIRTIO_F_SR_IOV             37
#define VIRTIO_F_NOTIFICATION_DATA  38

/* VirtIO status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED        128

/* Error codes */
#define VIRTIO_OK               0
#define VIRTIO_ERR_INIT        -1
#define VIRTIO_ERR_NOT_INIT    -2
#define VIRTIO_ERR_MEMORY      -3
#define VIRTIO_ERR_INVALID     -4
#define VIRTIO_ERR_DEVICE      -5
#define VIRTIO_ERR_QUEUE       -6
#define VIRTIO_ERR_FEATURES    -7

/* Forward declarations */
struct virtqueue;
typedef struct virtqueue virtqueue_t;

struct virtio_device;
typedef struct virtio_device virtio_device_t;

/* Buffer descriptor */
typedef struct virtio_buf {
    void *addr;         /* Buffer address */
    uint32_t len;       /* Buffer length */
    bool writable;      /* true if device can write */
} virtio_buf_t;

/* Completion callback */
typedef void (*virtio_callback_t)(virtqueue_t *vq, void *userdata);

/* VirtIO device operations */
typedef struct virtio_device_ops {
    /* Get device features */
    uint64_t (*get_features)(virtio_device_t *dev);

    /* Set driver features */
    int (*set_features)(virtio_device_t *dev, uint64_t features);

    /* Get device status */
    uint8_t (*get_status)(virtio_device_t *dev);

    /* Set device status */
    void (*set_status)(virtio_device_t *dev, uint8_t status);

    /* Reset device */
    void (*reset)(virtio_device_t *dev);

    /* Get config space */
    void (*get_config)(virtio_device_t *dev, uint32_t offset, void *buf, uint32_t len);

    /* Set config space */
    void (*set_config)(virtio_device_t *dev, uint32_t offset, const void *buf, uint32_t len);

    /* Notify queue */
    void (*notify)(virtio_device_t *dev, uint16_t queue_index);
} virtio_device_ops_t;

/* VirtIO device structure */
struct virtio_device {
    uint32_t device_type;           /* Device type ID */
    uint32_t vendor_id;             /* Vendor ID */
    uint64_t device_features;       /* Device feature bits */
    uint64_t driver_features;       /* Negotiated features */
    uint8_t status;                 /* Device status */

    uint16_t num_queues;            /* Number of virtqueues */
    virtqueue_t **queues;           /* Array of virtqueues */

    const virtio_device_ops_t *ops; /* Device operations */
    void *transport_data;           /* Transport-specific data (PCI/MMIO) */
    void *driver_data;              /* Driver-specific data */
};

/* Virtqueue structure (opaque) */
struct virtqueue {
    virtio_device_t *dev;           /* Parent device */
    uint16_t index;                 /* Queue index */
    uint16_t num_entries;           /* Number of entries */
    uint16_t free_count;            /* Free descriptors */

    /* Descriptor ring */
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;

    /* Tracking */
    uint16_t last_used_idx;
    void **desc_state;              /* Per-descriptor state */

    /* Callback */
    virtio_callback_t callback;
    void *callback_data;

    /* DMA addresses (for IOMMU) */
    uint64_t desc_dma;
    uint64_t avail_dma;
    uint64_t used_dma;
};

/* ============================================================================
 * Device Management
 * ============================================================================ */

/**
 * Initialize a VirtIO device
 *
 * @param dev Device structure to initialize
 * @param ops Device operations
 * @return VIRTIO_OK on success
 */
int virtio_device_init(virtio_device_t *dev, const virtio_device_ops_t *ops);

/**
 * Cleanup a VirtIO device
 *
 * @param dev Device to cleanup
 */
void virtio_device_cleanup(virtio_device_t *dev);

/**
 * Reset a VirtIO device
 *
 * @param dev Device to reset
 */
void virtio_device_reset(virtio_device_t *dev);

/**
 * Complete device initialization
 *
 * @param dev Device
 * @return VIRTIO_OK on success
 */
int virtio_device_ready(virtio_device_t *dev);

/**
 * Negotiate device features
 *
 * @param dev Device
 * @param driver_features Features the driver supports
 * @return Negotiated features
 */
uint64_t virtio_negotiate_features(virtio_device_t *dev, uint64_t driver_features);

/**
 * Check if a feature is enabled
 *
 * @param dev Device
 * @param feature Feature bit to check
 * @return true if feature is enabled
 */
bool virtio_has_feature(virtio_device_t *dev, uint64_t feature);

/* ============================================================================
 * Virtqueue Management
 * ============================================================================ */

/**
 * Create a virtqueue
 *
 * @param dev Parent device
 * @param index Queue index
 * @param num_entries Number of entries (must be power of 2)
 * @param callback Completion callback
 * @param callback_data Callback user data
 * @return Created virtqueue or NULL on error
 */
virtqueue_t *virtio_create_queue(virtio_device_t *dev, uint16_t index,
                                  uint16_t num_entries,
                                  virtio_callback_t callback,
                                  void *callback_data);

/**
 * Destroy a virtqueue
 *
 * @param vq Queue to destroy
 */
void virtio_destroy_queue(virtqueue_t *vq);

/**
 * Add buffers to virtqueue
 *
 * @param vq Virtqueue
 * @param bufs Array of buffers
 * @param num_bufs Number of buffers
 * @param cookie Cookie for tracking
 * @return VIRTIO_OK on success
 */
int virtio_add_buf(virtqueue_t *vq, virtio_buf_t *bufs, uint32_t num_bufs, void *cookie);

/**
 * Get completed buffer from virtqueue
 *
 * @param vq Virtqueue
 * @param len Output: bytes written by device
 * @return Cookie from virtio_add_buf, or NULL if none available
 */
void *virtio_get_buf(virtqueue_t *vq, uint32_t *len);

/**
 * Kick the virtqueue (notify device)
 *
 * @param vq Virtqueue to kick
 */
void virtio_kick(virtqueue_t *vq);

/**
 * Check if more buffers are available
 *
 * @param vq Virtqueue
 * @return true if buffers available
 */
bool virtio_more_used(virtqueue_t *vq);

/**
 * Enable/disable virtqueue interrupts
 *
 * @param vq Virtqueue
 * @param enable true to enable
 */
void virtio_enable_cb(virtqueue_t *vq, bool enable);

/**
 * Process virtqueue interrupts
 *
 * @param vq Virtqueue
 */
void virtio_process_queue(virtqueue_t *vq);

/* ============================================================================
 * Config Space Access
 * ============================================================================ */

/**
 * Read from device config space
 *
 * @param dev Device
 * @param offset Offset in config space
 * @param buf Output buffer
 * @param len Number of bytes to read
 */
void virtio_read_config(virtio_device_t *dev, uint32_t offset, void *buf, uint32_t len);

/**
 * Write to device config space
 *
 * @param dev Device
 * @param offset Offset in config space
 * @param buf Input buffer
 * @param len Number of bytes to write
 */
void virtio_write_config(virtio_device_t *dev, uint32_t offset, const void *buf, uint32_t len);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get device type name
 *
 * @param type Device type ID
 * @return Static string name
 */
const char *virtio_device_type_name(uint32_t type);

/**
 * Get last error message
 *
 * @return Static error string
 */
const char *virtio_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VIRTIO_H */
