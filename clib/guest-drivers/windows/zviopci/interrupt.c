/*
 * Zixiao VirtIO PCI Bus Driver - Interrupt Handling
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI-X and legacy interrupt handling for VirtIO devices.
 */

#include "public.h"

/*
 * ZvioEvtInterruptIsr - Interrupt Service Routine
 *
 * Called at DIRQL when an interrupt occurs.
 */
BOOLEAN
ZvioEvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    UCHAR isrStatus;
    BOOLEAN claimed = FALSE;

    deviceContext = ZvioGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    //
    // For MSI-X, we know which queue triggered the interrupt
    //
    if (deviceContext->UseMsix) {
        //
        // MessageID 0 is typically for configuration changes
        // MessageID 1+ are for virtqueues
        //
        if (MessageID == VIRTIO_MSI_CONFIG_VECTOR) {
            //
            // Configuration change interrupt
            //
            ZvioDbgPrint("Config change interrupt");
            claimed = TRUE;
        } else if (MessageID > 0 && MessageID <= deviceContext->NumQueues) {
            //
            // Virtqueue interrupt
            //
            claimed = TRUE;
        }
    } else {
        //
        // Legacy interrupt: check ISR status register
        //
        if (deviceContext->IsrStatus) {
            isrStatus = READ_REGISTER_UCHAR(deviceContext->IsrStatus);

            if (isrStatus != 0) {
                //
                // Bit 0: Queue interrupt
                // Bit 1: Configuration change
                //
                claimed = TRUE;

                if (isrStatus & 0x02) {
                    ZvioDbgPrint("Config change interrupt (legacy)");
                }
            }
        }
    }

    //
    // Queue DPC for processing
    //
    if (claimed) {
        WdfInterruptQueueDpcForIsr(Interrupt);
    }

    return claimed;
}

/*
 * ZvioEvtInterruptDpc - Deferred Procedure Call for interrupt processing
 *
 * Called at DISPATCH_LEVEL to process completed buffers.
 */
VOID
ZvioEvtInterruptDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    USHORT i;
    PZVIO_VIRTQUEUE queue;
    ULONG length;
    PVOID userData;

    UNREFERENCED_PARAMETER(AssociatedObject);

    deviceContext = ZvioGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    //
    // Process all queues for completed buffers
    //
    for (i = 0; i < deviceContext->NumQueues; i++) {
        queue = deviceContext->Queues[i];
        if (!queue) {
            continue;
        }

        //
        // Drain completed buffers
        //
        while ((userData = ZvioQueueGetBuffer(queue, &length)) != NULL) {
            //
            // In a real driver, we would notify the appropriate
            // higher-level driver about the completion.
            // For the bus driver, we just log it.
            //
            ZvioDbgPrint("Queue %d: completed buffer, len=%d, data=0x%p",
                i, length, userData);
        }
    }
}

/*
 * ZvioEvtInterruptEnable - Enable interrupts
 */
NTSTATUS
ZvioEvtInterruptEnable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    USHORT i;

    UNREFERENCED_PARAMETER(Interrupt);

    deviceContext = ZvioGetDeviceContext(AssociatedDevice);

    ZvioDbgPrint("Enabling interrupts");

    //
    // Enable interrupts on all queues
    //
    for (i = 0; i < deviceContext->NumQueues; i++) {
        if (deviceContext->Queues[i]) {
            ZvioQueueEnableInterrupts(deviceContext->Queues[i], TRUE);
        }
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioEvtInterruptDisable - Disable interrupts
 */
NTSTATUS
ZvioEvtInterruptDisable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
    )
{
    PZVIO_DEVICE_CONTEXT deviceContext;
    USHORT i;

    UNREFERENCED_PARAMETER(Interrupt);

    deviceContext = ZvioGetDeviceContext(AssociatedDevice);

    ZvioDbgPrint("Disabling interrupts");

    //
    // Disable interrupts on all queues
    //
    for (i = 0; i < deviceContext->NumQueues; i++) {
        if (deviceContext->Queues[i]) {
            ZvioQueueEnableInterrupts(deviceContext->Queues[i], FALSE);
        }
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioCreateInterrupts - Create interrupt objects for the device
 *
 * Called during device initialization to set up MSI-X or legacy interrupts.
 */
NTSTATUS
ZvioCreateInterrupts(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG interruptCount = 0;
    WDF_INTERRUPT_CONFIG interruptConfig;
    WDFINTERRUPT interrupt;

    //
    // Count interrupts
    //
    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (descriptor->Type == CmResourceTypeInterrupt) {
            interruptCount++;
        }
    }

    if (interruptCount == 0) {
        ZvioDbgError("No interrupts found");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ZvioDbgPrint("Creating %d interrupt(s)", interruptCount);

    //
    // Allocate interrupt array
    //
    DeviceContext->Interrupts = (WDFINTERRUPT*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        interruptCount * sizeof(WDFINTERRUPT),
        ZVIO_POOL_TAG
        );

    if (!DeviceContext->Interrupts) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DeviceContext->Interrupts, interruptCount * sizeof(WDFINTERRUPT));

    //
    // Check if MSI-X is available
    //
    DeviceContext->UseMsix = FALSE;
    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (descriptor->Type == CmResourceTypeInterrupt) {
            if (descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
                DeviceContext->UseMsix = TRUE;
                break;
            }
        }
    }

    ZvioDbgPrint("Using %s interrupts", DeviceContext->UseMsix ? "MSI-X" : "legacy");

    //
    // Create interrupt objects
    //
    ULONG interruptIndex = 0;
    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++) {
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (descriptor->Type != CmResourceTypeInterrupt) {
            continue;
        }

        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, ZvioEvtInterruptIsr, ZvioEvtInterruptDpc);
        interruptConfig.EvtInterruptEnable = ZvioEvtInterruptEnable;
        interruptConfig.EvtInterruptDisable = ZvioEvtInterruptDisable;
        interruptConfig.InterruptTranslated = descriptor;
        interruptConfig.InterruptRaw = WdfCmResourceListGetDescriptor(
            DeviceContext->ResourcesRaw, i);

        status = WdfInterruptCreate(
            DeviceContext->Device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &interrupt
            );

        if (!NT_SUCCESS(status)) {
            ZvioDbgError("Failed to create interrupt %d: 0x%08X", interruptIndex, status);
            return status;
        }

        DeviceContext->Interrupts[interruptIndex] = interrupt;
        interruptIndex++;
    }

    DeviceContext->NumInterrupts = (USHORT)interruptIndex;

    //
    // Configure MSI-X vectors for virtqueues
    //
    if (DeviceContext->UseMsix && DeviceContext->CommonCfg) {
        //
        // Set config vector
        //
        WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->MsixConfig, VIRTIO_MSI_CONFIG_VECTOR);

        //
        // Set queue vectors (one per queue, up to available vectors)
        //
        for (i = 0; i < DeviceContext->NumQueues; i++) {
            WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSel, (USHORT)i);
            KeMemoryBarrier();

            if (i + 1 < DeviceContext->NumInterrupts) {
                WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueMsixVector, (USHORT)(i + 1));
            } else {
                // Share the last vector for remaining queues
                WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueMsixVector,
                    DeviceContext->NumInterrupts - 1);
            }
        }
    }

    ZvioDbgPrint("Interrupts configured successfully");
    return STATUS_SUCCESS;
}
