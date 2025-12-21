/*
 * Zixiao VirtIO Block Driver - Virtqueue Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtqueue creation, buffer management, and notification.
 */

#include "public.h"

/*
 * ZvioBlkQueueCreate - Create and initialize a virtqueue
 */
NTSTATUS
ZvioBlkQueueCreate(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIOBLK_VIRTQUEUE *Queue
    )
{
    NTSTATUS status;
    PZVIOBLK_VIRTQUEUE vq;
    USHORT queueSize;
    SIZE_T descSize, availSize, usedSize, totalSize;
    WDF_COMMON_BUFFER_CONFIG bufferConfig;

    *Queue = NULL;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    //
    // Select queue
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    //
    // Read queue size
    //
    queueSize = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSize);
    if (queueSize == 0) {
        ZvioBlkDbgPrint("Queue %d not available", Index);
        return STATUS_DEVICE_NOT_READY;
    }

    ZvioBlkDbgPrint("Creating queue %d with %d entries", Index, queueSize);

    //
    // Allocate queue structure
    //
    vq = (PZVIOBLK_VIRTQUEUE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(ZVIOBLK_VIRTQUEUE),
        ZVIOBLK_TAG
        );

    if (!vq) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(vq, sizeof(ZVIOBLK_VIRTQUEUE));
    vq->Index = Index;
    vq->Size = queueSize;
    vq->DeviceContext = DeviceContext;

    //
    // Create spinlock for queue
    //
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &vq->Lock);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(vq, ZVIOBLK_TAG);
        return status;
    }

    //
    // Calculate ring sizes
    //
    descSize = sizeof(VRING_DESC) * queueSize;
    availSize = sizeof(USHORT) * (3 + queueSize);
    usedSize = sizeof(USHORT) * 3 + sizeof(VRING_USED_ELEM) * queueSize;

    totalSize = ROUND_TO_PAGES(descSize) + ROUND_TO_PAGES(availSize) + ROUND_TO_PAGES(usedSize);

    //
    // Allocate DMA buffer for rings
    //
    WDF_COMMON_BUFFER_CONFIG_INIT(&bufferConfig, PAGE_SIZE);

    status = WdfCommonBufferCreate(
        DeviceContext->DmaEnabler,
        totalSize,
        &bufferConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &vq->RingBuffer
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Failed to allocate ring buffer: 0x%08X", status);
        WdfObjectDelete(vq->Lock);
        ExFreePoolWithTag(vq, ZVIOBLK_TAG);
        return status;
    }

    //
    // Get buffer addresses
    //
    PVOID ringVA = WdfCommonBufferGetAlignedVirtualAddress(vq->RingBuffer);
    PHYSICAL_ADDRESS ringPA = WdfCommonBufferGetAlignedLogicalAddress(vq->RingBuffer);

    RtlZeroMemory(ringVA, totalSize);

    //
    // Set up ring pointers
    //
    vq->Desc = (PVRING_DESC)ringVA;
    vq->DescPhys = ringPA;

    vq->Avail = (PVRING_AVAIL)((PUCHAR)ringVA + ROUND_TO_PAGES(descSize));
    vq->AvailPhys.QuadPart = ringPA.QuadPart + ROUND_TO_PAGES(descSize);

    vq->Used = (PVRING_USED)((PUCHAR)vq->Avail + ROUND_TO_PAGES(availSize));
    vq->UsedPhys.QuadPart = vq->AvailPhys.QuadPart + ROUND_TO_PAGES(availSize);

    //
    // Initialize descriptor chain
    //
    vq->NumFree = queueSize;
    vq->FreeHead = 0;
    for (USHORT i = 0; i < queueSize - 1; i++) {
        vq->Desc[i].Next = i + 1;
    }
    vq->Desc[queueSize - 1].Next = 0xFFFF;

    //
    // Allocate user data array
    //
    vq->DescData = (PVOID*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        queueSize * sizeof(PVOID),
        ZVIOBLK_TAG
        );

    if (!vq->DescData) {
        WdfObjectDelete(vq->RingBuffer);
        WdfObjectDelete(vq->Lock);
        ExFreePoolWithTag(vq, ZVIOBLK_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(vq->DescData, queueSize * sizeof(PVOID));

    //
    // Write queue addresses to device
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG64((PULONG64)&DeviceContext->CommonCfg->QueueDesc, vq->DescPhys.QuadPart);
    WRITE_REGISTER_ULONG64((PULONG64)&DeviceContext->CommonCfg->QueueDriver, vq->AvailPhys.QuadPart);
    WRITE_REGISTER_ULONG64((PULONG64)&DeviceContext->CommonCfg->QueueDevice, vq->UsedPhys.QuadPart);

    //
    // Calculate notify address
    //
    USHORT notifyOff = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueNotifyOff);
    vq->NotifyAddr = (PUCHAR)DeviceContext->NotifyBase +
                     notifyOff * DeviceContext->NotifyOffMultiplier;

    //
    // Enable the queue
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueEnable, 1);

    ZvioBlkDbgPrint("Queue %d created: desc=0x%llX avail=0x%llX used=0x%llX",
        Index, vq->DescPhys.QuadPart, vq->AvailPhys.QuadPart, vq->UsedPhys.QuadPart);

    *Queue = vq;
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkQueueDestroy - Destroy a virtqueue
 */
