/*
 * Zixiao VirtIO Balloon Driver - Driver Entry
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDF driver entry and PnP callbacks for VirtIO memory balloon.
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, ZvioBlnEvtDeviceAdd)
#pragma alloc_text(PAGE, ZvioBlnEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, ZvioBlnEvtDeviceReleaseHardware)
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

    ZvioBlnDbgPrint("DriverEntry");

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = ZvioBlnEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, ZvioBlnEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfDriverCreate failed: 0x%X", status);
        return status;
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtDriverContextCleanup - Driver cleanup callback
 */
VOID
ZvioBlnEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    ZvioBlnDbgPrint("Driver cleanup");
}

/*
 * ZvioBlnEvtDeviceAdd - Device add callback
 */
NTSTATUS
ZvioBlnEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES timerAttributes;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    ZvioBlnDbgPrint("DeviceAdd");

    //
    // Set up PnP/Power callbacks
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = ZvioBlnEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = ZvioBlnEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = ZvioBlnEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = ZvioBlnEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // Create the device
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ZVIOBLN_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfDeviceCreate failed: 0x%X", status);
        return status;
    }

    deviceContext = ZvioBlnGetDeviceContext(device);
    RtlZeroMemory(deviceContext, sizeof(ZVIOBLN_DEVICE_CONTEXT));
    deviceContext->Device = device;
    InitializeListHead(&deviceContext->PageList);

    //
    // Create spin lock for page list
    //
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->PageListLock);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfSpinLockCreate failed: 0x%X", status);
        return status;
    }

    //
    // Create work timer for balloon operations
    //
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, ZvioBlnWorkTimerCallback, 1000);
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = device;

    status = WdfTimerCreate(&timerConfig, &timerAttributes, &deviceContext->WorkTimer);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfTimerCreate failed: 0x%X", status);
        return status;
    }

    //
    // Create DMA enabler
    //
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather64, PAGE_SIZE * ZVIOBLN_MAX_PAGES_PER_OP);

    status = WdfDmaEnablerCreate(device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->DmaEnabler);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfDmaEnablerCreate failed: 0x%X", status);
        return status;
    }

    ZvioBlnDbgPrint("Device created successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtDevicePrepareHardware - Prepare hardware callback
 */
NTSTATUS
ZvioBlnEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    ULONG resourceCount;
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG barIndex = 0;

    PAGED_CODE();

    ZvioBlnDbgPrint("PrepareHardware");

    deviceContext = ZvioBlnGetDeviceContext(Device);
    deviceContext->ResourcesRaw = ResourcesRaw;
    deviceContext->ResourcesTranslated = ResourcesTranslated;

    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

    for (i = 0; i < resourceCount; i++) {
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (!descriptor) {
            continue;
        }

        switch (descriptor->Type) {
        case CmResourceTypeMemory:
            if (barIndex < 6) {
                deviceContext->BarPA[barIndex] = descriptor->u.Memory.Start;
                deviceContext->BarLength[barIndex] = descriptor->u.Memory.Length;

                deviceContext->BarVA[barIndex] = MmMapIoSpace(
                    descriptor->u.Memory.Start,
                    descriptor->u.Memory.Length,
                    MmNonCached
                    );

                if (!deviceContext->BarVA[barIndex]) {
                    ZvioBlnDbgError("Failed to map BAR%u", barIndex);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                ZvioBlnDbgPrint("BAR%u: PA=0x%llX Length=0x%X VA=%p",
                    barIndex,
                    descriptor->u.Memory.Start.QuadPart,
                    descriptor->u.Memory.Length,
                    deviceContext->BarVA[barIndex]);

                barIndex++;
            }
            break;

        case CmResourceTypeInterrupt:
            ZvioBlnDbgPrint("Interrupt: Level=%u Vector=%u",
                descriptor->u.Interrupt.Level,
                descriptor->u.Interrupt.Vector);
            deviceContext->UseMsix = (descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
            break;

        default:
            break;
        }
    }

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
        ZvioBlnDbgError("Failed to get bus interface: 0x%X", status);
        return status;
    }

    //
    // Parse VirtIO PCI capabilities
    //
    status = ZvioBlnPciParseCapabilities(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to parse PCI capabilities: 0x%X", status);
        return status;
    }

    //
    // Create interrupt
    //
    WDF_INTERRUPT_CONFIG interruptConfig;
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, ZvioBlnEvtInterruptIsr, ZvioBlnEvtInterruptDpc);
    interruptConfig.EvtInterruptEnable = ZvioBlnEvtInterruptEnable;
    interruptConfig.EvtInterruptDisable = ZvioBlnEvtInterruptDisable;

    status = WdfInterruptCreate(Device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->Interrupt);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("WdfInterruptCreate failed: 0x%X", status);
        return status;
    }

    //
    // Initialize VirtIO device
    //
    status = ZvioBlnDeviceInit(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("ZvioBlnDeviceInit failed: 0x%X", status);
        return status;
    }

    //
    // Allocate PFN buffer for communication with host
    //
    status = WdfCommonBufferCreate(
        deviceContext->DmaEnabler,
        ZVIOBLN_MAX_PAGES_PER_OP * sizeof(ULONG),
        WDF_NO_OBJECT_ATTRIBUTES,
        &deviceContext->PfnBuffer
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to create PFN buffer: 0x%X", status);
        return status;
    }

    deviceContext->PfnArray = (PULONG)WdfCommonBufferGetAlignedVirtualAddress(deviceContext->PfnBuffer);
    deviceContext->PfnArrayPhys = WdfCommonBufferGetAlignedLogicalAddress(deviceContext->PfnBuffer);
    deviceContext->PfnArraySize = ZVIOBLN_MAX_PAGES_PER_OP;

    //
    // Allocate stats buffer if feature supported
    //
    if (deviceContext->DriverFeatures & VIRTIO_BALLOON_F_STATS_VQ) {
        status = WdfCommonBufferCreate(
            deviceContext->DmaEnabler,
            sizeof(VIRTIO_BALLOON_STAT) * VIRTIO_BALLOON_S_NR,
            WDF_NO_OBJECT_ATTRIBUTES,
            &deviceContext->StatsBuffer
            );

        if (NT_SUCCESS(status)) {
            deviceContext->Stats = (PVIRTIO_BALLOON_STAT)WdfCommonBufferGetAlignedVirtualAddress(deviceContext->StatsBuffer);
            deviceContext->StatsPhys = WdfCommonBufferGetAlignedLogicalAddress(deviceContext->StatsBuffer);
        }
    }

    ZvioBlnDbgPrint("Hardware prepared successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtDeviceReleaseHardware - Release hardware callback
 */
NTSTATUS
ZvioBlnEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    ULONG i;
    PLIST_ENTRY entry;
    PZVIOBLN_PAGE_ENTRY pageEntry;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    ZvioBlnDbgPrint("ReleaseHardware");

    deviceContext = ZvioBlnGetDeviceContext(Device);

    //
    // Reset device
    //
    ZvioBlnDeviceReset(deviceContext);

    //
    // Free all balloon pages
    //
    while (!IsListEmpty(&deviceContext->PageList)) {
        entry = RemoveHeadList(&deviceContext->PageList);
        pageEntry = CONTAINING_RECORD(entry, ZVIOBLN_PAGE_ENTRY, Link);

        if (pageEntry->Mdl) {
            MmFreePagesFromMdl(pageEntry->Mdl);
            ExFreePool(pageEntry->Mdl);
        }
        ExFreePoolWithTag(pageEntry, ZVIOBLN_TAG);
    }
    deviceContext->NumPages = 0;

    //
    // Destroy virtqueues
    //
    if (deviceContext->InflateQueue) {
        ZvioBlnQueueDestroy(deviceContext->InflateQueue);
        deviceContext->InflateQueue = NULL;
    }
    if (deviceContext->DeflateQueue) {
        ZvioBlnQueueDestroy(deviceContext->DeflateQueue);
        deviceContext->DeflateQueue = NULL;
    }
    if (deviceContext->StatsQueue) {
        ZvioBlnQueueDestroy(deviceContext->StatsQueue);
        deviceContext->StatsQueue = NULL;
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
    // Dereference bus interface
    //
    if (deviceContext->BusInterface.InterfaceDereference) {
        deviceContext->BusInterface.InterfaceDereference(deviceContext->BusInterface.Context);
    }

    ZvioBlnDbgPrint("Hardware released");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtDeviceD0Entry - D0 entry callback
 */
NTSTATUS
ZvioBlnEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PZVIOBLN_DEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(PreviousState);

    ZvioBlnDbgPrint("D0Entry");

    deviceContext = ZvioBlnGetDeviceContext(Device);

    //
    // Start work timer
    //
    WdfTimerStart(deviceContext->WorkTimer, WDF_REL_TIMEOUT_IN_MS(1000));

    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtDeviceD0Exit - D0 exit callback
 */
NTSTATUS
ZvioBlnEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PZVIOBLN_DEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(TargetState);

    ZvioBlnDbgPrint("D0Exit");

    deviceContext = ZvioBlnGetDeviceContext(Device);

    //
    // Stop work timer
    //
    WdfTimerStop(deviceContext->WorkTimer, TRUE);

    return STATUS_SUCCESS;
}
