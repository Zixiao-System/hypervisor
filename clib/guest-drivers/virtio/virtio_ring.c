/**
 * Zixiao Hypervisor - VirtIO Ring Implementation
 *
 * Copyright (C) 2024 Zixiao Team
 * Licensed under Apache License 2.0
 */

#include "virtio.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* VirtIO ring structures (from virtio spec) */
struct vring_desc {
    uint64_t addr;      /* Buffer address */
    uint32_t len;       /* Buffer length */
    uint16_t flags;     /* Descriptor flags */
    uint16_t next;      /* Next descriptor index */
};

#define VRING_DESC_F_NEXT       1   /* More descriptors follow */
#define VRING_DESC_F_WRITE      2   /* Device writes to buffer */
#define VRING_DESC_F_INDIRECT   4   /* Indirect descriptor */

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];    /* Available ring */
};

#define VRING_AVAIL_F_NO_INTERRUPT  1

struct vring_used_elem {
    uint32_t id;        /* Descriptor index */
    uint32_t len;       /* Bytes written */
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

#define VRING_USED_F_NO_NOTIFY  1

/* Global error string */
static char virtio_last_error[256] = {0};

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(virtio_last_error, sizeof(virtio_last_error), fmt, args);
    va_end(args);
}

/* Calculate vring sizes */
static size_t vring_size(uint16_t num, size_t align) {
    size_t desc_size = sizeof(struct vring_desc) * num;
    size_t avail_size = sizeof(struct vring_avail) + sizeof(uint16_t) * (num + 1);
    size_t used_size = sizeof(struct vring_used) + sizeof(struct vring_used_elem) * num;

    size_t size = desc_size;
    size = (size + avail_size + align - 1) & ~(align - 1);
    size = (size + used_size + align - 1) & ~(align - 1);

    return size;
}

/* ============================================================================
 * Device Management
 * ============================================================================ */

int virtio_device_init(virtio_device_t *dev, const virtio_device_ops_t *ops) {
    if (!dev || !ops) {
        set_error("Invalid device or ops pointer");
        return VIRTIO_ERR_INVALID;
    }

    memset(dev, 0, sizeof(*dev));
    dev->ops = ops;
    dev->status = 0;

    /* Read device features */
    if (ops->get_features) {
        dev->device_features = ops->get_features(dev);
    }

    /* Set ACKNOWLEDGE status */
    dev->status = VIRTIO_STATUS_ACKNOWLEDGE;
    if (ops->set_status) {
        ops->set_status(dev, dev->status);
    }

    return VIRTIO_OK;
}

void virtio_device_cleanup(virtio_device_t *dev) {
    if (!dev) return;

    /* Destroy all queues */
    if (dev->queues) {
        for (uint16_t i = 0; i < dev->num_queues; i++) {
            if (dev->queues[i]) {
                virtio_destroy_queue(dev->queues[i]);
            }
        }
        free(dev->queues);
        dev->queues = NULL;
    }

    /* Reset device */
    if (dev->ops && dev->ops->reset) {
        dev->ops->reset(dev);
    }

    dev->num_queues = 0;
    dev->status = 0;
}

void virtio_device_reset(virtio_device_t *dev) {
    if (!dev) return;

    if (dev->ops && dev->ops->reset) {
        dev->ops->reset(dev);
    }

    dev->status = 0;
    dev->driver_features = 0;
}

int virtio_device_ready(virtio_device_t *dev) {
    if (!dev) {
        set_error("Invalid device");
        return VIRTIO_ERR_INVALID;
    }

    /* Set DRIVER_OK status */
    dev->status |= VIRTIO_STATUS_DRIVER_OK;
    if (dev->ops && dev->ops->set_status) {
        dev->ops->set_status(dev, dev->status);
    }

    return VIRTIO_OK;
}

uint64_t virtio_negotiate_features(virtio_device_t *dev, uint64_t driver_features) {
    if (!dev) return 0;

    /* Intersect driver and device features */
    dev->driver_features = dev->device_features & driver_features;

    /* Set driver features */
    if (dev->ops && dev->ops->set_features) {
        dev->ops->set_features(dev, dev->driver_features);
    }

    /* Update status */
    dev->status |= VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    if (dev->ops && dev->ops->set_status) {
        dev->ops->set_status(dev, dev->status);
    }

    return dev->driver_features;
}

