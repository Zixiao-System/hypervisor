/*
 * Zixiao VirtIO PCI Bus Driver - Bus Enumeration
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Creates child PDOs for VirtIO device types (virtio-net, virtio-blk, etc.)
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ZvioBusEnumerateChildren)
#endif

/*
 * Device type to hardware ID mapping
 */
typedef struct _ZVIO_DEVICE_INFO {
    VIRTIO_DEVICE_TYPE  Type;
    PCWSTR              HardwareId;
    PCWSTR              DeviceDesc;
    PCWSTR              CompatibleId;
} ZVIO_DEVICE_INFO, *PZVIO_DEVICE_INFO;

static const ZVIO_DEVICE_INFO g_DeviceInfoTable[] = {
    { VirtioDevTypeNet,      L"ZVIO\\NET",      L"Zixiao VirtIO Network Adapter",    L"ZVIO\\VEN_1AF4&DEV_1000" },
    { VirtioDevTypeBlock,    L"ZVIO\\BLK",      L"Zixiao VirtIO Block Device",       L"ZVIO\\VEN_1AF4&DEV_1001" },
    { VirtioDevTypeBalloon,  L"ZVIO\\BALLOON",  L"Zixiao VirtIO Balloon",            L"ZVIO\\VEN_1AF4&DEV_1002" },
    { VirtioDevTypeConsole,  L"ZVIO\\CONSOLE",  L"Zixiao VirtIO Console",            L"ZVIO\\VEN_1AF4&DEV_1003" },
    { VirtioDevTypeSCSI,     L"ZVIO\\SCSI",     L"Zixiao VirtIO SCSI Controller",    L"ZVIO\\VEN_1AF4&DEV_1004" },
    { VirtioDevTypeEntropy,  L"ZVIO\\RNG",      L"Zixiao VirtIO Random Number Gen",  L"ZVIO\\VEN_1AF4&DEV_1005" },
    { VirtioDevType9P,       L"ZVIO\\9P",       L"Zixiao VirtIO 9P Transport",       L"ZVIO\\VEN_1AF4&DEV_1009" },
    { VirtioDevTypeGPU,      L"ZVIO\\GPU",      L"Zixiao VirtIO GPU",                L"ZVIO\\VEN_1AF4&DEV_1050" },
    { VirtioDevTypeInput,    L"ZVIO\\INPUT",    L"Zixiao VirtIO Input Device",       L"ZVIO\\VEN_1AF4&DEV_1052" },
    { VirtioDevTypeVSock,    L"ZVIO\\VSOCK",    L"Zixiao VirtIO Socket",             L"ZVIO\\VEN_1AF4&DEV_1053" },
    { VirtioDevTypeFS,       L"ZVIO\\FS",       L"Zixiao VirtIO File System",        L"ZVIO\\VEN_1AF4&DEV_105A" },
    { VirtioDevTypeSound,    L"ZVIO\\SOUND",    L"Zixiao VirtIO Sound Device",       L"ZVIO\\VEN_1AF4&DEV_1059" },
};

#define DEVICE_INFO_COUNT (sizeof(g_DeviceInfoTable) / sizeof(g_DeviceInfoTable[0]))

/*
 * GetDeviceInfo - Look up device info by type
 */
static const ZVIO_DEVICE_INFO*
GetDeviceInfo(
    _In_ VIRTIO_DEVICE_TYPE Type
    )
{
    for (ULONG i = 0; i < DEVICE_INFO_COUNT; i++) {
        if (g_DeviceInfoTable[i].Type == Type) {
            return &g_DeviceInfoTable[i];
        }
    }
    return NULL;
}

/*
 * ZvioBusEnumerateChildren - Enumerate and create child PDOs
 *
 * For a multi-function VirtIO device, this would enumerate
 * each function. For a single-function device, it creates
 * one PDO for the device type.
 */