VOID
ZvioBlkQueueDestroy(
    _In_ PZVIOBLK_VIRTQUEUE Queue
    )
{
    if (!Queue) {
        return;
    }

    ZvioBlkDbgPrint("Destroying queue %d", Queue->Index);

    //
    // Disable the queue
    //
    if (Queue->DeviceContext && Queue->DeviceContext->CommonCfg) {
        WRITE_REGISTER_USHORT(&Queue->DeviceContext->CommonCfg->QueueSel, Queue->Index);
        KeMemoryBarrier();
        WRITE_REGISTER_USHORT(&Queue->DeviceContext->CommonCfg->QueueEnable, 0);
    }

    //
    // Free resources
    //
    if (Queue->DescData) {
        ExFreePoolWithTag(Queue->DescData, ZVIOBLK_TAG);
    }

    if (Queue->RingBuffer) {
        WdfObjectDelete(Queue->RingBuffer);
    }

    if (Queue->Lock) {
        WdfObjectDelete(Queue->Lock);
    }

    ExFreePoolWithTag(Queue, ZVIOBLK_TAG);
}

/*
 * ZvioBlkQueueAddBuffers - Add scatter-gather buffers to the virtqueue
 *
 * Creates a descriptor chain for a block request:
 * - OutCount descriptors are device-readable (header, data for writes)
 * - InCount descriptors are device-writable (data for reads, status)
 */
