/*
 * Zixiao VirtIO Network Driver - VirtIO Device Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "public.h"

/*
 * ZvioNetDeviceInit - Initialize VirtIO network device
 */
NTSTATUS
ZvioNetDeviceInit(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    NTSTATUS status;
    UCHAR deviceStatus;
    ULONGLONG deviceFeatures;
    ULONGLONG driverFeatures;

    if (!Adapter->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ZvioNetDbgPrint("Initializing VirtIO network device");

    // Reset
    status = ZvioNetDeviceReset(Adapter);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Set ACKNOWLEDGE
    ZvioNetSetDeviceStatus(Adapter, VIRTIO_STATUS_ACKNOWLEDGE);

    // Set DRIVER
    deviceStatus = ZvioNetGetDeviceStatus(Adapter);
    ZvioNetSetDeviceStatus(Adapter, deviceStatus | VIRTIO_STATUS_DRIVER);

    // Read and negotiate features
    deviceFeatures = ZvioNetGetDeviceFeatures(Adapter);
    ZvioNetDbgPrint("Device features: 0x%016llX", deviceFeatures);

    driverFeatures = deviceFeatures & (
        VIRTIO_F_VERSION_1 |
        VIRTIO_F_RING_INDIRECT_DESC |
        VIRTIO_F_RING_EVENT_IDX |
        VIRTIO_NET_F_MAC |
        VIRTIO_NET_F_STATUS |
        VIRTIO_NET_F_MTU |
        VIRTIO_NET_F_CSUM |
        VIRTIO_NET_F_HOST_TSO4 |
        VIRTIO_NET_F_HOST_TSO6 |
        VIRTIO_NET_F_GUEST_CSUM |
        VIRTIO_NET_F_GUEST_TSO4 |
        VIRTIO_NET_F_GUEST_TSO6 |
        VIRTIO_NET_F_CTRL_VQ |
        VIRTIO_NET_F_CTRL_RX |
        VIRTIO_NET_F_MRG_RXBUF
        );

    ZvioNetDbgPrint("Driver features: 0x%016llX", driverFeatures);

    status = ZvioNetSetDriverFeatures(Adapter, driverFeatures);
    if (!NT_SUCCESS(status)) {
        ZvioNetDbgError("Failed to set driver features");
        ZvioNetSetDeviceStatus(Adapter, VIRTIO_STATUS_FAILED);
        return status;
    }

    // Set FEATURES_OK
    deviceStatus = ZvioNetGetDeviceStatus(Adapter);
    ZvioNetSetDeviceStatus(Adapter, deviceStatus | VIRTIO_STATUS_FEATURES_OK);

    // Verify FEATURES_OK
    deviceStatus = ZvioNetGetDeviceStatus(Adapter);
    if (!(deviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
        ZvioNetDbgError("Device did not accept features");
        ZvioNetSetDeviceStatus(Adapter, VIRTIO_STATUS_FAILED);
        return STATUS_DEVICE_FEATURE_NOT_SUPPORTED;
    }

    // Set up offload capabilities
    if (driverFeatures & VIRTIO_NET_F_CSUM) {
        Adapter->TxChecksumOffload = TRUE;
    }
    if (driverFeatures & VIRTIO_NET_F_GUEST_CSUM) {
        Adapter->RxChecksumOffload = TRUE;
    }
    if (driverFeatures & VIRTIO_NET_F_HOST_TSO4) {
        Adapter->LsoV2Ipv4 = TRUE;
    }
    if (driverFeatures & VIRTIO_NET_F_HOST_TSO6) {
        Adapter->LsoV2Ipv6 = TRUE;
    }

    // Create virtqueues (0=RX, 1=TX)
    status = ZvioNetQueueCreate(Adapter, 0, &Adapter->RxQueue);
    if (!NT_SUCCESS(status)) {
        ZvioNetDbgError("Failed to create RX queue");
        ZvioNetSetDeviceStatus(Adapter, VIRTIO_STATUS_FAILED);
        return status;
    }

    status = ZvioNetQueueCreate(Adapter, 1, &Adapter->TxQueue);
    if (!NT_SUCCESS(status)) {
        ZvioNetDbgError("Failed to create TX queue");
        ZvioNetQueueDestroy(Adapter->RxQueue);
        Adapter->RxQueue = NULL;
        ZvioNetSetDeviceStatus(Adapter, VIRTIO_STATUS_FAILED);
        return status;
    }

    // Create control queue if available
    if (driverFeatures & VIRTIO_NET_F_CTRL_VQ) {
        status = ZvioNetQueueCreate(Adapter, 2, &Adapter->CtrlQueue);
        if (!NT_SUCCESS(status)) {
            ZvioNetDbgPrint("Control queue not available");
            // Not fatal
        }
    }

    // Set DRIVER_OK
    deviceStatus = ZvioNetGetDeviceStatus(Adapter);
    ZvioNetSetDeviceStatus(Adapter, deviceStatus | VIRTIO_STATUS_DRIVER_OK);

    ZvioNetDbgPrint("VirtIO network device initialized");
    return STATUS_SUCCESS;
}

/*
 * ZvioNetDeviceReset - Reset the VirtIO device
 */
NTSTATUS
ZvioNetDeviceReset(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    UCHAR status;
    ULONG timeout = 1000;

    if (!Adapter->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WRITE_REGISTER_UCHAR(&Adapter->CommonCfg->DeviceStatus, 0);

    while (timeout--) {
        status = READ_REGISTER_UCHAR(&Adapter->CommonCfg->DeviceStatus);
        if (status == 0) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);
    }

    return STATUS_TIMEOUT;
}

/*
 * ZvioNetGetDeviceFeatures - Read device feature bits
 */
ULONGLONG
ZvioNetGetDeviceFeatures(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    ULONGLONG features = 0;
    ULONG featuresLow, featuresHigh;

    if (!Adapter->CommonCfg) {
        return 0;
    }

    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DeviceFeatureSel, 0);
    KeMemoryBarrier();
    featuresLow = READ_REGISTER_ULONG(&Adapter->CommonCfg->DeviceFeature);

    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DeviceFeatureSel, 1);
    KeMemoryBarrier();
    featuresHigh = READ_REGISTER_ULONG(&Adapter->CommonCfg->DeviceFeature);

    features = ((ULONGLONG)featuresHigh << 32) | featuresLow;
    Adapter->DeviceFeatures = features;

    return features;
}