NTSTATUS
ZvioBusEnumerateChildren(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    WDFDEVICE hChild = NULL;
    PWDFDEVICE_INIT pDeviceInit = NULL;
    WDF_OBJECT_ATTRIBUTES pdoAttributes;
    PZVIO_PDO_CONTEXT pdoContext;
    const ZVIO_DEVICE_INFO *deviceInfo;
    DECLARE_UNICODE_STRING_SIZE(hardwareId, 128);
    DECLARE_UNICODE_STRING_SIZE(instanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(compatibleId, 128);
    DECLARE_UNICODE_STRING_SIZE(deviceDesc, 128);

    PAGED_CODE();

    ZvioDbgPrint("Enumerating VirtIO children, device type = %d", DeviceContext->DeviceType);

    //
    // Look up device info
    //
    deviceInfo = GetDeviceInfo(DeviceContext->DeviceType);
    if (!deviceInfo) {
        ZvioDbgPrint("Unknown device type %d, not creating PDO", DeviceContext->DeviceType);
        return STATUS_SUCCESS; // Not an error, just no child
    }

    //
    // Allocate a WDFDEVICE_INIT structure for the child PDO
    //
    pDeviceInit = WdfPdoInitAllocate(DeviceContext->Device);
    if (!pDeviceInit) {
        ZvioDbgError("WdfPdoInitAllocate failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Set device IDs
    //
    status = RtlUnicodeStringPrintf(&hardwareId, L"%s", deviceInfo->HardwareId);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = WdfPdoInitAssignDeviceID(pDeviceInit, &hardwareId);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfPdoInitAssignDeviceID failed: 0x%08X", status);
        goto Cleanup;
    }

    //
    // Set instance ID
    //
    status = RtlUnicodeStringPrintf(&instanceId, L"0");
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = WdfPdoInitAssignInstanceID(pDeviceInit, &instanceId);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfPdoInitAssignInstanceID failed: 0x%08X", status);
        goto Cleanup;
    }

    //
    // Add hardware IDs
    //
    status = WdfPdoInitAddHardwareID(pDeviceInit, &hardwareId);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfPdoInitAddHardwareID failed: 0x%08X", status);
        goto Cleanup;
    }

    //
    // Add compatible ID
    //
    status = RtlUnicodeStringPrintf(&compatibleId, L"%s", deviceInfo->CompatibleId);
    if (NT_SUCCESS(status)) {
        WdfPdoInitAddCompatibleID(pDeviceInit, &compatibleId);
    }

    //
    // Set device description
    //
    status = RtlUnicodeStringPrintf(&deviceDesc, L"%s", deviceInfo->DeviceDesc);
    if (NT_SUCCESS(status)) {
        status = WdfPdoInitAddDeviceText(
            pDeviceInit,
            &deviceDesc,
            &deviceDesc,
            0x0409  // English (US)
            );
    }

    WdfPdoInitSetDefaultLocale(pDeviceInit, 0x0409);

    //
    // Create the PDO
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, ZVIO_PDO_CONTEXT);

    status = WdfDeviceCreate(&pDeviceInit, &pdoAttributes, &hChild);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfDeviceCreate (PDO) failed: 0x%08X", status);
        goto Cleanup;
    }

    //
    // Initialize PDO context
    //
    pdoContext = ZvioGetPdoContext(hChild);
    pdoContext->DeviceType = DeviceContext->DeviceType;

    //
    // Add the PDO to the bus
    //
    status = WdfFdoAddStaticChild(DeviceContext->Device, hChild);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfFdoAddStaticChild failed: 0x%08X", status);
        WdfObjectDelete(hChild);
        return status;
    }

    ZvioDbgPrint("Created child PDO for %S", deviceInfo->DeviceDesc);
    return STATUS_SUCCESS;

Cleanup:
    if (pDeviceInit) {
        WdfDeviceInitFree(pDeviceInit);
    }
    return status;
}

/*
 * ZvioBusQueryInterface - Handle IRP_MN_QUERY_INTERFACE for child PDOs
 *
 * This allows child function drivers to access the parent VirtIO resources.
 */
NTSTATUS
ZvioBusQueryInterface(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ PZVIO_PDO_CONTEXT PdoContext,
    _In_ LPCGUID InterfaceType,
    _Out_ PINTERFACE Interface
    )
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(PdoContext);
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(Interface);

    //
    // TODO: Implement a custom interface that allows child drivers
    // (zvioblk, zvionet, etc.) to access virtqueues and device config.
    //
    // For now, child drivers will need to re-probe the PCI device
    // or we can expose this through a custom IOCTL.
    //

    return STATUS_NOT_SUPPORTED;
}
