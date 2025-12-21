/*
 * Zixiao VirtIO Balloon Driver - VirtIO Device Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO device initialization and feature negotiation.
 */

#include "public.h"

/*
 * ZvioBlnDeviceInit - Initialize VirtIO balloon device
 */
NTSTATUS
ZvioBlnDeviceInit(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    UCHAR deviceStatus;
    ULONGLONG deviceFeatures;
    ULONGLONG driverFeatures;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioBlnDbgPrint("Initializing VirtIO balloon device");

    //
    // Step 1: Reset device
    //
    status = ZvioBlnDeviceReset(DeviceContext);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    //
    // Step 2: Set ACKNOWLEDGE status
    //
    ZvioBlnSetDeviceStatus(DeviceContext, VIRTIO_STATUS_ACKNOWLEDGE);

    //
    // Step 3: Set DRIVER status
    //
    deviceStatus = ZvioBlnGetDeviceStatus(DeviceContext);
    ZvioBlnSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER);

    //
    // Step 4: Read and negotiate features
    //
    deviceFeatures = ZvioBlnGetDeviceFeatures(DeviceContext);
    ZvioBlnDbgPrint("Device features: 0x%016llX", deviceFeatures);

    //
    // Select features we support
    //
    driverFeatures = deviceFeatures & (
        VIRTIO_F_VERSION_1 |
        VIRTIO_F_RING_INDIRECT_DESC |
        VIRTIO_F_RING_EVENT_IDX |
        VIRTIO_BALLOON_F_MUST_TELL_HOST |
        VIRTIO_BALLOON_F_STATS_VQ |
        VIRTIO_BALLOON_F_DEFLATE_ON_OOM
        );

    ZvioBlnDbgPrint("Driver features: 0x%016llX", driverFeatures);

    status = ZvioBlnSetDriverFeatures(DeviceContext, driverFeatures);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to set driver features");
        ZvioBlnSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return status;
    }

    //
    // Step 5: Set FEATURES_OK status
    //
    deviceStatus = ZvioBlnGetDeviceStatus(DeviceContext);
    ZvioBlnSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_FEATURES_OK);

    //
    // Step 6: Verify FEATURES_OK
    //
    deviceStatus = ZvioBlnGetDeviceStatus(DeviceContext);
    if (!(deviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
        ZvioBlnDbgError("Device did not accept features");
        ZvioBlnSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return STATUS_DEVICE_FEATURE_NOT_SUPPORTED;
    }

    //
    // Step 7: Create virtqueues
    //
    // Queue 0: Inflate (pages to give to host)
    status = ZvioBlnQueueCreate(DeviceContext, 0, &DeviceContext->InflateQueue);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to create inflate queue");
        ZvioBlnSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return status;
    }

    // Queue 1: Deflate (pages to reclaim from host)
    status = ZvioBlnQueueCreate(DeviceContext, 1, &DeviceContext->DeflateQueue);
    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to create deflate queue");
        ZvioBlnQueueDestroy(DeviceContext->InflateQueue);
        DeviceContext->InflateQueue = NULL;
        ZvioBlnSetDeviceStatus(DeviceContext, VIRTIO_STATUS_FAILED);
        return status;
    }

    // Queue 2: Statistics (optional)
    if (driverFeatures & VIRTIO_BALLOON_F_STATS_VQ) {
        status = ZvioBlnQueueCreate(DeviceContext, 2, &DeviceContext->StatsQueue);
        if (!NT_SUCCESS(status)) {
            ZvioBlnDbgPrint("Stats queue not available (optional)");
            // Not fatal - stats is optional
        }
    }

    //
    // Step 8: Set DRIVER_OK status - device is live
    //
    deviceStatus = ZvioBlnGetDeviceStatus(DeviceContext);
    ZvioBlnSetDeviceStatus(DeviceContext, deviceStatus | VIRTIO_STATUS_DRIVER_OK);

    ZvioBlnDbgPrint("VirtIO balloon device initialized successfully");
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnDeviceReset - Reset the VirtIO device
 */
NTSTATUS
ZvioBlnDeviceReset(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    UCHAR status;
    ULONG timeout = 1000;

    if (!DeviceContext->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    //
    // Write 0 to device status to reset
    //
    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, 0);

    //
    // Wait for reset to complete (status reads as 0)
    //
    while (timeout--) {
        status = READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
        if (status == 0) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);  // 1ms delay
    }

    ZvioBlnDbgError("Device reset timeout");
    return STATUS_TIMEOUT;
}

/*
 * ZvioBlnGetDeviceFeatures - Read device feature bits
 */
ULONGLONG
ZvioBlnGetDeviceFeatures(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    ULONGLONG features = 0;
    ULONG featuresLow, featuresHigh;

    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    //
    // Read low 32 bits (selector = 0)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 0);
    KeMemoryBarrier();
    featuresLow = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    //
    // Read high 32 bits (selector = 1)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeatureSel, 1);
    KeMemoryBarrier();
    featuresHigh = READ_REGISTER_ULONG(&DeviceContext->CommonCfg->DeviceFeature);

    features = ((ULONGLONG)featuresHigh << 32) | featuresLow;
    DeviceContext->DeviceFeatures = features;

    return features;
}

/*
 * ZvioBlnSetDriverFeatures - Set driver feature bits
 */
NTSTATUS
ZvioBlnSetDriverFeatures(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
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
    // Write low 32 bits (selector = 0)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 0);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresLow);

    //
    // Write high 32 bits (selector = 1)
    //
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeatureSel, 1);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&DeviceContext->CommonCfg->DriverFeature, featuresHigh);

    DeviceContext->DriverFeatures = Features;
    return STATUS_SUCCESS;
}

/*
 * ZvioBlnGetDeviceStatus - Read device status
 */
UCHAR
ZvioBlnGetDeviceStatus(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    if (!DeviceContext->CommonCfg) {
        return 0;
    }

    return READ_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus);
}

/*
 * ZvioBlnSetDeviceStatus - Set device status
 */
VOID
ZvioBlnSetDeviceStatus(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    )
{
    if (!DeviceContext->CommonCfg) {
        return;
    }

    WRITE_REGISTER_UCHAR(&DeviceContext->CommonCfg->DeviceStatus, Status);
    KeMemoryBarrier();
}