/*
 * ZvioNetSetDriverFeatures - Set driver feature bits
 */
NTSTATUS
ZvioNetSetDriverFeatures(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ ULONGLONG Features
    )
{
    ULONG featuresLow, featuresHigh;

    if (!Adapter->CommonCfg) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    featuresLow = (ULONG)(Features & 0xFFFFFFFF);
    featuresHigh = (ULONG)(Features >> 32);

    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DriverFeatureSel, 0);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DriverFeature, featuresLow);

    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DriverFeatureSel, 1);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(&Adapter->CommonCfg->DriverFeature, featuresHigh);

    Adapter->DriverFeatures = Features;
    return STATUS_SUCCESS;
}

/*
 * ZvioNetGetDeviceStatus - Read device status
 */
UCHAR
ZvioNetGetDeviceStatus(
    _In_ PZVIONET_ADAPTER Adapter
    )
{
    if (!Adapter->CommonCfg) {
        return 0;
    }

    return READ_REGISTER_UCHAR(&Adapter->CommonCfg->DeviceStatus);
}

/*
 * ZvioNetSetDeviceStatus - Set device status
 */
VOID
ZvioNetSetDeviceStatus(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ UCHAR Status
    )
{
    if (!Adapter->CommonCfg) {
        return;
    }

    WRITE_REGISTER_UCHAR(&Adapter->CommonCfg->DeviceStatus, Status);
    KeMemoryBarrier();
}
