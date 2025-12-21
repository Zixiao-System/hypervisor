/*
 * Zixiao VirtIO Network Driver - PCI Configuration
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

#define PCI_CAP_ID_VENDOR_SPECIFIC  0x09

#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

#pragma pack(push, 1)
typedef struct _VIRTIO_PCI_CAP {
    UCHAR   CapVndr;
    UCHAR   CapNext;
    UCHAR   CapLen;
    UCHAR   CfgType;
    UCHAR   Bar;
    UCHAR   Id;
    UCHAR   Padding[2];
    ULONG   Offset;
    ULONG   Length;
} VIRTIO_PCI_CAP, *PVIRTIO_PCI_CAP;

typedef struct _VIRTIO_PCI_NOTIFY_CAP {
    VIRTIO_PCI_CAP  Cap;
    ULONG           NotifyOffMultiplier;
} VIRTIO_PCI_NOTIFY_CAP, *PVIRTIO_PCI_NOTIFY_CAP;
#pragma pack(pop)

/*
 * ZvioNetPciParseCapabilities - Parse VirtIO PCI capabilities
 */
NTSTATUS
ZvioNetPciParseCapabilities(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    ULONG capOffset;
    VIRTIO_PCI_CAP cap;
    VIRTIO_PCI_NOTIFY_CAP notifyCap;
    PVOID barVA;
    BOOLEAN foundCommon = FALSE;
    BOOLEAN foundNotify = FALSE;
    BOOLEAN foundIsr = FALSE;
    BOOLEAN foundDevice = FALSE;

    ZvioNetDbgPrint("Parsing VirtIO PCI capabilities");

    //
    // Walk capability list by scanning mapped BARs for VirtIO structures
    // In a real driver, we would read PCI config space
    // For simplicity, we assume BAR0 contains VirtIO modern config
    //
    if (!Adapter->BarVA[0]) {
        ZvioNetDbgError("BAR0 not mapped");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    //
    // For VirtIO 1.0+ modern devices, the configuration is typically:
    // - Common config at offset 0
    // - ISR at some offset
    // - Device config at some offset
    // - Notify at some offset
    //
    // Since we don't have PCI config access in this simplified driver,
    // we'll use typical VirtIO modern offsets
    //

    // Assume common config at start of BAR0
    Adapter->CommonCfg = (PVIRTIO_PCI_COMMON_CFG)Adapter->BarVA[0];
    foundCommon = TRUE;

    // Notify base typically follows common config
    Adapter->NotifyBase = (PUCHAR)Adapter->BarVA[0] + 0x1000;
    Adapter->NotifyOffMultiplier = 4;
    foundNotify = TRUE;

    // ISR status
    Adapter->IsrStatus = (PUCHAR)Adapter->BarVA[0] + 0x2000;
    foundIsr = TRUE;

    // Device-specific config for network
    if (Adapter->BarLength[0] > 0x3000) {
        Adapter->DeviceCfg = (PVIRTIO_NET_CONFIG)((PUCHAR)Adapter->BarVA[0] + 0x3000);
        foundDevice = TRUE;
    }

    if (!foundCommon) {
        ZvioNetDbgError("VirtIO common config not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundNotify) {
        ZvioNetDbgError("VirtIO notify capability not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ZvioNetDbgPrint("VirtIO PCI capabilities parsed");
    return STATUS_SUCCESS;
}
