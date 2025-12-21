/*
 * Zixiao VirtIO Network Driver - Virtqueue Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

/*
 * ZvioNetQueueCreate - Create and initialize a virtqueue
 */
NTSTATUS
ZvioNetQueueCreate(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ USHORT Index,
    _Out_ PZVIONET_VIRTQUEUE *Queue
    )
{
    PZVIONET_VIRTQUEUE vq;
    USHORT queueSize;
    SIZE_T descSize, availSize, usedSize, totalSize;
    NDIS_STATUS status;

    *Queue = NULL;

    if (!Adapter->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WRITE_REGISTER_USHORT(&Adapter->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    queueSize = READ_REGISTER_USHORT(&Adapter->CommonCfg->QueueSize);
    if (queueSize == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    ZvioNetDbgPrint("Creating queue %d with %d entries", Index, queueSize);

    vq = (PZVIONET_VIRTQUEUE)NdisAllocateMemoryWithTagPriority(
        Adapter->AdapterHandle,
        sizeof(ZVIONET_VIRTQUEUE),
        ZVIONET_TAG,
        NormalPoolPriority
        );

    if (!vq) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NdisZeroMemory(vq, sizeof(ZVIONET_VIRTQUEUE));
    vq->Index = Index;
    vq->Size = queueSize;
    vq->Adapter = Adapter;

    NdisAllocateSpinLock(&vq->Lock);

    descSize = sizeof(VRING_DESC) * queueSize;
    availSize = sizeof(USHORT) * (3 + queueSize);
    usedSize = sizeof(USHORT) * 3 + sizeof(VRING_USED_ELEM) * queueSize;
    totalSize = ROUND_TO_PAGES(descSize) + ROUND_TO_PAGES(availSize) + ROUND_TO_PAGES(usedSize);

    status = NdisMAllocateSharedMemory(
        Adapter->AdapterHandle,
        totalSize,
        FALSE,
        &vq->RingBuffer,
        &vq->RingBufferPA
        );

    if (status != NDIS_STATUS_SUCCESS || !vq->RingBuffer) {
        ZvioNetDbgError("Failed to allocate ring buffer");
        NdisFreeSpinLock(&vq->Lock);
        NdisFreeMemory(vq, sizeof(ZVIONET_VIRTQUEUE), 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    vq->RingBufferSize = totalSize;
    NdisZeroMemory(vq->RingBuffer, totalSize);

    vq->Desc = (PVRING_DESC)vq->RingBuffer;
    vq->DescPhys = vq->RingBufferPA;

    vq->Avail = (PVRING_AVAIL)((PUCHAR)vq->RingBuffer + ROUND_TO_PAGES(descSize));
    vq->AvailPhys.QuadPart = vq->RingBufferPA.QuadPart + ROUND_TO_PAGES(descSize);

    vq->Used = (PVRING_USED)((PUCHAR)vq->Avail + ROUND_TO_PAGES(availSize));
    vq->UsedPhys.QuadPart = vq->AvailPhys.QuadPart + ROUND_TO_PAGES(availSize);

    vq->NumFree = queueSize;
    vq->FreeHead = 0;
    for (USHORT i = 0; i < queueSize - 1; i++) {
        vq->Desc[i].Next = i + 1;
    }
    vq->Desc[queueSize - 1].Next = 0xFFFF;

    vq->DescData = (PVOID*)NdisAllocateMemoryWithTagPriority(
        Adapter->AdapterHandle,
        queueSize * sizeof(PVOID),
        ZVIONET_TAG,
        NormalPoolPriority
        );

    if (!vq->DescData) {
        NdisMFreeSharedMemory(Adapter->AdapterHandle, vq->RingBufferSize, FALSE, vq->RingBuffer, vq->RingBufferPA);
        NdisFreeSpinLock(&vq->Lock);
        NdisFreeMemory(vq, sizeof(ZVIONET_VIRTQUEUE), 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NdisZeroMemory(vq->DescData, queueSize * sizeof(PVOID));

    WRITE_REGISTER_USHORT(&Adapter->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    WRITE_REGISTER_ULONG64((PULONG64)&Adapter->CommonCfg->QueueDesc, vq->DescPhys.QuadPart);
    WRITE_REGISTER_ULONG64((PULONG64)&Adapter->CommonCfg->QueueDriver, vq->AvailPhys.QuadPart);
    WRITE_REGISTER_ULONG64((PULONG64)&Adapter->CommonCfg->QueueDevice, vq->UsedPhys.QuadPart);

    USHORT notifyOff = READ_REGISTER_USHORT(&Adapter->CommonCfg->QueueNotifyOff);
    vq->NotifyAddr = (PUCHAR)Adapter->NotifyBase + notifyOff * Adapter->NotifyOffMultiplier;

    WRITE_REGISTER_USHORT(&Adapter->CommonCfg->QueueEnable, 1);

    *Queue = vq;
    return STATUS_SUCCESS;
}

/*
 * ZvioNetQueueDestroy - Destroy a virtqueue
 */
VOID
ZvioNetQueueDestroy(
    _In_ PZVIONET_VIRTQUEUE Queue
    )
{
    if (!Queue) return;

    if (Queue->Adapter && Queue->Adapter->CommonCfg) {
        WRITE_REGISTER_USHORT(&Queue->Adapter->CommonCfg->QueueSel, Queue->Index);
        KeMemoryBarrier();
        WRITE_REGISTER_USHORT(&Queue->Adapter->CommonCfg->QueueEnable, 0);
    }

    if (Queue->DescData) {
        NdisFreeMemory(Queue->DescData, Queue->Size * sizeof(PVOID), 0);
    }

    if (Queue->RingBuffer && Queue->Adapter) {
        NdisMFreeSharedMemory(Queue->Adapter->AdapterHandle, Queue->RingBufferSize, FALSE, Queue->RingBuffer, Queue->RingBufferPA);
    }

    NdisFreeSpinLock(&Queue->Lock);
    NdisFreeMemory(Queue, sizeof(ZVIONET_VIRTQUEUE), 0);
}

/*
 * ZvioNetQueueAddBuffer - Add a single buffer to the virtqueue
 */
NTSTATUS
ZvioNetQueueAddBuffer(
    _In_ PZVIONET_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    )
{
    USHORT descIdx;
    USHORT availIdx;

    NdisAcquireSpinLock(&Queue->Lock);

    if (Queue->NumFree == 0) {
        NdisReleaseSpinLock(&Queue->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    descIdx = Queue->FreeHead;
    Queue->FreeHead = Queue->Desc[descIdx].Next;
    Queue->NumFree--;

    Queue->Desc[descIdx].Addr = PhysAddr.QuadPart;
    Queue->Desc[descIdx].Len = Length;
    Queue->Desc[descIdx].Flags = DeviceWritable ? VRING_DESC_F_WRITE : 0;
    Queue->Desc[descIdx].Next = 0xFFFF;

    Queue->DescData[descIdx] = UserData;

    availIdx = Queue->Avail->Idx & (Queue->Size - 1);
    Queue->Avail->Ring[availIdx] = descIdx;
    KeMemoryBarrier();
    Queue->Avail->Idx++;

    NdisReleaseSpinLock(&Queue->Lock);
    return STATUS_SUCCESS;
}

/*
 * ZvioNetQueueAddBufferChain - Add multiple buffers as a chain
 */
NTSTATUS
ZvioNetQueueAddBufferChain(
    _In_ PZVIONET_VIRTQUEUE Queue,
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

    *HeadIdx = 0xFFFF;

    if (totalDescs == 0 || totalDescs > Queue->Size) {
        return STATUS_INVALID_PARAMETER;
    }

    NdisAcquireSpinLock(&Queue->Lock);

    if (Queue->NumFree < totalDescs) {
        NdisReleaseSpinLock(&Queue->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    head = Queue->FreeHead;

    for (ULONG i = 0; i < SgList->NumberOfElements; i++) {
        descIdx = Queue->FreeHead;
        Queue->FreeHead = Queue->Desc[descIdx].Next;
        Queue->NumFree--;

        Queue->Desc[descIdx].Addr = SgList->Elements[i].Address.QuadPart;
        Queue->Desc[descIdx].Len = SgList->Elements[i].Length;
        Queue->Desc[descIdx].Flags = (i >= OutCount) ? VRING_DESC_F_WRITE : 0;

        if (prevIdx != 0xFFFF) {
            Queue->Desc[prevIdx].Next = descIdx;
            Queue->Desc[prevIdx].Flags |= VRING_DESC_F_NEXT;
        }

        prevIdx = descIdx;
    }

    Queue->Desc[descIdx].Next = 0xFFFF;
    Queue->DescData[head] = UserData;

    USHORT availIdx = Queue->Avail->Idx & (Queue->Size - 1);
    Queue->Avail->Ring[availIdx] = head;
    KeMemoryBarrier();
    Queue->Avail->Idx++;

    NdisReleaseSpinLock(&Queue->Lock);

    *HeadIdx = head;
    return STATUS_SUCCESS;
}

/*
 * ZvioNetQueueGetBuffer - Get a completed buffer
 */
PVOID
ZvioNetQueueGetBuffer(
    _In_ PZVIONET_VIRTQUEUE Queue,
    _Out_ PULONG Length
    )
{
    USHORT usedIdx;
    USHORT headIdx;
    USHORT descIdx;
    PVOID userData;

    *Length = 0;

    NdisAcquireSpinLock(&Queue->Lock);

    if (Queue->LastUsedIdx == Queue->Used->Idx) {
        NdisReleaseSpinLock(&Queue->Lock);
        return NULL;
    }

    usedIdx = Queue->LastUsedIdx & (Queue->Size - 1);
    headIdx = (USHORT)Queue->Used->Ring[usedIdx].Id;
    *Length = Queue->Used->Ring[usedIdx].Len;

    Queue->LastUsedIdx++;

    userData = Queue->DescData[headIdx];
    Queue->DescData[headIdx] = NULL;

    descIdx = headIdx;
    while (descIdx != 0xFFFF) {
        USHORT nextIdx = (Queue->Desc[descIdx].Flags & VRING_DESC_F_NEXT) ?
            Queue->Desc[descIdx].Next : 0xFFFF;

        Queue->Desc[descIdx].Next = Queue->FreeHead;
        Queue->FreeHead = descIdx;
        Queue->NumFree++;

        descIdx = nextIdx;
    }

    NdisReleaseSpinLock(&Queue->Lock);
    return userData;
}

/*
 * ZvioNetQueueKick - Notify the device
 */
VOID
ZvioNetQueueKick(
    _In_ PZVIONET_VIRTQUEUE Queue
    )
{
    KeMemoryBarrier();

    if (Queue->Used->Flags & VRING_USED_F_NO_NOTIFY) {
        return;
    }

    if (Queue->NotifyAddr) {
        WRITE_REGISTER_USHORT((PUSHORT)Queue->NotifyAddr, Queue->Index);
    }
}
