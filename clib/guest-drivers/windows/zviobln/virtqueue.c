/*
 * Zixiao VirtIO Balloon Driver - Virtqueue Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO virtqueue implementation for balloon device.
 */

#include "public.h"

/*
 * Calculate required ring buffer size
 */
static
ULONG
ZvioBlnQueueRingSize(
    _In_ USHORT QueueSize
    )
{
    ULONG descSize = sizeof(VRING_DESC) * QueueSize;
    ULONG availSize = sizeof(USHORT) * (3 + QueueSize);
    ULONG usedSize = sizeof(USHORT) * 3 + sizeof(VRING_USED_ELEM) * QueueSize;

    //
    // Align available ring to 2 bytes, used ring to 4 bytes
    //
    descSize = (descSize + 15) & ~15;
    availSize = (availSize + 1) & ~1;
    usedSize = (usedSize + 3) & ~3;

    return descSize + availSize + usedSize;
}

/*
 * ZvioBlnQueueCreate - Create a virtqueue
 */
NTSTATUS
ZvioBlnQueueCreate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIOBLN_VIRTQUEUE *Queue
    )
{
    NTSTATUS status;
    PZVIOBLN_VIRTQUEUE queue;
    USHORT queueSize;
    ULONG ringSize;
    PVOID ringVA;
    PHYSICAL_ADDRESS ringPA;
    PUCHAR ringPtr;
    USHORT i;

    *Queue = NULL;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    //
    // Select queue and get size
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    queueSize = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSize);
    if (queueSize == 0) {
        ZvioBlnDbgError("Queue %u not available", Index);
        return STATUS_DEVICE_NOT_READY;
    }

    //
    // Cap queue size at 256 for balloon
    //
    if (queueSize > 256) {
        queueSize = 256;
        WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSize, queueSize);
    }

    ZvioBlnDbgPrint("Queue %u size: %u", Index, queueSize);

    //
    // Allocate queue structure
    //
    queue = (PZVIOBLN_VIRTQUEUE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(ZVIOBLN_VIRTQUEUE),
        ZVIOBLN_TAG
        );

    if (!queue) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(queue, sizeof(ZVIOBLN_VIRTQUEUE));
    queue->Index = Index;
    queue->Size = queueSize;
    queue->DeviceContext = DeviceContext;

    //
    // Allocate ring buffer
    //
    ringSize = ZvioBlnQueueRingSize(queueSize);

    status = WdfCommonBufferCreate(
        DeviceContext->DmaEnabler,
        ringSize,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue->RingBuffer
        );

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(queue, ZVIOBLN_TAG);
        return status;
    }

    ringVA = WdfCommonBufferGetAlignedVirtualAddress(queue->RingBuffer);
    ringPA = WdfCommonBufferGetAlignedLogicalAddress(queue->RingBuffer);

    RtlZeroMemory(ringVA, ringSize);

    //
    // Set up ring pointers
    //
    ringPtr = (PUCHAR)ringVA;

    queue->Desc = (PVRING_DESC)ringPtr;
    queue->DescPhys = ringPA;
    ringPtr += sizeof(VRING_DESC) * queueSize;
    ringPtr = (PUCHAR)(((ULONG_PTR)ringPtr + 15) & ~15);

    queue->Avail = (PVRING_AVAIL)ringPtr;
    queue->AvailPhys.QuadPart = ringPA.QuadPart + (ringPtr - (PUCHAR)ringVA);
    ringPtr += sizeof(USHORT) * (3 + queueSize);
    ringPtr = (PUCHAR)(((ULONG_PTR)ringPtr + 1) & ~1);

    queue->Used = (PVRING_USED)ringPtr;
    queue->UsedPhys.QuadPart = ringPA.QuadPart + (ringPtr - (PUCHAR)ringVA);

    //
    // Allocate descriptor data tracking array
    //
    queue->DescData = (PVOID*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(PVOID) * queueSize,
        ZVIOBLN_TAG
        );

    if (!queue->DescData) {
        WdfObjectDelete(queue->RingBuffer);
        ExFreePoolWithTag(queue, ZVIOBLN_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(queue->DescData, sizeof(PVOID) * queueSize);

    //
    // Initialize descriptor free list
    //
    for (i = 0; i < queueSize - 1; i++) {
        queue->Desc[i].Next = i + 1;
    }
    queue->Desc[queueSize - 1].Next = 0xFFFF;
    queue->FreeHead = 0;
    queue->NumFree = queueSize;

    //
    // Create spin lock
    //
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &queue->Lock);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(queue->DescData, ZVIOBLN_TAG);
        WdfObjectDelete(queue->RingBuffer);
        ExFreePoolWithTag(queue, ZVIOBLN_TAG);
        return status;
    }

    //
    // Configure queue in device
    //
    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDesc, queue->DescPhys.QuadPart);
    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDriver, queue->AvailPhys.QuadPart);
    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDevice, queue->UsedPhys.QuadPart);
    KeMemoryBarrier();

    //
    // Enable queue
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueEnable, 1);
    KeMemoryBarrier();

    //
    // Calculate notify address
    //
    USHORT notifyOff = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueNotifyOff);
    queue->NotifyAddr = (PUCHAR)DeviceContext->NotifyBase +
                        notifyOff * DeviceContext->NotifyOffMultiplier;

    ZvioBlnDbgPrint("Queue %u created: Desc=%p Avail=%p Used=%p Notify=%p",
        Index, queue->Desc, queue->Avail, queue->Used, queue->NotifyAddr);

    *Queue = queue;
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnQueueDestroy - Destroy a virtqueue
 */
