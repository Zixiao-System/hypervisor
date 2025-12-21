/*
 * Zixiao VirtIO PCI Bus Driver - VirtIO Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO device initialization and virtqueue operations.
 */

#include "public.h"

/*
 * ZvioDeviceInit - Initialize VirtIO device
 *
 * Performs the VirtIO device initialization sequence:
 * 1. Reset device
 * 2. Set ACKNOWLEDGE status
 * 3. Set DRIVER status
 * 4. Negotiate features
 * 5. Set FEATURES_OK status
 * 6. Verify FEATURES_OK
 * 7. Setup virtqueues
 * 8. Set DRIVER_OK status
 */
NTSTATUS
ZvioDeviceInit(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    UCHAR deviceStatus;
    ULONGLONG deviceFeatures;
    ULONGLONG driverFeatures;
    USHORT numQueues;
    USHORT i;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioDbgPrint("Initializing VirtIO device");

    //
    // Step 1: Reset the device
    //
    status = ZvioDeviceReset(DeviceContext);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    //
    // Step 2: Set ACKNOWLEDGE status
    //
    ZvioSetDeviceStatus(DeviceContext, VIRTIO_STATUS_ACKNOWLEDGE);

    //
    // Step 3: Set DRIVER status
    //
    deviceStatus = ZvioGetDeviceStatus(DeviceContext);
    ZvioSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER);

    //
    // Step 4: Read and negotiate features
    //
    deviceFeatures = ZvioGetDeviceFeatures(DeviceContext);
    ZvioDbgPrint("Device features: 0x%016llX", deviceFeatures);

    //
    // Select features we support
    // For the bus driver, we accept common VirtIO 1.0 features
    //
    driverFeatures = deviceFeatures & (
        VIRTIO_F_VERSION_1 |
        VIRTIO_F_RING_INDIRECT_DESC |
        VIRTIO_F_RING_EVENT_IDX |
        VIRTIO_F_IN_ORDER
        );

    ZvioDbgPrint("Driver features: 0x%016llX", driverFeatures);

    status = ZvioSetDriverFeatures(DeviceContext, driverFeatures);
    if (!NT_SUCCESS(status)) {
        ZvioDbgError("Failed to set driver features");
        ZvioSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return status;
    }

    //
    // Step 5: Set FEATURES_OK status
    //
    deviceStatus = ZvioGetDeviceStatus(DeviceContext);
    ZvioSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_FEATURES_OK);

    //
    // Step 6: Verify FEATURES_OK was accepted
    //
    deviceStatus = ZvioGetDeviceStatus(DeviceContext);
    if (!(deviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
        ZvioDbgError("Device did not accept features");
        ZvioSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return STATUS_DEVICE_FEATURE_NOT_SUPPORTED;
    }

    //
    // Step 7: Read number of queues and allocate queue array
    //
    numQueues = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->NumQueues);
    ZvioDbgPrint("Device has %d queues", numQueues);

    if (numQueues > 0) {
        DeviceContext->Queues = (PZVIO_VIRTQUEUE*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            numQueues * sizeof(PZVIO_VIRTQUEUE),
            ZVIO_POOL_TAG
            );

        if (!DeviceContext->Queues) {
            ZvioDbgError("Failed to allocate queue array");
            ZvioSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(DeviceContext->Queues, numQueues * sizeof(PZVIO_VIRTQUEUE));
        DeviceContext->NumQueues = numQueues;

        //
        // Create each virtqueue
        //
        for (i = 0; i < numQueues; i++) {
            status = ZvioQueueCreate(DeviceContext, i, &DeviceContext->Queues[i]);
            if (!NT_SUCCESS(status)) {
                ZvioDbgError("Failed to create queue %d: 0x%08X", i, status);
                // Continue trying other queues
            }
        }
    }

    //
    // Step 8: Set DRIVER_OK status
    //
    deviceStatus = ZvioGetDeviceStatus(DeviceContext);
    ZvioSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER_OK);

    ZvioDbgPrint("VirtIO device initialized successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioDeviceReset - Reset the VirtIO device
 */
NTSTATUS
ZvioDeviceReset(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    )
{
    UCHAR status;
    ULONG timeout = 1000; // 1 second timeout

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioDbgPrint("Resetting VirtIO device");

    //
    // Write 0 to device status to reset
    //
    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, 0);

    //
    // Wait for reset to complete (status should read 0)
    //
    while (timeout--) {
        status = READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
        if (status == 0) {
            ZvioDbgPrint("Device reset complete");
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000); // 1ms
    }

    ZvioDbgError("Device reset timeout");
    return STATUS_TIMEOUT;
}

