/*
 * Zixiao VirtIO Block Driver - Driver Entry
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Main driver entry point and WDF device initialization.
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, ZvioBlkEvtDeviceAdd)
#pragma alloc_text(PAGE, ZvioBlkEvtDriverContextCleanup)
#pragma alloc_text(PAGE, ZvioBlkEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, ZvioBlkEvtDeviceReleaseHardware)
#endif

/*
 * DriverEntry - Main driver entry point
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    ZvioBlkDbgPrint("DriverEntry: Zixiao VirtIO Block Driver v1.0");

    //
    // Initialize driver configuration
    //
    WDF_DRIVER_CONFIG_INIT(&config, ZvioBlkEvtDeviceAdd);

    //
    // Set cleanup callback
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = ZvioBlkEvtDriverContextCleanup;

    //
    // Create the WDF driver object
    //
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("WdfDriverCreate failed: 0x%08X", status);
        return status;
    }

    ZvioBlkDbgPrint("DriverEntry: Driver created successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtDriverContextCleanup - Driver cleanup callback
 */
VOID
ZvioBlkEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    ZvioBlkDbgPrint("Driver cleanup");
}

/*
 * ZvioBlkEvtDeviceAdd - Called when a new device is found
 */
NTSTATUS
ZvioBlkEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDFDEVICE device;
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDF_INTERRUPT_CONFIG interruptConfig;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    ZvioBlkDbgPrint("ZvioBlkEvtDeviceAdd");

    //
    // Configure PnP/Power callbacks
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = ZvioBlkEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = ZvioBlkEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = ZvioBlkEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = ZvioBlkEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // Mark as non-power pageable (we're a storage driver)
    //
    WdfDeviceInitSetPowerPageable(DeviceInit);

    //
    // Create the device object with context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, ZVIOBLK_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("WdfDeviceCreate failed: 0x%08X", status);
        return status;
    }

    //
    // Initialize device context
    //
    deviceContext = ZvioBlkGetDeviceContext(device);
    RtlZeroMemory(deviceContext, sizeof(ZVIOBLK_DEVICE_CONTEXT));
    deviceContext->Device = device;
    deviceContext->SectorSize = 512; // Default

    //
    // Create I/O queue for read/write/ioctl operations
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);
    ioQueueConfig.EvtIoRead = ZvioBlkEvtIoRead;
    ioQueueConfig.EvtIoWrite = ZvioBlkEvtIoWrite;
    ioQueueConfig.EvtIoDeviceControl = ZvioBlkEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->IoQueue);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("WdfIoQueueCreate failed: 0x%08X", status);
        return status;
    }

    //
    // Create DMA enabler for scatter-gather operations
    //
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather64, 0x100000);

    status = WdfDmaEnablerCreate(device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->DmaEnabler);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("WdfDmaEnablerCreate failed: 0x%08X", status);
        // Continue without DMA - will use bounce buffers
    }

    //
    // Create interrupt object
    //
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, ZvioBlkEvtInterruptIsr, ZvioBlkEvtInterruptDpc);
    interruptConfig.EvtInterruptEnable = ZvioBlkEvtInterruptEnable;
    interruptConfig.EvtInterruptDisable = ZvioBlkEvtInterruptDisable;

    status = WdfInterruptCreate(device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->Interrupt);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("WdfInterruptCreate failed: 0x%08X", status);
        // Will try again in PrepareHardware with proper resources
    }

    ZvioBlkDbgPrint("Device created successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtDevicePrepareHardware - Prepare hardware resources
 */
NTSTATUS
ZvioBlkEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG barIndex = 0;

    PAGED_CODE();

    deviceContext = ZvioBlkGetDeviceContext(Device);
    deviceContext->ResourcesRaw = ResourcesRaw;
    deviceContext->ResourcesTranslated = ResourcesTranslated;

    ZvioBlkDbgPrint("PrepareHardware: Mapping resources");

    //
    // Get PCI bus interface for config space access
    //
    status = WdfFdoQueryForInterface(
        Device,
        &GUID_BUS_INTERFACE_STANDARD,
        (PINTERFACE)&deviceContext->BusInterface,
        sizeof(BUS_INTERFACE_STANDARD),
        1,
        NULL
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Failed to get PCI bus interface: 0x%08X", status);
        return status;
    }

    //
    // Enumerate and map resources
    //
    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

        switch (descriptor->Type) {
        case CmResourceTypeMemory:
            if (barIndex < 6) {
                deviceContext->BarPA[barIndex] = descriptor->u.Memory.Start;
                deviceContext->BarLength[barIndex] = descriptor->u.Memory.Length;

                deviceContext->BarVA[barIndex] = MmMapIoSpaceEx(
                    descriptor->u.Memory.Start,
                    descriptor->u.Memory.Length,
                    PAGE_READWRITE | PAGE_NOCACHE
                    );

                if (deviceContext->BarVA[barIndex] == NULL) {
                    ZvioBlkDbgError("Failed to map BAR%d", barIndex);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                ZvioBlkDbgPrint("BAR%d: PA=0x%llX Len=0x%X VA=0x%p",
                    barIndex,
                    descriptor->u.Memory.Start.QuadPart,
                    descriptor->u.Memory.Length,
                    deviceContext->BarVA[barIndex]);

                barIndex++;
            }
            break;

        case CmResourceTypeInterrupt:
            if (descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
                deviceContext->UseMsix = TRUE;
            }
            break;

        default:
            break;
        }
    }

    //
    // Parse VirtIO PCI capabilities
    //
    status = ZvioBlkPciParseCapabilities(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Failed to parse VirtIO capabilities: 0x%08X", status);
        return status;
    }

    //
    // Initialize the VirtIO device
    //
    status = ZvioBlkDeviceInit(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Failed to initialize VirtIO device: 0x%08X", status);
        return status;
    }

    //
    // Read device configuration
    //
    if (deviceContext->DeviceCfg) {
        deviceContext->Capacity = READ_REGISTER_ULONG64((PULONG64)&deviceContext->DeviceCfg->Capacity);

        if (deviceContext->DriverFeatures & VIRTIO_BLK_F_BLK_SIZE) {
            deviceContext->SectorSize = READ_REGISTER_ULONG(&deviceContext->DeviceCfg->BlkSize);
        }

        if (deviceContext->DriverFeatures & VIRTIO_BLK_F_SIZE_MAX) {
            deviceContext->MaxSegmentSize = READ_REGISTER_ULONG(&deviceContext->DeviceCfg->SizeMax);
        } else {
            deviceContext->MaxSegmentSize = 0x10000; // 64KB default
        }

        if (deviceContext->DriverFeatures & VIRTIO_BLK_F_SEG_MAX) {
            deviceContext->MaxSegments = READ_REGISTER_ULONG(&deviceContext->DeviceCfg->SegMax);
        } else {
            deviceContext->MaxSegments = 128; // Default
        }

        deviceContext->ReadOnly = (deviceContext->DeviceFeatures & VIRTIO_BLK_F_RO) != 0;
        deviceContext->SupportsFlush = (deviceContext->DriverFeatures & VIRTIO_BLK_F_FLUSH) != 0;
        deviceContext->SupportsDiscard = (deviceContext->DriverFeatures & VIRTIO_BLK_F_DISCARD) != 0;

        ZvioBlkDbgPrint("Capacity: %llu sectors (%llu MB)",
            deviceContext->Capacity,
            (deviceContext->Capacity * deviceContext->SectorSize) / (1024 * 1024));
        ZvioBlkDbgPrint("Sector size: %d, Max segments: %d, Max segment size: %d",
            deviceContext->SectorSize, deviceContext->MaxSegments, deviceContext->MaxSegmentSize);
        ZvioBlkDbgPrint("ReadOnly: %d, Flush: %d, Discard: %d",
            deviceContext->ReadOnly, deviceContext->SupportsFlush, deviceContext->SupportsDiscard);
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtDeviceReleaseHardware - Release hardware resources
 */
NTSTATUS
ZvioBlkEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    ULONG i;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    deviceContext = ZvioBlkGetDeviceContext(Device);

    ZvioBlkDbgPrint("ReleaseHardware");

    //
    // Reset the device
    //
    ZvioBlkDeviceReset(deviceContext);

    //
    // Destroy all virtqueues
    //
    if (deviceContext->Queues) {
        for (i = 0; i < deviceContext->NumQueues; i++) {
            if (deviceContext->Queues[i]) {
                ZvioBlkQueueDestroy(deviceContext->Queues[i]);
                deviceContext->Queues[i] = NULL;
            }
        }
        ExFreePoolWithTag(deviceContext->Queues, ZVIOBLK_TAG);
        deviceContext->Queues = NULL;
    }

    //
    // Unmap BARs
    //
    for (i = 0; i < 6; i++) {
        if (deviceContext->BarVA[i]) {
            MmUnmapIoSpace(deviceContext->BarVA[i], deviceContext->BarLength[i]);
            deviceContext->BarVA[i] = NULL;
        }
    }

    //
    // Release bus interface
    //
    if (deviceContext->BusInterface.InterfaceDereference) {
        deviceContext->BusInterface.InterfaceDereference(deviceContext->BusInterface.Context);
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtDeviceD0Entry - Device entering D0 state
 */
NTSTATUS
ZvioBlkEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    UCHAR status;

    UNREFERENCED_PARAMETER(PreviousState);

    deviceContext = ZvioBlkGetDeviceContext(Device);

    ZvioBlkDbgPrint("D0Entry from state %d", PreviousState);

    if (PreviousState != WdfPowerDeviceD0) {
        status = ZvioBlkGetDeviceStatus(deviceContext);
        if (!(status & VIRTIO_STATUS_DRIVER_OK)) {
            ZvioBlkSetDeviceStatus(deviceContext, status | VIRTIO_STATUS_DRIVER_OK);
        }
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtDeviceD0Exit - Device leaving D0 state
 */
NTSTATUS
ZvioBlkEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    UNREFERENCED_PARAMETER(Device);

    ZvioBlkDbgPrint("D0Exit to state %d", TargetState);

    return STATUS_SUCCESS;
}
