/*
 * Zixiao VirtIO PCI Bus Driver - PCI Configuration
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCI configuration space access and VirtIO capability parsing.
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ZvioPciParseCapabilities)
#endif

/*
 * ZvioPciReadConfig - Read from PCI configuration space
 */
NTSTATUS
ZvioPciReadConfig(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
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
        ZvioDbgError("PCI config read failed: offset=0x%X len=%d read=%d",
            Offset, Length, bytesRead);
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioPciWriteConfig - Write to PCI configuration space
 */
NTSTATUS
ZvioPciWriteConfig(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    ULONG bytesWritten;

    if (!DeviceContext->BusInterface.SetBusData) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    bytesWritten = DeviceContext->BusInterface.SetBusData(
        DeviceContext->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        Buffer,
        Offset,
        Length
        );

    if (bytesWritten != Length) {
        ZvioDbgError("PCI config write failed: offset=0x%X len=%d written=%d",
            Offset, Length, bytesWritten);
        return STATUS_IO_DEVICE_ERROR;
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioPciParseCapabilities - Parse VirtIO PCI capabilities
 *
 * Finds and maps the VirtIO configuration structures from PCI capabilities.
 */
NTSTATUS
ZvioPciParseCapabilities(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
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
    USHORT deviceId;
    USHORT subsystemId;

    PAGED_CODE();

    ZvioDbgPrint("Parsing VirtIO PCI capabilities");

    //
    // Read device and subsystem IDs to determine device type
    //
    status = ZvioPciReadConfig(DeviceContext, PCI_DEVICE_ID_OFFSET, &deviceId, sizeof(deviceId));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ZvioPciReadConfig(DeviceContext, PCI_SUBSYSTEM_ID_OFFSET, &subsystemId, sizeof(subsystemId));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ZvioDbgPrint("Device ID: 0x%04X, Subsystem ID: 0x%04X", deviceId, subsystemId);

    //
    // Determine device type from device ID
    //
    if (deviceId >= VIRTIO_PCI_DEVICE_ID_BASE) {
        // Modern device: type = device_id - 0x1040
        DeviceContext->DeviceType = (VIRTIO_DEVICE_TYPE)(deviceId - VIRTIO_PCI_DEVICE_ID_BASE);
    } else if (deviceId >= VIRTIO_PCI_DEVICE_ID_NET && deviceId <= VIRTIO_PCI_DEVICE_ID_9P) {
        // Transitional device: map legacy IDs
        switch (deviceId) {
        case VIRTIO_PCI_DEVICE_ID_NET:
            DeviceContext->DeviceType = VirtioDevTypeNet;
            break;
        case VIRTIO_PCI_DEVICE_ID_BLK:
            DeviceContext->DeviceType = VirtioDevTypeBlock;
            break;
        case VIRTIO_PCI_DEVICE_ID_BALLOON:
            DeviceContext->DeviceType = VirtioDevTypeBalloon;
            break;
        case VIRTIO_PCI_DEVICE_ID_CONSOLE:
            DeviceContext->DeviceType = VirtioDevTypeConsole;
            break;
        case VIRTIO_PCI_DEVICE_ID_SCSI:
            DeviceContext->DeviceType = VirtioDevTypeSCSI;
            break;
        case VIRTIO_PCI_DEVICE_ID_RNG:
            DeviceContext->DeviceType = VirtioDevTypeEntropy;
            break;
        case VIRTIO_PCI_DEVICE_ID_9P:
            DeviceContext->DeviceType = VirtioDevType9P;
            break;
        default:
            DeviceContext->DeviceType = VirtioDevTypeUnknown;
            break;
        }
    } else {
        DeviceContext->DeviceType = VirtioDevTypeUnknown;
    }

    ZvioDbgPrint("VirtIO device type: %d", DeviceContext->DeviceType);

    //
    // Find the capabilities pointer in PCI config space
    //
    status = ZvioPciReadConfig(DeviceContext, PCI_CAPABILITYLIST_OFFSET, &capOffset, sizeof(capOffset));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ZvioDbgPrint("Capabilities pointer: 0x%02X", capOffset);

    //
    // Walk the capability list
    //
    while (capOffset != 0 && capOffset != 0xFF) {
        //
        // Read capability ID
        //
        status = ZvioPciReadConfig(DeviceContext, capOffset, &capId, sizeof(capId));
        if (!NT_SUCCESS(status)) {
            return status;
        }

        //
        // Read next capability offset
        //
        status = ZvioPciReadConfig(DeviceContext, capOffset + 1, &nextOffset, sizeof(nextOffset));
        if (!NT_SUCCESS(status)) {
            return status;
        }

        //
        // Check for vendor-specific capability (VirtIO)
        //
        if (capId == PCI_CAP_ID_VENDOR_SPECIFIC) {
            //
            // Read the full VirtIO capability structure
            //
            status = ZvioPciReadConfig(DeviceContext, capOffset, &cap, sizeof(cap));
            if (!NT_SUCCESS(status)) {
                return status;
            }

            ZvioDbgPrint("VirtIO cap: type=%d bar=%d offset=0x%X len=0x%X",
                cap.CfgType, cap.Bar, cap.Offset, cap.Length);

            //
            // Validate BAR
            //
            if (cap.Bar >= 6 || DeviceContext->BarVA[cap.Bar] == NULL) {
                ZvioDbgError("Invalid BAR %d in capability", cap.Bar);
                capOffset = nextOffset;
                continue;
            }

            barVA = DeviceContext->BarVA[cap.Bar];

            //
            // Process based on capability type
            //
            switch (cap.CfgType) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                DeviceContext->CommonCfg = (PVIRTIO_PCI_COMMON_CFG)
                    ((PUCHAR)barVA + cap.Offset);
                foundCommon = TRUE;
                ZvioDbgPrint("Common config at BAR%d+0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                //
                // Notify capability has extra field
                //
                status = ZvioPciReadConfig(DeviceContext, capOffset, &notifyCap, sizeof(notifyCap));
                if (NT_SUCCESS(status)) {
                    DeviceContext->NotifyBase = (PUCHAR)barVA + cap.Offset;
                    DeviceContext->NotifyOffMultiplier = notifyCap.NotifyOffMultiplier;
                    foundNotify = TRUE;
                    ZvioDbgPrint("Notify at BAR%d+0x%X, multiplier=%d",
                        cap.Bar, cap.Offset, notifyCap.NotifyOffMultiplier);
                }
                break;

            case VIRTIO_PCI_CAP_ISR_CFG:
                DeviceContext->IsrStatus = (PUCHAR)barVA + cap.Offset;
                foundIsr = TRUE;
                ZvioDbgPrint("ISR status at BAR%d+0x%X", cap.Bar, cap.Offset);
                break;

            case VIRTIO_PCI_CAP_DEVICE_CFG:
                DeviceContext->DeviceCfg = (PUCHAR)barVA + cap.Offset;
                DeviceContext->DeviceCfgLen = cap.Length;
                foundDevice = TRUE;
                ZvioDbgPrint("Device config at BAR%d+0x%X len=%d",
                    cap.Bar, cap.Offset, cap.Length);
                break;

            case VIRTIO_PCI_CAP_PCI_CFG:
                // PCI config access capability - used for legacy devices
                ZvioDbgPrint("PCI config access capability at 0x%X", capOffset);
                break;

            case VIRTIO_PCI_CAP_SHARED_MEM_CFG:
                ZvioDbgPrint("Shared memory capability at BAR%d", cap.Bar);
                break;

            default:
                ZvioDbgPrint("Unknown VirtIO cap type %d", cap.CfgType);
                break;
            }
        }

        capOffset = nextOffset;
    }

    //
    // Verify we found required capabilities
    //
    if (!foundCommon) {
        ZvioDbgError("VirtIO common config not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundNotify) {
        ZvioDbgError("VirtIO notify capability not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!foundIsr) {
        ZvioDbgError("VirtIO ISR capability not found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    // Device config is optional (some devices don't have it)

    ZvioDbgPrint("VirtIO PCI capabilities parsed successfully");
    return STATUS_SUCCESS;
}

//
// PCI configuration space offset definitions (if not already defined)
//
#ifndef PCI_DEVICE_ID_OFFSET
#define PCI_DEVICE_ID_OFFSET            0x02
#endif

#ifndef PCI_SUBSYSTEM_ID_OFFSET
#define PCI_SUBSYSTEM_ID_OFFSET         0x2E
#endif

#ifndef PCI_CAPABILITYLIST_OFFSET
#define PCI_CAPABILITYLIST_OFFSET       0x34
#endif