/*
 * ZvioGetDeviceFeatures - Read device feature bits
 */
ULONGLONG
ZvioGetDeviceFeatures(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    )
{
    ULONGLONG features = 0;
    ULONG featuresLow, featuresHigh;

    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    //
    // Read low 32 bits (select = 0)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 0);
    KeMemoryBarrier();
    featuresLow = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    //
    // Read high 32 bits (select = 1)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 1);
    KeMemoryBarrier();
    featuresHigh = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    features = ((ULONGLONG)featuresHigh << 32) | featuresLow;

    DeviceContext->DeviceFeatures = features;
    return features;
}

/*
 * ZvioSetDriverFeatures - Set driver feature bits
 */
NTSTATUS
ZvioSetDriverFeatures(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ ULONGLONG Features
    )
{
    ULONG featuresLow, featuresHigh;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    featuresLow = (ULONG)(Features & 0xFFFFFFFF);
    featuresHigh = (ULONG)(Features >> 32);

    //
    // Write low 32 bits (select = 0)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 0);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresLow);

    //
    // Write high 32 bits (select = 1)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 1);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresHigh);

    DeviceContext->DriverFeatures = Features;
    return STATUS_SUCCESS;
}

/*
 * ZvioGetDeviceStatus - Read device status
 */
UCHAR
ZvioGetDeviceStatus(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    )
{
    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    return READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
}

/*
 * ZvioSetDeviceStatus - Set device status
 */
VOID
ZvioSetDeviceStatus(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    )
{
    if (!DeviceContext->CommonCfg) {
        return;
    }

    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, Status);
    KeMemoryBarrier();
}

/*
 * ZvioQueueCreate - Create and initialize a virtqueue
 */
NTSTATUS
ZvioQueueCreate(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIO_VIRTQUEUE *Queue
    )
{
    NTSTATUS status;
    PZVIO_VIRTQUEUE vq;
    USHORT queueSize;
    SIZE_T descSize, availSize, usedSize, totalSize;
    WDF_COMMON_BUFFER_CONFIG bufferConfig;
    PHYSICAL_ADDRESS highestAddr = { .QuadPart = MAXULONG64 };

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
        ZvioDbgPrint("Queue %d not available", Index);
        return STATUS_DEVICE_NOT_READY;
    }

    ZvioDbgPrint("Creating queue %d with %d entries", Index, queueSize);

    //
    // Allocate queue structure
    //
    vq = (PZVIO_VIRTQUEUE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(ZVIO_VIRTQUEUE),
        ZVIO_POOL_TAG
        );

    if (!vq) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(vq, sizeof(ZVIO_VIRTQUEUE));
    vq->Index = Index;
    vq->Size = queueSize;
    vq->DeviceContext = DeviceContext;

    //
    // Create spinlock for queue
    //
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &vq->Lock);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(vq, ZVIO_POOL_TAG);
        return status;
    }

    //
    // Calculate ring sizes
    // Descriptor table: 16 bytes per entry
    // Available ring: 2 bytes header + 2 bytes per entry + 2 bytes event
    // Used ring: 2 bytes header + 8 bytes per entry + 2 bytes event
    //
    descSize = sizeof(VRING_DESC) * queueSize;
    availSize = sizeof(USHORT) * (3 + queueSize);  // flags + idx + ring + event
    usedSize = sizeof(USHORT) * 3 + sizeof(VRING_USED_ELEM) * queueSize;

    // Align to 4K page boundary
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
        ZvioDbgError("Failed to allocate ring buffer: 0x%08X", status);
        WdfObjectDelete(vq->Lock);
        ExFreePoolWithTag(vq, ZVIO_POOL_TAG);
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
    // Initialize descriptor chain (all descriptors are free initially)
    //
    vq->NumFree = queueSize;
    vq->FreeHead = 0;
    for (USHORT i = 0; i < queueSize - 1; i++) {
        vq->Desc[i].Next = i + 1;
    }
    vq->Desc[queueSize - 1].Next = 0xFFFF; // End of chain

    //
    // Allocate user data tracking array
    //
    vq->DescData = (PVOID*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        queueSize * sizeof(PVOID),
        ZVIO_POOL_TAG
        );

    if (!vq->DescData) {
        WdfObjectDelete(vq->RingBuffer);
        WdfObjectDelete(vq->Lock);
        ExFreePoolWithTag(vq, ZVIO_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(vq->DescData, queueSize * sizeof(PVOID));

    //
    // Write queue addresses to device
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueSel, Index);
    KeMemoryBarrier();

    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDesc, vq->DescPhys.QuadPart);
    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDriver, vq->AvailPhys.QuadPart);
    WRITE_REGISTER_ULONGLONG(&DeviceContext->CommonCfg->QueueDevice, vq->UsedPhys.QuadPart);

    //
    // Calculate notify address for this queue
    //
    USHORT notifyOff = READ_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueNotifyOff);
    vq->NotifyAddr = (PUCHAR)DeviceContext->NotifyBase +
                     notifyOff * DeviceContext->NotifyOffMultiplier;

    //
    // Enable the queue
    //
    WRITE_REGISTER_USHORT(&DeviceContext->CommonCfg->QueueEnable, 1);

    ZvioDbgPrint("Queue %d created: desc=0x%llX avail=0x%llX used=0x%llX",
        Index, vq->DescPhys.QuadPart, vq->AvailPhys.QuadPart, vq->UsedPhys.QuadPart);

    *Queue = vq;
    return STATUS_SUCCESS;
}