bool virtio_has_feature(virtio_device_t *dev, uint64_t feature) {
    if (!dev) return false;
    return (dev->driver_features & (1ULL << feature)) != 0;
}

/* ============================================================================
 * Virtqueue Management
 * ============================================================================ */

virtqueue_t *virtio_create_queue(virtio_device_t *dev, uint16_t index,
                                  uint16_t num_entries,
                                  virtio_callback_t callback,
                                  void *callback_data) {
    if (!dev || num_entries == 0 || (num_entries & (num_entries - 1))) {
        set_error("Invalid parameters or num_entries not power of 2");
        return NULL;
    }

    /* Allocate queue structure */
    virtqueue_t *vq = calloc(1, sizeof(*vq));
    if (!vq) {
        set_error("Failed to allocate virtqueue");
        return NULL;
    }

    vq->dev = dev;
    vq->index = index;
    vq->num_entries = num_entries;
    vq->free_count = num_entries;
    vq->callback = callback;
    vq->callback_data = callback_data;

    /* Allocate vring memory (4KB aligned) */
    size_t ring_size = vring_size(num_entries, 4096);
    void *ring_mem = aligned_alloc(4096, ring_size);
    if (!ring_mem) {
        set_error("Failed to allocate vring memory");
        free(vq);
        return NULL;
    }
    memset(ring_mem, 0, ring_size);

    /* Setup ring pointers */
    vq->desc = (struct vring_desc *)ring_mem;
    size_t avail_offset = sizeof(struct vring_desc) * num_entries;
    vq->avail = (struct vring_avail *)((char *)ring_mem + avail_offset);
    size_t used_offset = (avail_offset + sizeof(struct vring_avail) +
                          sizeof(uint16_t) * (num_entries + 1) + 4095) & ~4095UL;
    vq->used = (struct vring_used *)((char *)ring_mem + used_offset);

    /* Initialize free list */
    for (uint16_t i = 0; i < num_entries - 1; i++) {
        vq->desc[i].next = i + 1;
    }

    /* Allocate descriptor state tracking */
    vq->desc_state = calloc(num_entries, sizeof(void *));
    if (!vq->desc_state) {
        set_error("Failed to allocate descriptor state");
        free(ring_mem);
        free(vq);
        return NULL;
    }

    /* Add to device queue list */
    if (index >= dev->num_queues) {
        uint16_t new_count = index + 1;
        virtqueue_t **new_queues = realloc(dev->queues, new_count * sizeof(virtqueue_t *));
        if (!new_queues) {
            set_error("Failed to expand queue array");
            free(vq->desc_state);
            free(ring_mem);
            free(vq);
            return NULL;
        }
        for (uint16_t i = dev->num_queues; i < new_count; i++) {
            new_queues[i] = NULL;
        }
        dev->queues = new_queues;
        dev->num_queues = new_count;
    }
    dev->queues[index] = vq;

    return vq;
}

void virtio_destroy_queue(virtqueue_t *vq) {
    if (!vq) return;

    /* Remove from device */
    if (vq->dev && vq->dev->queues && vq->index < vq->dev->num_queues) {
        vq->dev->queues[vq->index] = NULL;
    }

    /* Free resources */
    free(vq->desc_state);
    free(vq->desc);  /* This frees the entire ring memory */
    free(vq);
}

