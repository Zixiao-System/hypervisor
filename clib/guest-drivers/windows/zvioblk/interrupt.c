/*
 * Zixiao VirtIO Block Driver - Interrupt Handling
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI-X and legacy interrupt handling.
 */

#include "public.h"

/*
 * ZvioBlkEvtInterruptIsr - Interrupt Service Routine
 */
BOOLEAN
ZvioBlkEvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    UCHAR isrStatus;
    BOOLEAN claimed = FALSE;

    deviceContext = ZvioBlkGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    //
    // For MSI-X, we know which queue triggered the interrupt
    //
    if (deviceContext->UseMsix) {
        if (MessageID == 0) {
            // Configuration change interrupt
            ZvioBlkDbgPrint("Config change interrupt");
            claimed = TRUE;
        } else if (MessageID == 1) {
            // Virtqueue interrupt (request queue)
            claimed = TRUE;
        }
    } else {
        //
        // Legacy interrupt: check ISR status register
        //
        if (deviceContext->IsrStatus) {
            isrStatus = READ_REGISTER_UCHAR(deviceContext->IsrStatus);

            if (isrStatus != 0) {
                claimed = TRUE;

                if (isrStatus & 0x02) {
                    ZvioBlkDbgPrint("Config change interrupt (legacy)");
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
 * ZvioBlkEvtInterruptDpc - Deferred Procedure Call for interrupt processing
 */
VOID
ZvioBlkEvtInterruptDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    PZVIOBLK_VIRTQUEUE queue;
    PZVIOBLK_REQUEST blkRequest;
    ULONG length;

    UNREFERENCED_PARAMETER(AssociatedObject);

    deviceContext = ZvioBlkGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    //
    // Process the request queue (queue 0)
    //
    if (deviceContext->NumQueues > 0 && deviceContext->Queues && deviceContext->Queues[0]) {
        queue = deviceContext->Queues[0];

        //
        // Process all completed requests
        //
        while ((blkRequest = (PZVIOBLK_REQUEST)ZvioBlkQueueGetBuffer(queue, &length)) != NULL) {
            UCHAR status = *blkRequest->Status;
            ULONG bytesTransferred = 0;

            //
            // Calculate bytes transferred for read operations
            //
            if (blkRequest->Header->Type == VIRTIO_BLK_T_IN && status == VIRTIO_BLK_S_OK) {
                bytesTransferred = blkRequest->DataLength;
            } else if (blkRequest->Header->Type == VIRTIO_BLK_T_OUT && status == VIRTIO_BLK_S_OK) {
                bytesTransferred = blkRequest->DataLength;
            }

            ZvioBlkDbgPrint("Completed: type=%d status=%d bytes=%d",
                blkRequest->Header->Type, status, bytesTransferred);

            ZvioBlkCompleteRequest(blkRequest, status, bytesTransferred);
        }
    }
}

/*
 * ZvioBlkEvtInterruptEnable - Enable interrupts
 */
NTSTATUS
ZvioBlkEvtInterruptEnable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    USHORT i;

    UNREFERENCED_PARAMETER(Interrupt);

    deviceContext = ZvioBlkGetDeviceContext(AssociatedDevice);

    ZvioBlkDbgPrint("Enabling interrupts");

    //
    // Enable interrupts on all queues
    //
    if (deviceContext->Queues) {
        for (i = 0; i < deviceContext->NumQueues; i++) {
            if (deviceContext->Queues[i]) {
                WdfSpinLockAcquire(deviceContext->Queues[i]->Lock);
                deviceContext->Queues[i]->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
                KeMemoryBarrier();
                WdfSpinLockRelease(deviceContext->Queues[i]->Lock);
            }
        }
    }

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkEvtInterruptDisable - Disable interrupts
 */
NTSTATUS
ZvioBlkEvtInterruptDisable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
    )
{
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    USHORT i;

    UNREFERENCED_PARAMETER(Interrupt);

    deviceContext = ZvioBlkGetDeviceContext(AssociatedDevice);

    ZvioBlkDbgPrint("Disabling interrupts");

    //
    // Disable interrupts on all queues
    //
    if (deviceContext->Queues) {
        for (i = 0; i < deviceContext->NumQueues; i++) {
            if (deviceContext->Queues[i]) {
                WdfSpinLockAcquire(deviceContext->Queues[i]->Lock);
                deviceContext->Queues[i]->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
                KeMemoryBarrier();
                WdfSpinLockRelease(deviceContext->Queues[i]->Lock);
            }
        }
    }

    return STATUS_SUCCESS;
}
