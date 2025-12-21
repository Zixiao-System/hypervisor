/*
 * Zixiao VirtIO Balloon Driver - Interrupt Handling
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interrupt service routine and DPC for balloon device.
 */

#include "public.h"

/*
 * ZvioBlnEvtInterruptIsr - Interrupt service routine
 */
BOOLEAN
ZvioBlnEvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
    )
{
    WDFDEVICE device;
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    UCHAR isrStatus;

    UNREFERENCED_PARAMETER(MessageID);

    device = WdfInterruptGetDevice(Interrupt);
    deviceContext = ZvioBlnGetDeviceContext(device);

    //
    // For MSI-X, we know this is our interrupt
    //
    if (deviceContext->UseMsix) {
        WdfInterruptQueueDpcForIsr(Interrupt);
        return TRUE;
    }

    //
    // For legacy interrupts, check ISR status
    //
    if (!deviceContext->IsrStatus) {
        return FALSE;
    }

    isrStatus = READ_REGISTER_UCHAR(deviceContext->IsrStatus);

    if (isrStatus == 0) {
        return FALSE;
    }

    //
    // Queue DPC
    //
    WdfInterruptQueueDpcForIsr(Interrupt);
    return TRUE;
}

/*
 * ZvioBlnEvtInterruptDpc - Deferred procedure call for interrupt
 */
VOID
ZvioBlnEvtInterruptDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject
    )
{
    WDFDEVICE device;
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    ULONG length;
    PVOID userData;

    UNREFERENCED_PARAMETER(AssociatedObject);

    device = WdfInterruptGetDevice(Interrupt);
    deviceContext = ZvioBlnGetDeviceContext(device);

    ZvioBlnDbgPrint("Interrupt DPC");

    //
    // Process completed inflate buffers
    //
    if (deviceContext->InflateQueue) {
        while ((userData = ZvioBlnQueueGetBuffer(deviceContext->InflateQueue, &length)) != NULL) {
            //
            // Inflate completed - nothing to do with user data for PFN array
            //
            ZvioBlnDbgPrint("Inflate completed: %u bytes", length);
        }
    }

    //
    // Process completed deflate buffers
    //
    if (deviceContext->DeflateQueue) {
        while ((userData = ZvioBlnQueueGetBuffer(deviceContext->DeflateQueue, &length)) != NULL) {
            //
            // Deflate completed - nothing to do with user data for PFN array
            //
            ZvioBlnDbgPrint("Deflate completed: %u bytes", length);
        }
    }

    //
    // Process completed stats buffers
    //
    if (deviceContext->StatsQueue) {
        while ((userData = ZvioBlnQueueGetBuffer(deviceContext->StatsQueue, &length)) != NULL) {
            //
            // Stats sent - host may have requested new stats
            //
            ZvioBlnDbgPrint("Stats sent: %u bytes", length);
        }
    }

    //
    // Check for config change (new target size)
    //
    if (deviceContext->DeviceCfg) {
        ULONG targetPages = READ_REGISTER_ULONG(&deviceContext->DeviceCfg->NumPages);

        if (targetPages != deviceContext->TargetPages) {
            ZvioBlnDbgPrint("Config change: target pages %u -> %u",
                deviceContext->TargetPages, targetPages);
            deviceContext->TargetPages = targetPages;

            //
            // Timer will handle the actual inflate/deflate
            //
        }
    }
}

/*
 * ZvioBlnEvtInterruptEnable - Enable interrupt
 */
NTSTATUS
ZvioBlnEvtInterruptEnable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE Device
    )
{
    PZVIOBLN_DEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(Interrupt);

    ZvioBlnDbgPrint("InterruptEnable");

    deviceContext = ZvioBlnGetDeviceContext(Device);

    //
    // Enable interrupts on all queues by clearing NO_INTERRUPT flag
    //
    if (deviceContext->InflateQueue && deviceContext->InflateQueue->Avail) {
        deviceContext->InflateQueue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    }

    if (deviceContext->DeflateQueue && deviceContext->DeflateQueue->Avail) {
        deviceContext->DeflateQueue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    }

    if (deviceContext->StatsQueue && deviceContext->StatsQueue->Avail) {
        deviceContext->StatsQueue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    }

    KeMemoryBarrier();

    return STATUS_SUCCESS;
}

/*
 * ZvioBlnEvtInterruptDisable - Disable interrupt
 */
NTSTATUS
ZvioBlnEvtInterruptDisable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE Device
    )
{
    PZVIOBLN_DEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(Interrupt);

    ZvioBlnDbgPrint("InterruptDisable");

    deviceContext = ZvioBlnGetDeviceContext(Device);

    //
    // Disable interrupts on all queues by setting NO_INTERRUPT flag
    //
    if (deviceContext->InflateQueue && deviceContext->InflateQueue->Avail) {
        deviceContext->InflateQueue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
    }

    if (deviceContext->DeflateQueue && deviceContext->DeflateQueue->Avail) {
        deviceContext->DeflateQueue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
    }

    if (deviceContext->StatsQueue && deviceContext->StatsQueue->Avail) {
        deviceContext->StatsQueue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
    }

    KeMemoryBarrier();

    return STATUS_SUCCESS;
}
