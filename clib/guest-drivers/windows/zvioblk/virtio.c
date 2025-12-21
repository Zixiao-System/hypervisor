/*
 * Zixiao VirtIO Block Driver - VirtIO Device Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO device initialization and feature negotiation.
 */

#include "public.h"

/*
 * ZvioBlkDeviceInit - Initialize VirtIO block device
 */
NTSTATUS
ZvioBlkDeviceInit(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    UCHAR deviceStatus;
    ULONGLONG deviceFeatures;
    ULONGLONG driverFeatures;
    USHORT numQueues;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioBlkDbgPrint("Initializing VirtIO block device");

    //
    // Step 1: Reset the device
    //
    status = ZvioBlkDeviceReset(DeviceContext);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    //
    // Step 2: Set ACKNOWLEDGE status
    //
    ZvioBlkSetDeviceStatus(DeviceContext, VIRTIO_STATUS_ACKNOWLEDGE);

    //
    // Step 3: Set DRIVER status
    //
    deviceStatus = ZvioBlkGetDeviceStatus(DeviceContext);
    ZvioBlkSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER);

    //
    // Step 4: Read and negotiate features
    //
    deviceFeatures = ZvioBlkGetDeviceFeatures(DeviceContext);
    ZvioBlkDbgPrint("Device features: 0x%016llX", deviceFeatures);

    //
    // Select features we support
    //
    driverFeatures = deviceFeatures & (
        VIRTIO_F_VERSION_1 |
        VIRTIO_F_RING_INDIRECT_DESC |
        VIRTIO_F_RING_EVENT_IDX |
        VIRTIO_BLK_F_SIZE_MAX |
        VIRTIO_BLK_F_SEG_MAX |
        VIRTIO_BLK_F_BLK_SIZE |
        VIRTIO_BLK_F_FLUSH |
        VIRTIO_BLK_F_TOPOLOGY |
        VIRTIO_BLK_F_DISCARD |
        VIRTIO_BLK_F_WRITE_ZEROES |
        VIRTIO_BLK_F_RO
        );

    // Note: We read VIRTIO_BLK_F_RO to detect read-only, but we don't negotiate it as a driver feature

    ZvioBlkDbgPrint("Driver features: 0x%016llX", driverFeatures);

    status = ZvioBlkSetDriverFeatures(DeviceContext, driverFeatures);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Failed to set driver features");
        ZvioBlkSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return status;
    }

    //
    // Step 5: Set FEATURES_OK status
    //
    deviceStatus = ZvioBlkGetDeviceStatus(DeviceContext);
    ZvioBlkSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_FEATURES_OK);

    //
    // Step 6: Verify FEATURES_OK was accepted
    //
    deviceStatus = ZvioBlkGetDeviceStatus(DeviceContext);
    if (!(deviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
        ZvioBlkDbgError("Device did not accept features");
        ZvioBlkSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return STATUS_DEVICE_FEATURE_NOT_SUPPORTED;
    }

    //
    // Step 7: Read number of queues and create them
    //
    if (driverFeatures & VIRTIO_BLK_F_MQ) {
        // Multi-queue is supported, but for simplicity use single queue
        numQueues = 1;
    } else {
        numQueues = 1;
    }

    ZvioBlkDbgPrint("Creating %d request queue(s)", numQueues);

    DeviceContext->Queues = (PZVIOBLK_VIRTQUEUE*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        numQueues * sizeof(PZVIOBLK_VIRTQUEUE),
        ZVIOBLK_TAG
        );

    if (!DeviceContext->Queues) {
        ZvioBlkDbgError("Failed to allocate queue array");
        ZvioBlkSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DeviceContext->Queues, numQueues * sizeof(PZVIOBLK_VIRTQUEUE));
    DeviceContext->NumQueues = numQueues;

    for (USHORT i = 0; i < numQueues; i++) {
        status = ZvioBlkQueueCreate(DeviceContext, i, &DeviceContext->Queues[i]);
        if (!NT_SUCCESS(status)) {
            ZvioBlkDbgError("Failed to create queue %d: 0x%08X", i, status);
            // Continue - some queues may still work
        }
    }

    //
    // Step 8: Set DRIVER_OK status
    //
    deviceStatus = ZvioBlkGetDeviceStatus(DeviceContext);
    ZvioBlkSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER_OK);

    ZvioBlkDbgPrint("VirtIO block device initialized successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkDeviceReset - Reset the VirtIO device
 */
NTSTATUS
ZvioBlkDeviceReset(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    )
{
    UCHAR status;
    ULONG timeout = 1000;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioBlkDbgPrint("Resetting VirtIO device");

    //
    // Write 0 to device status to reset
    //
    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, 0);

    //
    // Wait for reset to complete
    //
    while (timeout--) {
        status = READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
        if (status == 0) {
            ZvioBlkDbgPrint("Device reset complete");
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);
    }

    ZvioBlkDbgError("Device reset timeout");
    return STATUS_TIMEOUT;
}

/*
 * ZvioBlkGetDeviceFeatures - Read device feature bits
 */
ULONGLONG
ZvioBlkGetDeviceFeatures(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    )
{
    ULONGLONG features = 0;
    ULONG featuresLow, featuresHigh;

    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    //
    // Read low 32 bits
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 0);
    KeMemoryBarrier();
    featuresLow = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    //
    // Read high 32 bits
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 1);
    KeMemoryBarrier();
    featuresHigh = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    features = ((ULONGLONG)featuresHigh << 32) | featuresLow;
    DeviceContext->DeviceFeatures = features;

    return features;
}

/*
 * ZvioBlkSetDriverFeatures - Set driver feature bits
 */
NTSTATUS
ZvioBlkSetDriverFeatures(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
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
    // Write low 32 bits
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 0);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresLow);

    //
    // Write high 32 bits
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 1);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresHigh);

    DeviceContext->DriverFeatures = Features;
    return STATUS_SUCCESS;
}

/*
 * ZvioBlkGetDeviceStatus - Read device status
 */
UCHAR
ZvioBlkGetDeviceStatus(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    )
{
    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    return READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
}

/*
 * ZvioBlkSetDeviceStatus - Set device status
 */
VOID
ZvioBlkSetDeviceStatus(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    )
{
    if (!DeviceContext->CommonCfg) {
        return;
    }

    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, Status);
    KeMemoryBarrier();
}
