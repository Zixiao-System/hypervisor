/*
 * Zixiao VirtIO PCI Bus Driver - Driver Entry
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Main driver entry point and WDF device initialization.
 */

#include <initguid.h>
#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, ZvioEvtDeviceAdd)
#pragma alloc_text(PAGE, ZvioEvtDriverContextCleanup)
#pragma alloc_text(PAGE, ZvioEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, ZvioEvtDeviceReleaseHardware)
#endif

/*
 * DriverEntry - Main driver entry point
 *
 * Called by the OS when the driver is loaded.
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

    ZvioDbgPrint("DriverEntry: Zixiao VirtIO PCI Bus Driver v1.0");

    //
    // Initialize driver configuration
    //
    WDF_DRIVER_CONFIG_INIT(&config, ZvioEvtDeviceAdd);

    //
    // Set cleanup callback
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = ZvioEvtDriverContextCleanup;

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
        ZvioDbgError("WdfDriverCreate failed: 0x%08X", status);
        return status;
    }

    ZvioDbgPrint("DriverEntry: Driver created successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioEvtDriverContextCleanup - Driver cleanup callback
 */
VOID
ZvioEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    ZvioDbgPrint("Driver cleanup");
}

/*
 * ZvioEvtDeviceAdd - Called when a new device is found
 *
 * Creates the WDF device object and sets up PnP/Power callbacks.
 */
NTSTATUS
ZvioEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDFDEVICE device;
    PZVIO_DEVICE_CONTEXT deviceContext;
    WDF_INTERRUPT_CONFIG interruptConfig;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    ZvioDbgPrint("ZvioEvtDeviceAdd");

    //
    // Set device type to bus driver (PDO generator)
    //
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);

    //
    // Configure PnP/Power callbacks
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = ZvioEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = ZvioEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = ZvioEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = ZvioEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // Create the device object with context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, ZVIO_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfDeviceCreate failed: 0x%08X", status);
        return status;
    }

    //
    // Initialize device context
    //
    deviceContext = ZvioGetDeviceContext(device);
    RtlZeroMemory(deviceContext, sizeof(ZVIO_DEVICE_CONTEXT));
    deviceContext->Device = device;
    deviceContext->DeviceType = VirtioDevTypeUnknown;

    //
    // Create a default interrupt object (will be configured in PrepareHardware)
    // This creates a single interrupt; MSI-X will configure multiple in PrepareHardware
    //
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, ZvioEvtInterruptIsr, ZvioEvtInterruptDpc);
    interruptConfig.EvtInterruptEnable = ZvioEvtInterruptEnable;
    interruptConfig.EvtInterruptDisable = ZvioEvtInterruptDisable;

    // We'll create interrupts in PrepareHardware after we know MSI-X vector count

    //
    // Create DMA enabler for 64-bit addressing
    //
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather64, 0x100000);

    status = WdfDmaEnablerCreate(device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->DmaEnabler);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("WdfDmaEnablerCreate failed: 0x%08X", status);
        // Continue without DMA - some features may not work
    }

    ZvioDbgPrint("Device created successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioEvtDevicePrepareHardware - Prepare hardware resources
 *
 * Maps BARs and initializes the VirtIO device.
 */
NTSTATUS
ZvioEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PZVIO_DEVICE_CONTEXT deviceContext;
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG barIndex = 0;
    ULONG interruptCount = 0;

    PAGED_CODE();

    deviceContext = ZvioGetDeviceContext(Device);
    deviceContext->ResourcesRaw = ResourcesRaw;
    deviceContext->ResourcesTranslated = ResourcesTranslated;

    ZvioDbgPrint("PrepareHardware: Mapping resources");

    //
    // Get PCI bus interface for config space access
    //
    status = WdfFdoQueryForInterface(
        Device,
        &GUID_BUS_INTERFACE_STANDARD,
        (PINTERFACE)&deviceContext->BusInterface,
        sizeof(BUS_INTERFACE_STANDARD),
        1,      // Version
        NULL    // InterfaceSpecificData
        );

    if (!NT_SUCCESS(status)) {
        ZvioDbgError("Failed to get PCI bus interface: 0x%08X", status);
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
                deviceContext->BarType[barIndex] = 0; // Memory

                //
                // Map the BAR to virtual address
                //
                deviceContext->BarVA[barIndex] = MmMapIoSpaceEx(
                    descriptor->u.Memory.Start,
                    descriptor->u.Memory.Length,
                    PAGE_READWRITE | PAGE_NOCACHE
                    );

                if (deviceContext->BarVA[barIndex] == NULL) {
                    ZvioDbgError("Failed to map BAR%d", barIndex);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                ZvioDbgPrint("BAR%d: PA=0x%llX Len=0x%X VA=0x%p",
                    barIndex,
                    descriptor->u.Memory.Start.QuadPart,
                    descriptor->u.Memory.Length,
                    deviceContext->BarVA[barIndex]);

                barIndex++;
            }
            break;

        case CmResourceTypePort:
            if (barIndex < 6) {
                deviceContext->BarPA[barIndex] = descriptor->u.Port.Start;
                deviceContext->BarLength[barIndex] = descriptor->u.Port.Length;
                deviceContext->BarType[barIndex] = 1; // I/O

                ZvioDbgPrint("BAR%d: I/O Port 0x%llX Len=0x%X",
                    barIndex,
                    descriptor->u.Port.Start.QuadPart,
                    descriptor->u.Port.Length);

                barIndex++;
            }
            break;

        case CmResourceTypeInterrupt:
            interruptCount++;
            break;

        default:
            break;
        }
    }

    ZvioDbgPrint("Mapped %d BARs, found %d interrupts", barIndex, interruptCount);

    //
    // Parse VirtIO PCI capabilities to find config structures
    //
    status = ZvioPciParseCapabilities(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("Failed to parse VirtIO capabilities: 0x%08X", status);
        return status;
    }

    //
    // Initialize the VirtIO device
    //
    status = ZvioDeviceInit(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("Failed to initialize VirtIO device: 0x%08X", status);
        return status;
    }

    //
    // Enumerate child devices (virtio-net, virtio-blk, etc.)
    //
    status = ZvioBusEnumerateChildren(deviceContext);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("Failed to enumerate children: 0x%08X", status);
        // Continue anyway - we may still work as a single device
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioEvtDeviceReleaseHardware - Release hardware resources
 */
