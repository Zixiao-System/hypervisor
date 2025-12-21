/*
 * Zixiao VirtIO Block Driver - PCI Configuration
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCI configuration space access and VirtIO capability parsing.
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ZvioBlkPciParseCapabilities)
#endif

//
// PCI configuration offsets
//
#define PCI_CAP_ID_VENDOR_SPECIFIC  0x09
#define PCI_CAPABILITYLIST_OFFSET   0x34

//
// VirtIO PCI capability types
//
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

#pragma pack(push, 1)
typedef struct _VIRTIO_PCI_CAP {
    UCHAR   CapVndr;        // Generic PCI cap ID (0x09)
    UCHAR   CapNext;        // Next capability pointer
    UCHAR   CapLen;         // Length of this capability
    UCHAR   CfgType;        // VirtIO PCI cap type
    UCHAR   Bar;            // BAR number
    UCHAR   Id;             // Multiple caps of same type ID
    UCHAR   Padding[2];
    ULONG   Offset;         // Offset within BAR
    ULONG   Length;         // Length of structure
} VIRTIO_PCI_CAP, *PVIRTIO_PCI_CAP;

typedef struct _VIRTIO_PCI_NOTIFY_CAP {
    VIRTIO_PCI_CAP  Cap;
    ULONG           NotifyOffMultiplier;
} VIRTIO_PCI_NOTIFY_CAP, *PVIRTIO_PCI_NOTIFY_CAP;
#pragma pack(pop)

/*
 * ZvioBlkPciReadConfig - Read from PCI configuration space
 */
NTSTATUS
ZvioBlkPciReadConfig(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    ULONG bytesRead;

    if (!DeviceContext->BusInterface.GetBusData) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    bytesRead = DeviceContext->BusInterface.GetBusData(
        DeviceContext->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        Buffer,
        Offset,
        Length
        );

    if (bytesRead != Length) {
        ZvioBlkDbgError("PCI config read failed: offset=0x%X len=%d read=%d",
            Offset, Length, bytesRead);
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkPciParseCapabilities - Parse VirtIO PCI capabilities
 */
NTSTATUS
ZvioBlkPciParseCapabilities(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    UCHAR capOffset;
    UCHAR nextOffset;
    UCHAR capId;
    VIRTIO_PCI_CAP cap;
    VIRTIO_PCI_NOTIFY_CAP notifyCap;
    PVOID barVA;
    BOOLEAN foundCommon = FALSE;
    BOOLEAN foundNotify = FALSE;
    BOOLEAN foundIsr = FALSE;
    BOOLEAN foundDevice = FALSE;

    PAGED_CODE();

    ZvioBlkDbgPrint("Parsing VirtIO PCI capabilities");

    //
    // Find the capabilities pointer
    //
    status = ZvioBlkPciReadConfig(DeviceContext, PCI_CAPABILITYLIST_OFFSET, &capOffset, sizeof(capOffset));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ZvioBlkDbgPrint("Capabilities pointer: 0x%02X", capOffset);

    //
    // Walk the capability list
    //
    while (capOffset != 0 && capOffset != 0xFF) {
        status = ZvioBlkPciReadConfig(DeviceContext, capOffset, &capId, sizeof(capId));
        if (!NT_SUCCESS(status)) {
            return status;
        }

        status = ZvioBlkPciReadConfig(DeviceContext, capOffset + 1, &nextOffset, sizeof(nextOffset));
        if (!NT_SUCCESS(status)) {
            return status;
        }

        if (capId == PCI_CAP_ID_VENDOR_SPECIFIC) {
            status = ZvioBlkPciReadConfig(DeviceContext, capOffset, &cap, sizeof(cap));
            if (!NT_SUCCESS(status)) {
                return status;
            }

            ZvioBlkDbgPrint("VirtIO cap: type=%d bar=%d offset=0x%X len=0x%X",
                cap.CfgType, cap.Bar, cap.Offset, cap.Length);

            if (cap.Bar >= 6 || DeviceContext->BarVA[cap.Bar] == NULL) {
                ZvioBlkDbgError("Invalid BAR %d in capability", cap.Bar);
                capOffset = nextOffset;
                continue;
            }

            barVA = DeviceContext->BarVA[cap.Bar];

            switch (cap.CfgType) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                DeviceContext->CommonCfg = (PVIRTIO_PCI_COMMON_CFG)((PUCHAR)barVA + cap.Offset);
                foundCommon = TRUE;
                ZvioBlkDbgPrint("Common config at BAR%d+0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                status = ZvioBlkPciReadConfig(DeviceContext, capOffset, &notifyCap, sizeof(notifyCap));
                if (NT_SUCCESS(status)) {
                    DeviceContext->NotifyBase = (PUCHAR)barVA + cap.Offset;
                    DeviceContext->NotifyOffMultiplier = notifyCap.NotifyOffMultiplier;
                    foundNotify = TRUE;
                    ZvioBlkDbgPrint("Notify at BAR%d+0x%X, multiplier=%d",
                        cap.Bar, cap.Offset, notifyCap.NotifyOffMultiplier);
                }
                break;

            case VIRTIO_PCI_CAP_ISR_CFG:
                DeviceContext->IsrStatus = (PUCHAR)barVA + cap.Offset;
                foundIsr = TRUE;
                ZvioBlkDbgPrint("ISR status at BAR%d+0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_DEVICE_CFG:
                DeviceContext->DeviceCfg = (PVIRTIO_BLK_CONFIG)((PUCHAR)barVA + cap.Offset);
                foundDevice = TRUE;
                ZvioBlkDbgPrint("Device config at BAR%d+0x%X", cap.Bar, cap.Offset);
                break;

            default:
                break;
            }
        }

        capOffset = nextOffset;
    }

    if (!foundCommon) {
        ZvioBlkDbgError("VirtIO common config not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundNotify) {
        ZvioBlkDbgError("VirtIO notify capability not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundIsr) {
        ZvioBlkDbgError("VirtIO ISR capability not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundDevice) {
        ZvioBlkDbgError("VirtIO device config not found (required for block)");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ZvioBlkDbgPrint("VirtIO PCI capabilities parsed successfully");
    return STATUS_SUCCESS;
}
