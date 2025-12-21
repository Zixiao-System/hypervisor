/*
 * Zixiao VirtIO Network Driver - Receive Path
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

#define RX_BUFFER_COUNT     64

/*
 * ZvioNetInitRxBuffers - Initialize receive buffers
 */
NTSTATUS
ZvioNetInitRxBuffers(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    NET_BUFFER_LIST_POOL_PARAMETERS nblPoolParams;
    NET_BUFFER_POOL_PARAMETERS nbPoolParams;
    ULONG i;

    //
    // Create NBL pool
    //
    NdisZeroMemory(&nblPoolParams, sizeof(nblPoolParams));
    nblPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    nblPoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    nblPoolParams.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    nblPoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    nblPoolParams.ContextSize = sizeof(PZVIONET_RX_BUFFER);
    nblPoolParams.fAllocateNetBuffer = TRUE;
    nblPoolParams.PoolTag = ZVIONET_TAG;

    Adapter->NblPool = NdisAllocateNetBufferListPool(Adapter->AdapterHandle, &nblPoolParams);
    if (!Adapter->NblPool) {
        ZvioNetDbgError("Failed to allocate NBL pool");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Allocate RX buffers and post to queue
    //
    for (i = 0; i < RX_BUFFER_COUNT; i++) {
        PZVIONET_RX_BUFFER rxBuf;
        PHYSICAL_ADDRESS bufferPA;

        rxBuf = (PZVIONET_RX_BUFFER)NdisAllocateMemoryWithTagPriority(
            Adapter->AdapterHandle,
            sizeof(ZVIONET_RX_BUFFER),
            ZVIONET_TAG,
            NormalPoolPriority
            );

        if (!rxBuf) {
            continue;
        }

        NdisZeroMemory(rxBuf, sizeof(ZVIONET_RX_BUFFER));
        rxBuf->Adapter = Adapter;
        rxBuf->BufferSize = ZVIONET_RX_BUFFER_SIZE;
        rxBuf->PoolHandle = Adapter->NblPool;

        //
        // Allocate shared memory for receive buffer
        //
        NdisMAllocateSharedMemory(
            Adapter->AdapterHandle,
            ZVIONET_RX_BUFFER_SIZE,
            FALSE,
            &rxBuf->Buffer,
            &rxBuf->BufferPA
            );

        if (!rxBuf->Buffer) {
            NdisFreeMemory(rxBuf, sizeof(ZVIONET_RX_BUFFER), 0);
            continue;
        }

        //
        // Add to free list
        //
        NdisAcquireSpinLock(&Adapter->RxFreeLock);
        InsertTailList(&Adapter->RxFreeList, &rxBuf->Link);
        Adapter->RxBufferCount++;
        NdisReleaseSpinLock(&Adapter->RxFreeLock);
    }

    ZvioNetDbgPrint("Allocated %d RX buffers", Adapter->RxBufferCount);

    //
    // Post buffers to RX queue
    //
    ZvioNetReplenishRx(Adapter);

    return STATUS_SUCCESS;
}

/*
 * ZvioNetFreeRxBuffers - Free receive buffers
 */
VOID
ZvioNetFreeRxBuffers(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    PLIST_ENTRY entry;
    PZVIONET_RX_BUFFER rxBuf;

    NdisAcquireSpinLock(&Adapter->RxFreeLock);

    while (!IsListEmpty(&Adapter->RxFreeList)) {
        entry = RemoveHeadList(&Adapter->RxFreeList);
        rxBuf = CONTAINING_RECORD(entry, ZVIONET_RX_BUFFER, Link);

        if (rxBuf->Buffer) {
            NdisMFreeSharedMemory(
                Adapter->AdapterHandle,
                ZVIONET_RX_BUFFER_SIZE,
                FALSE,
                rxBuf->Buffer,
                rxBuf->BufferPA
                );
        }

        if (rxBuf->Nbl) {
            NdisFreeNetBufferList(rxBuf->Nbl);
        }

        NdisFreeMemory(rxBuf, sizeof(ZVIONET_RX_BUFFER), 0);
    }

    NdisReleaseSpinLock(&Adapter->RxFreeLock);

    if (Adapter->NblPool) {
        NdisFreeNetBufferListPool(Adapter->NblPool);
        Adapter->NblPool = NULL;
    }
}

/*
 * ZvioNetReplenishRx - Post buffers to RX queue
 */
VOID
ZvioNetReplenishRx(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    PZVIONET_VIRTQUEUE rxQueue = Adapter->RxQueue;
    PLIST_ENTRY entry;
    PZVIONET_RX_BUFFER rxBuf;

    if (!rxQueue) {
        return;
    }

    NdisAcquireSpinLock(&Adapter->RxFreeLock);

    while (!IsListEmpty(&Adapter->RxFreeList)) {
        entry = Adapter->RxFreeList.Flink;
        rxBuf = CONTAINING_RECORD(entry, ZVIONET_RX_BUFFER, Link);

        //
        // Try to add buffer to queue
        //
        if (ZvioNetQueueAddBuffer(rxQueue, rxBuf->BufferPA, rxBuf->BufferSize, TRUE, rxBuf) != STATUS_SUCCESS) {
            break;
        }

        RemoveHeadList(&Adapter->RxFreeList);
    }

    NdisReleaseSpinLock(&Adapter->RxFreeLock);

    ZvioNetQueueKick(rxQueue);
}

/*
 * ZvioNetProcessRx - Process received packets
 */
VOID
ZvioNetProcessRx(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    PZVIONET_VIRTQUEUE rxQueue = Adapter->RxQueue;
    PZVIONET_RX_BUFFER rxBuf;
    PNET_BUFFER_LIST nblChain = NULL;
    PNET_BUFFER_LIST *nextNbl = &nblChain;
    ULONG length;
    ULONG packetCount = 0;

    if (!rxQueue || !Adapter->Running) {
        return;
    }

    //
    // Process received buffers
    //
    while ((rxBuf = (PZVIONET_RX_BUFFER)ZvioNetQueueGetBuffer(rxQueue, &length)) != NULL) {
        PNET_BUFFER_LIST nbl;
        PNET_BUFFER nb;
        PMDL mdl;

        if (length == 0 || length > rxBuf->BufferSize) {
            //
            // Invalid packet, return buffer to free list
            //
            NdisAcquireSpinLock(&Adapter->RxFreeLock);
            InsertTailList(&Adapter->RxFreeList, &rxBuf->Link);
            NdisReleaseSpinLock(&Adapter->RxFreeLock);
            Adapter->RxErrors++;
            continue;
        }

        //
        // Create MDL for the received data
        //
        mdl = NdisAllocateMdl(Adapter->AdapterHandle, rxBuf->Buffer, length);
        if (!mdl) {
            NdisAcquireSpinLock(&Adapter->RxFreeLock);
            InsertTailList(&Adapter->RxFreeList, &rxBuf->Link);
            NdisReleaseSpinLock(&Adapter->RxFreeLock);
            continue;
        }

        //
        // Allocate NBL
        //
        nbl = NdisAllocateNetBufferAndNetBufferList(
            Adapter->NblPool,
            0,
            0,
            mdl,
            0,
            length
            );

        if (!nbl) {
            NdisFreeMdl(mdl);
            NdisAcquireSpinLock(&Adapter->RxFreeLock);
            InsertTailList(&Adapter->RxFreeList, &rxBuf->Link);
            NdisReleaseSpinLock(&Adapter->RxFreeLock);
            continue;
        }

        rxBuf->Nbl = nbl;
        rxBuf->Mdl = mdl;

        //
        // Store context for return
        //
        NET_BUFFER_LIST_MINIPORT_RESERVED(nbl)[0] = rxBuf;

        //
        // Add to chain
        //
        *nextNbl = nbl;
        nextNbl = &NET_BUFFER_LIST_NEXT_NBL(nbl);

        Adapter->RxPackets++;
        Adapter->RxBytes += length;
        packetCount++;
    }

    //
    // Indicate received packets
    //
    if (nblChain) {
        NdisMIndicateReceiveNetBufferLists(
            Adapter->AdapterHandle,
            nblChain,
            NDIS_DEFAULT_PORT_NUMBER,
            packetCount,
            NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL
            );
    }

    //
    // Replenish RX queue
    //
    ZvioNetReplenishRx(Adapter);
}