NTSTATUS
ZvioEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    ULONG i;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    deviceContext = ZvioGetDeviceContext(Device);

    ZvioDbgPrint("ReleaseHardware");

    //
    // Reset the device
    //
    ZvioDeviceReset(deviceContext);

    //
    // Destroy all virtqueues
    //
    if (deviceContext->Queues) {
        for (i = 0; i < deviceContext->NumQueues; i++) {
            if (deviceContext->Queues[i]) {
                ZvioQueueDestroy(deviceContext->Queues[i]);
                deviceContext->Queues[i] = NULL;
            }
        }
        ExFreePoolWithTag(deviceContext->Queues, ZVIO_POOL_TAG);
        deviceContext->Queues = NULL;
    }

    //
    // Free interrupt objects
    //
    if (deviceContext->Interrupts) {
        ExFreePoolWithTag(deviceContext->Interrupts, ZVIO_POOL_TAG);
        deviceContext->Interrupts = NULL;
    }

    //
    // Unmap BARs
    //
    for (i = 0; i < 6; i++) {
        if (deviceContext->BarVA[i] && deviceContext->BarType[i] == 0) {
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
 * ZvioEvtDeviceD0Entry - Device entering D0 (working) state
 */
NTSTATUS
ZvioEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    UCHAR status;

    UNREFERENCED_PARAMETER(PreviousState);

    deviceContext = ZvioGetDeviceContext(Device);

    ZvioDbgPrint("D0Entry from state %d", PreviousState);

    //
    // If coming from a low power state, re-enable the device
    //
    if (PreviousState != WdfPowerDeviceD0) {
        status = ZvioGetDeviceStatus(deviceContext);
        if (!(status & VIRTIO_STATUS_DRIVER_OK)) {
            // Re-initialize driver_ok
            ZvioSetDeviceStatus(deviceContext,
                status | VIRTIO_STATUS_DRIVER_OK);
        }
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioEvtDeviceD0Exit - Device leaving D0 state
 */
NTSTATUS
ZvioEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    UNREFERENCED_PARAMETER(Device);

    ZvioDbgPrint("D0Exit to state %d", TargetState);

    // For now, just log. In the future, may need to quiesce queues

    return STATUS_SUCCESS;
}