VOID
ZvioBlnQueueDestroy(
    _In_ PZVIOBLN_VIRTQUEUE Queue
    )
{
    if (!Queue) {
        return;
    }

    //
    // Disable queue in device
    //
    if (Queue->DeviceContext && Queue->DeviceContext->CommonCfg) {
        WRITE_REGISTER_USHORT(&Queue->DeviceContext->CommonCfg->QueueSel, Queue->Index);
        KeMemoryBarrier();
        WRITE_REGISTER_USHORT(&Queue->DeviceContext->CommonCfg->QueueEnable, 0);
    }

    if (Queue->DescData) {
        ExFreePoolWithTag(Queue->DescData, ZVIOBLN_TAG);
    }

    if (Queue->RingBuffer) {
        WdfObjectDelete(Queue->RingBuffer);
    }

    ExFreePoolWithTag(Queue, ZVIOBLN_TAG);
}

/*
 * ZvioBlnQueueAddBuffer - Add a buffer to the virtqueue
 */
NTSTATUS
ZvioBlnQueueAddBuffer(
    _In_ PZVIOBLN_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    )
{
    USHORT descIdx;
    USHORT availIdx;

    if (!Queue || Queue->NumFree == 0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WdfSpinLockAcquire(Queue->Lock);

    //
    // Get free descriptor
    //
    descIdx = Queue->FreeHead;
    Queue->FreeHead = Queue->Desc[descIdx].Next;
    Queue->NumFree--;

    //
    // Fill descriptor
    //
    Queue->Desc[descIdx].Addr = PhysAddr.QuadPart;
    Queue->Desc[descIdx].Len = Length;
    Queue->Desc[descIdx].Flags = DeviceWritable ? VRING_DESC_F_WRITE : 0;
    Queue->Desc[descIdx].Next = 0;

    Queue->DescData[descIdx] = UserData;

    //
    // Add to available ring
    //
    availIdx = Queue->Avail->Idx & (Queue->Size - 1);
    Queue->Avail->Ring[availIdx] = descIdx;
    KeMemoryBarrier();

    Queue->Avail->Idx++;
    KeMemoryBarrier();

    WdfSpinLockRelease(Queue->Lock);

    return STATUS_SUCCESS;
}

/*
 * ZvioBlnQueueGetBuffer - Get a completed buffer from the virtqueue
 */
PVOID
ZvioBlnQueueGetBuffer(
    _In_ PZVIOBLN_VIRTQUEUE Queue,
    _Out_ PULONG Length
    )
{
    USHORT usedIdx;
    USHORT descIdx;
    PVOID userData;

    *Length = 0;

    if (!Queue) {
        return NULL;
    }

    WdfSpinLockAcquire(Queue->Lock);

    if (Queue->LastUsedIdx == Queue->Used->Idx) {
        WdfSpinLockRelease(Queue->Lock);
        return NULL;
    }

    //
    // Get used entry
    //
    usedIdx = Queue->LastUsedIdx & (Queue->Size - 1);
    descIdx = (USHORT)Queue->Used->Ring[usedIdx].Id;
    *Length = Queue->Used->Ring[usedIdx].Len;

    Queue->LastUsedIdx++;

    //
    // Get user data
    //
    userData = Queue->DescData[descIdx];
    Queue->DescData[descIdx] = NULL;

    //
    // Return descriptor to free list
    //
    Queue->Desc[descIdx].Next = Queue->FreeHead;
    Queue->FreeHead = descIdx;
    Queue->NumFree++;

    WdfSpinLockRelease(Queue->Lock);

    return userData;
}

/*
 * ZvioBlnQueueKick - Notify device about new available buffers
 */
VOID
ZvioBlnQueueKick(
    _In_ PZVIOBLN_VIRTQUEUE Queue
    )
{
    if (!Queue || !Queue->NotifyAddr) {
        return;
    }

    KeMemoryBarrier();

    //
    // Check if notification is needed
    //
    if (!(Queue->Used->Flags & VRING_USED_F_NO_NOTIFY)) {
        WRITE_REGISTER_USHORT((PUSHORT)Queue->NotifyAddr, Queue->Index);
    }
}
