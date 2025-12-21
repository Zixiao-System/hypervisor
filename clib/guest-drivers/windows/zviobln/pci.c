/*
 * Zixiao VirtIO Balloon Driver - PCI Configuration
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCI capability parsing for VirtIO balloon device.
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
 * ZvioBlnPciReadConfig - Read from PCI configuration space
 */
NTSTATUS
ZvioBlnPciReadConfig(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
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
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlnPciParseCapabilities - Parse VirtIO PCI capabilities
 */
NTSTATUS
ZvioBlnPciParseCapabilities(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    UCHAR capPtr;
    VIRTIO_PCI_CAP cap;
    VIRTIO_PCI_NOTIFY_CAP notifyCap;
    PVOID barVA;
    BOOLEAN foundCommon = FALSE;
    BOOLEAN foundNotify = FALSE;
    BOOLEAN foundIsr = FALSE;
    BOOLEAN foundDevice = FALSE;

    ZvioBlnDbgPrint("Parsing VirtIO PCI capabilities");

    //
    // Read capability pointer from PCI config
    //
    status = ZvioBlnPciReadConfig(DeviceContext, 0x34, &capPtr, sizeof(capPtr));
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to read capability pointer");
        goto Fallback;
    }

    //
    // Walk capability list
    //
    while (capPtr != 0) {
        status = ZvioBlnPciReadConfig(DeviceContext, capPtr, &cap, sizeof(cap));
        if (!NT_SUCCESS(status)) {
            break;
        }

        if (cap.CapVndr == PCI_CAP_ID_VENDOR_SPECIFIC) {
            barVA = DeviceContext->BarVA[cap.Bar];

            if (!barVA) {
                ZvioBlnDbgPrint("BAR%u not mapped for capability type %u", cap.Bar, cap.CfgType);
                capPtr = cap.CapNext;
                continue;
            }

            switch (cap.CfgType) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                DeviceContext->CommonCfg = (PVIRTIO_PCI_COMMON_CFG)((PUCHAR)barVA + cap.Offset);
                foundCommon = TRUE;
                ZvioBlnDbgPrint("Common config: BAR%u offset 0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                status = ZvioBlnPciReadConfig(DeviceContext, capPtr, &notifyCap, sizeof(notifyCap));
                if (NT_SUCCESS(status)) {
                    DeviceContext->NotifyBase = (PUCHAR)barVA + cap.Offset;
                    DeviceContext->NotifyOffMultiplier = notifyCap.NotifyOffMultiplier;
                    foundNotify = TRUE;
                    ZvioBlnDbgPrint("Notify config: BAR%u offset 0x%X mult %u",
                        cap.Bar, cap.Offset, notifyCap.NotifyOffMultiplier);
                }
                break;

            case VIRTIO_PCI_CAP_ISR_CFG:
                DeviceContext->IsrStatus = (PUCHAR)barVA + cap.Offset;
                foundIsr = TRUE;
                ZvioBlnDbgPrint("ISR config: BAR%u offset 0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_DEVICE_CFG:
                DeviceContext->DeviceCfg = (PVIRTIO_BALLOON_CONFIG)((PUCHAR)barVA + cap.Offset);
                foundDevice = TRUE;
                ZvioBlnDbgPrint("Device config: BAR%u offset 0x%X", cap.Bar, cap.Offset);
                break;

            default:
                break;
            }
        }

        capPtr = cap.CapNext;
    }

    if (foundCommon && foundNotify) {
        ZvioBlnDbgPrint("VirtIO PCI capabilities parsed successfully");
        return STATUS_SUCCESS;
    }

Fallback:
    //
    // Fallback to assumed offsets in BAR0 (for simplified VirtIO devices)
    //
    ZvioBlnDbgPrint("Using fallback BAR0 layout");

    if (!DeviceContext->BarVA[0]) {
        ZvioBlnDbgError("BAR0 not mapped");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    // Common config at start of BAR0
    DeviceContext->CommonCfg = (PVIRTIO_PCI_COMMON_CFG)DeviceContext->BarVA[0];

    // Notify base at offset 0x1000
    DeviceContext->NotifyBase = (PUCHAR)DeviceContext->BarVA[0] + 0x1000;
    DeviceContext->NotifyOffMultiplier = 4;

    // ISR status at offset 0x2000
    DeviceContext->IsrStatus = (PUCHAR)DeviceContext->BarVA[0] + 0x2000;

    // Device config at offset 0x3000 (balloon config)
    if (DeviceContext->BarLength[0] > 0x3000 + sizeof(VIRTIO_BALLOON_CONFIG)) {
        DeviceContext->DeviceCfg = (PVIRTIO_BALLOON_CONFIG)((PUCHAR)DeviceContext->BarVA[0] + 0x3000);
    }

    ZvioBlnDbgPrint("VirtIO PCI capabilities (fallback) parsed");
    return STATUS_SUCCESS;
}