NTSTATUS
ZvioBlkQueueAddBuffers(
    _In_ PZVIOBLK_VIRTQUEUE Queue,
    _In_ PSCATTER_GATHER_LIST SgList,
    _In_ ULONG OutCount,
    _In_ ULONG InCount,
    _In_opt_ PVOID UserData,
    _Out_ PUSHORT HeadIdx
    )
{
    USHORT head;
    USHORT descIdx;
    USHORT prevIdx = 0xFFFF;
    ULONG totalDescs = OutCount + InCount;
    ULONG i;

    *HeadIdx = 0xFFFF;

    if (totalDescs == 0 || totalDescs > Queue->Size) {
        return STATUS_INVALID_PARAMETER;
    }

    WdfSpinLockAcquire(Queue->Lock);

    if (Queue->NumFree < totalDescs) {
        WdfSpinLockRelease(Queue->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Get the head of the descriptor chain
    //
    head = Queue->FreeHead;
    descIdx = head;

    //
    // Build descriptor chain from scatter-gather list
    //
    for (i = 0; i < SgList->NumberOfElements; i++) {
        if (Queue->FreeHead == 0xFFFF) {
            // Should not happen if we checked NumFree
            WdfSpinLockRelease(Queue->Lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        descIdx = Queue->FreeHead;
        Queue->FreeHead = Queue->Desc[descIdx].Next;
        Queue->NumFree--;

        Queue->Desc[descIdx].Addr = SgList->Elements[i].Address.QuadPart;
        Queue->Desc[descIdx].Len = SgList->Elements[i].Length;
        Queue->Desc[descIdx].Flags = 0;

        //
        // Set write flag for device-writable buffers (after OutCount)
        //
        if (i >= OutCount) {
            Queue->Desc[descIdx].Flags |= VRING_DESC_F_WRITE;
        }

        //
        // Link to previous descriptor
        //
        if (prevIdx != 0xFFFF) {
            Queue->Desc[prevIdx].Next = descIdx;
            Queue->Desc[prevIdx].Flags |= VRING_DESC_F_NEXT;
        }

        prevIdx = descIdx;
    }

    //
    // Terminate the chain
    //
    Queue->Desc[descIdx].Next = 0xFFFF;

    //
    // Store user data on the head descriptor
    //
    Queue->DescData[head] = UserData;

    //
    // Add to available ring
    //
    USHORT availIdx = Queue->Avail->Idx & (Queue->Size - 1);
    Queue->Avail->Ring[availIdx] = head;
    KeMemoryBarrier();
    Queue->Avail->Idx++;

    WdfSpinLockRelease(Queue->Lock);

    *HeadIdx = head;
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkQueueGetBuffer - Get a completed buffer from the virtqueue
 */
PVOID
ZvioBlkQueueGetBuffer(
    _In_ PZVIOBLK_VIRTQUEUE Queue,
    _Out_ PULONG Length
    )
{
    USHORT usedIdx;
    USHORT headIdx;
    USHORT descIdx;
    PVOID userData;

    *Length = 0;

    WdfSpinLockAcquire(Queue->Lock);

    //
    // Check if there are any used buffers
    //
    if (Queue->LastUsedIdx == Queue->Used->Idx) {
        WdfSpinLockRelease(Queue->Lock);
        return NULL;
    }

    //
    // Get the used element
    //
    usedIdx = Queue->LastUsedIdx & (Queue->Size - 1);
    headIdx = (USHORT)Queue->Used->Ring[usedIdx].Id;
    *Length = Queue->Used->Ring[usedIdx].Len;

    Queue->LastUsedIdx++;

    //
    // Get user data
    //
    userData = Queue->DescData[headIdx];
    Queue->DescData[headIdx] = NULL;

    //
    // Return all descriptors in the chain to the free list
    //
    descIdx = headIdx;
    while (descIdx != 0xFFFF) {
        USHORT nextIdx = (Queue->Desc[descIdx].Flags & VRING_DESC_F_NEXT) ?
            Queue->Desc[descIdx].Next : 0xFFFF;

        Queue->Desc[descIdx].Next = Queue->FreeHead;
        Queue->FreeHead = descIdx;
        Queue->NumFree++;

        descIdx = nextIdx;
    }

    WdfSpinLockRelease(Queue->Lock);

    return userData;
}

/*
 * ZvioBlkQueueKick - Notify the device of new buffers
 */
VOID
ZvioBlkQueueKick(
    _In_ PZVIOBLK_VIRTQUEUE Queue
    )
{
    KeMemoryBarrier();

    //
    // Check if notification is needed
    //
    if (Queue->Used->Flags & VRING_USED_F_NO_NOTIFY) {
        return;
    }

    //
    // Write to the notify address
    //
    if (Queue->NotifyAddr) {
        WRITE_REGISTER_USHORT((PUSHORT)Queue->NotifyAddr, Queue->Index);
    }
}