/*
 * ZvioQueueDestroy - Destroy a virtqueue
 */
VOID
ZvioQueueDestroy(
    _In_ PZVIO_VIRTQUEUE Queue
    )
{
    if (!Queue) {
        return;
    }

    ZvioDbgPrint("Destroying queue %d", Queue->Index);

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
        ExFreePoolWithTag(Queue->DescData, ZVIO_POOL_TAG);
    }

    if (Queue->RingBuffer) {
        WdfObjectDelete(Queue->RingBuffer);
    }

    if (Queue->Lock) {
        WdfObjectDelete(Queue->Lock);
    }

    ExFreePoolWithTag(Queue, ZVIO_POOL_TAG);
}

/*
 * ZvioQueueAddBuffer - Add a buffer to the virtqueue
 */
NTSTATUS
ZvioQueueAddBuffer(
    _In_ PZVIO_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    )
{
    USHORT descIdx;
    USHORT availIdx;

    WdfSpinLockAcquire(Queue->Lock);

    if (Queue->NumFree == 0) {
        WdfSpinLockRelease(Queue->Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Get a free descriptor
    //
    descIdx = Queue->FreeHead;
    Queue->FreeHead = Queue->Desc[descIdx].Next;
    Queue->NumFree--;

    //
    // Fill in the descriptor
    //
    Queue->Desc[descIdx].Addr = PhysAddr.QuadPart;
    Queue->Desc[descIdx].Len = Length;
    Queue->Desc[descIdx].Flags = DeviceWritable ? VRING_DESC_F_WRITE : 0;
    Queue->Desc[descIdx].Next = 0xFFFF;

    Queue->DescData[descIdx] = UserData;

    //
    // Add to available ring
    //
    availIdx = Queue->Avail->Idx & (Queue->Size - 1);
    Queue->Avail->Ring[availIdx] = descIdx;
    KeMemoryBarrier();
    Queue->Avail->Idx++;

    WdfSpinLockRelease(Queue->Lock);

    return STATUS_SUCCESS;
}

/*
 * ZvioQueueGetBuffer - Get a completed buffer from the virtqueue
 */
PVOID
ZvioQueueGetBuffer(
    _In_ PZVIO_VIRTQUEUE Queue,
    _Out_ PULONG Length
    )
{
    USHORT usedIdx;
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
    descIdx = (USHORT)Queue->Used->Ring[usedIdx].Id;
    *Length = Queue->Used->Ring[usedIdx].Len;

    Queue->LastUsedIdx++;

    //
    // Get user data and return descriptor to free list
    //
    userData = Queue->DescData[descIdx];
    Queue->DescData[descIdx] = NULL;

    Queue->Desc[descIdx].Next = Queue->FreeHead;
    Queue->FreeHead = descIdx;
    Queue->NumFree++;

    WdfSpinLockRelease(Queue->Lock);

    return userData;
}

/*
 * ZvioQueueKick - Notify the device of new buffers
 */
VOID
ZvioQueueKick(
    _In_ PZVIO_VIRTQUEUE Queue
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

/*
 * ZvioQueueEnableInterrupts - Enable/disable queue interrupts
 */
BOOLEAN
ZvioQueueEnableInterrupts(
    _In_ PZVIO_VIRTQUEUE Queue,
    _In_ BOOLEAN Enable
    )
{
    BOOLEAN wasEnabled;

    WdfSpinLockAcquire(Queue->Lock);

    wasEnabled = !(Queue->Avail->Flags & VRING_AVAIL_F_NO_INTERRUPT);

    if (Enable) {
        Queue->Avail->Flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    } else {
        Queue->Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
    }

    KeMemoryBarrier();

    WdfSpinLockRelease(Queue->Lock);

    return wasEnabled;
}
