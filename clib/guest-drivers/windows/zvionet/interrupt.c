/*
 * Zixiao VirtIO Network Driver - Interrupt Handling
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

/*
 * ZvioNetInterruptHandler - Interrupt Service Routine
 */
BOOLEAN
ZvioNetInterruptHandler(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportInterruptContext;
    UCHAR isrStatus;
    BOOLEAN claimed = FALSE;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;

    if (adapter->UseMsix) {
        // MSI-X always claims the interrupt
        claimed = TRUE;
    } else if (adapter->IsrStatus) {
        isrStatus = READ_REGISTER_UCHAR(adapter->IsrStatus);
        if (isrStatus != 0) {
            claimed = TRUE;
        }
    }

    if (claimed) {
        *QueueDefaultInterruptDpc = TRUE;
    }

    return claimed;
}

/*
 * ZvioNetInterruptDpc - Deferred Procedure Call
 */
VOID
ZvioNetInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    if (!adapter->Running) {
        return;
    }

    //
    // Process TX completions
    //
    ZvioNetCompleteTx(adapter);

    //
    // Process RX packets
    //
    ZvioNetProcessRx(adapter);
}

/*
 * ZvioNetEnableInterrupt - Enable interrupts
 */
VOID
ZvioNetEnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportInterruptContext;

    ZvioNetDbgPrint("EnableInterrupt");

    if (adapter->RxQueue) {
        NdisAcquireSpinLock(&adapter->RxQueue->Lock);
        adapter->RxQueue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
        KeMemoryBarrier();
        NdisReleaseSpinLock(&adapter->RxQueue->Lock);
    }

    if (adapter->TxQueue) {
        NdisAcquireSpinLock(&adapter->TxQueue->Lock);
        adapter->TxQueue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
        KeMemoryBarrier();
        NdisReleaseSpinLock(&adapter->TxQueue->Lock);
    }
}

/*
 * ZvioNetDisableInterrupt - Disable interrupts
 */
VOID
ZvioNetDisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportInterruptContext;

    ZvioNetDbgPrint("DisableInterrupt");

    if (adapter->RxQueue) {
        NdisAcquireSpinLock(&adapter->RxQueue->Lock);
        adapter->RxQueue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
        KeMemoryBarrier();
        NdisReleaseSpinLock(&adapter->RxQueue->Lock);
    }

    if (adapter->TxQueue) {
        NdisAcquireSpinLock(&adapter->TxQueue->Lock);
        adapter->TxQueue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
        KeMemoryBarrier();
        NdisReleaseSpinLock(&adapter->TxQueue->Lock);
    }
}
