/*
 * Zixiao VirtIO Network Driver - Transmit Path
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

/*
 * ZvioNetSendNetBufferLists - Send network buffer lists
 */
VOID
ZvioNetSendNetBufferLists(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists
    )
{
    PNET_BUFFER_LIST nbl;
    PNET_BUFFER nb;
    PZVIONET_VIRTQUEUE txQueue = Adapter->TxQueue;
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    if (!txQueue) {
        for (nbl = NetBufferLists; nbl; nbl = NET_BUFFER_LIST_NEXT_NBL(nbl)) {
            NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_ADAPTER_NOT_READY;
        }
        NdisMSendNetBufferListsComplete(Adapter->AdapterHandle, NetBufferLists, 0);
        return;
    }

    //
    // Process each NBL
    //
    for (nbl = NetBufferLists; nbl != NULL; ) {
        PNET_BUFFER_LIST nextNbl = NET_BUFFER_LIST_NEXT_NBL(nbl);
        NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;

        status = NDIS_STATUS_SUCCESS;

        for (nb = NET_BUFFER_LIST_FIRST_NB(nbl); nb; nb = NET_BUFFER_NEXT_NB(nb)) {
            PMDL mdl = NET_BUFFER_CURRENT_MDL(nb);
            ULONG dataLength = NET_BUFFER_DATA_LENGTH(nb);
            ULONG offset = NET_BUFFER_CURRENT_MDL_OFFSET(nb);
            PVOID virtualAddr;
            PHYSICAL_ADDRESS physAddr;

            if (!mdl || dataLength == 0) {
                status = NDIS_STATUS_INVALID_DATA;
                break;
            }

            //
            // Get the virtual address of the data
            //
            virtualAddr = (PUCHAR)MmGetMdlVirtualAddress(mdl) + offset;
            physAddr = MmGetPhysicalAddress(virtualAddr);

            //
            // For simplicity, we send the packet directly without VirtIO header
            // In a real driver, we would prepend VIRTIO_NET_HDR
            //
            NdisAcquireSpinLock(&txQueue->Lock);

            if (txQueue->NumFree < 2) {
                NdisReleaseSpinLock(&txQueue->Lock);
                status = NDIS_STATUS_RESOURCES;
                break;
            }

            //
            // Add buffer to TX queue
            //
            USHORT descIdx = txQueue->FreeHead;
            txQueue->FreeHead = txQueue->Desc[descIdx].Next;
            txQueue->NumFree--;

            txQueue->Desc[descIdx].Addr = physAddr.QuadPart;
            txQueue->Desc[descIdx].Len = dataLength;
            txQueue->Desc[descIdx].Flags = 0;
            txQueue->Desc[descIdx].Next = 0xFFFF;

            txQueue->DescData[descIdx] = nbl;

            USHORT availIdx = txQueue->Avail->Idx & (txQueue->Size - 1);
            txQueue->Avail->Ring[availIdx] = descIdx;
            KeMemoryBarrier();
            txQueue->Avail->Idx++;

            NdisReleaseSpinLock(&txQueue->Lock);

            Adapter->TxPackets++;
            Adapter->TxBytes += dataLength;
        }

        if (status != NDIS_STATUS_SUCCESS) {
            NET_BUFFER_LIST_STATUS(nbl) = status;
            NdisMSendNetBufferListsComplete(Adapter->AdapterHandle, nbl, 0);
        } else {
            //
            // Kick the TX queue
            //
            ZvioNetQueueKick(txQueue);
        }

        nbl = nextNbl;
    }
}

/*
 * ZvioNetCompleteTx - Complete transmitted packets
 */
VOID
ZvioNetCompleteTx(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    PZVIONET_VIRTQUEUE txQueue = Adapter->TxQueue;
    PNET_BUFFER_LIST nbl;
    PNET_BUFFER_LIST completeList = NULL;
    PNET_BUFFER_LIST *nextPtr = &completeList;
    ULONG length;

    if (!txQueue) {
        return;
    }

    //
    // Process completed TX buffers
    //
    while ((nbl = (PNET_BUFFER_LIST)ZvioNetQueueGetBuffer(txQueue, &length)) != NULL) {
        NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_SUCCESS;
        *nextPtr = nbl;
        nextPtr = &NET_BUFFER_LIST_NEXT_NBL(nbl);
        *nextPtr = NULL;
    }

    //
    // Complete all NBLs
    //
    if (completeList) {
        NdisMSendNetBufferListsComplete(
            Adapter->AdapterHandle,
            completeList,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL
            );
    }
}