int virtio_add_buf(virtqueue_t *vq, virtio_buf_t *bufs, uint32_t num_bufs, void *cookie) {
    if (!vq || !bufs || num_bufs == 0) {
        set_error("Invalid parameters");
        return VIRTIO_ERR_INVALID;
    }

    if (vq->free_count < num_bufs) {
        set_error("Not enough free descriptors");
        return VIRTIO_ERR_QUEUE;
    }

    uint16_t head = vq->avail->ring[vq->avail->idx % vq->num_entries];
    uint16_t desc_idx = head;

    for (uint32_t i = 0; i < num_bufs; i++) {
        vq->desc[desc_idx].addr = (uint64_t)(uintptr_t)bufs[i].addr;
        vq->desc[desc_idx].len = bufs[i].len;
        vq->desc[desc_idx].flags = bufs[i].writable ? VRING_DESC_F_WRITE : 0;

        if (i < num_bufs - 1) {
            vq->desc[desc_idx].flags |= VRING_DESC_F_NEXT;
            desc_idx = vq->desc[desc_idx].next;
        }
    }

    /* Store cookie for this chain */
    vq->desc_state[head] = cookie;

    /* Update available ring */
    vq->avail->ring[vq->avail->idx % vq->num_entries] = head;
    __sync_synchronize();  /* Memory barrier */
    vq->avail->idx++;

    vq->free_count -= num_bufs;

    return VIRTIO_OK;
}

void *virtio_get_buf(virtqueue_t *vq, uint32_t *len) {
    if (!vq) return NULL;

    if (vq->last_used_idx == vq->used->idx) {
        return NULL;  /* No completed buffers */
    }

    __sync_synchronize();  /* Memory barrier */

    struct vring_used_elem *elem = &vq->used->ring[vq->last_used_idx % vq->num_entries];
    uint32_t id = elem->id;

    if (len) {
        *len = elem->len;
    }

    void *cookie = vq->desc_state[id];
    vq->desc_state[id] = NULL;

    /* Return descriptors to free list */
    uint16_t desc_idx = id;
    while (vq->desc[desc_idx].flags & VRING_DESC_F_NEXT) {
        vq->free_count++;
        desc_idx = vq->desc[desc_idx].next;
    }
    vq->free_count++;

    vq->last_used_idx++;

    return cookie;
}

void virtio_kick(virtqueue_t *vq) {
    if (!vq || !vq->dev) return;

    __sync_synchronize();  /* Memory barrier */

    /* Check if notification is needed */
    if (!(vq->used->flags & VRING_USED_F_NO_NOTIFY)) {
        if (vq->dev->ops && vq->dev->ops->notify) {
            vq->dev->ops->notify(vq->dev, vq->index);
        }
    }
}

bool virtio_more_used(virtqueue_t *vq) {
    if (!vq) return false;
    return vq->last_used_idx != vq->used->idx;
}

void virtio_enable_cb(virtqueue_t *vq, bool enable) {
    if (!vq) return;

    if (enable) {
        vq->avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    } else {
        vq->avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
    }
    __sync_synchronize();
}

void virtio_process_queue(virtqueue_t *vq) {
    if (!vq) return;

    while (virtio_more_used(vq)) {
        if (vq->callback) {
            vq->callback(vq, vq->callback_data);
        }
    }
}

/* ============================================================================
 * Config Space Access
 * ============================================================================ */

void virtio_read_config(virtio_device_t *dev, uint32_t offset, void *buf, uint32_t len) {
    if (!dev || !buf) return;

    if (dev->ops && dev->ops->get_config) {
        dev->ops->get_config(dev, offset, buf, len);
    }
}

void virtio_write_config(virtio_device_t *dev, uint32_t offset, const void *buf, uint32_t len) {
    if (!dev || !buf) return;

    if (dev->ops && dev->ops->set_config) {
        dev->ops->set_config(dev, offset, buf, len);
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *virtio_device_type_name(uint32_t type) {
    switch (type) {
        case VIRTIO_DEV_NET:      return "network";
        case VIRTIO_DEV_BLK:      return "block";
        case VIRTIO_DEV_CONSOLE:  return "console";
        case VIRTIO_DEV_ENTROPY:  return "entropy";
        case VIRTIO_DEV_BALLOON:  return "balloon";
        case VIRTIO_DEV_SCSI:     return "scsi";
        case VIRTIO_DEV_9P:       return "9p";
        case VIRTIO_DEV_GPU:      return "gpu";
        case VIRTIO_DEV_INPUT:    return "input";
        case VIRTIO_DEV_VSOCK:    return "vsock";
        case VIRTIO_DEV_CRYPTO:   return "crypto";
        case VIRTIO_DEV_FS:       return "filesystem";
        default:                  return "unknown";
    }
}

const char *virtio_get_last_error(void) {
    return virtio_last_error[0] ? virtio_last_error : "No error";
}
