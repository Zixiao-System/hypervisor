/*
 * Zixiao VirtIO Block Driver - Block I/O Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Block read/write and IOCTL handling.
 */

#include "public.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ZvioBlkEvtIoDeviceControl)
#endif

/*
 * ZvioBlkEvtIoRead - Handle read requests
 */
VOID
ZvioBlkEvtIoRead(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length
    )
{
    NTSTATUS status;
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    WDF_REQUEST_PARAMETERS params;
    ULONGLONG byteOffset;
    ULONGLONG sector;
    PMDL mdl;

    deviceContext = ZvioBlkGetDeviceContext(WdfIoQueueGetDevice(Queue));

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    byteOffset = params.Parameters.Read.DeviceOffset;
    sector = byteOffset / deviceContext->SectorSize;

    ZvioBlkDbgPrint("Read: offset=0x%llX length=0x%zX sector=%llu",
        byteOffset, Length, sector);

    //
    // Validate request
    //
    if (Length == 0) {
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    if ((byteOffset % deviceContext->SectorSize) != 0 ||
        (Length % deviceContext->SectorSize) != 0) {
        ZvioBlkDbgError("Read: Unaligned request");
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    if (sector + (Length / deviceContext->SectorSize) > deviceContext->Capacity) {
        ZvioBlkDbgError("Read: Request beyond capacity");
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    //
    // Get MDL for the output buffer
    //
    status = WdfRequestRetrieveOutputWdmMdl(Request, &mdl);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Read: Failed to get MDL: 0x%08X", status);
        WdfRequestComplete(Request, status);
        return;
    }

    //
    // Submit the read request
    //
    status = ZvioBlkSubmitRequest(
        deviceContext,
        Request,
        VIRTIO_BLK_T_IN,
        sector,
        mdl,
        (ULONG)Length
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Read: Failed to submit: 0x%08X", status);
        WdfRequestComplete(Request, status);
    }

    // Request will be completed asynchronously
}

/*
 * ZvioBlkEvtIoWrite - Handle write requests
 */
VOID
ZvioBlkEvtIoWrite(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length
    )
{
    NTSTATUS status;
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    WDF_REQUEST_PARAMETERS params;
    ULONGLONG byteOffset;
    ULONGLONG sector;
    PMDL mdl;

    deviceContext = ZvioBlkGetDeviceContext(WdfIoQueueGetDevice(Queue));

    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    byteOffset = params.Parameters.Write.DeviceOffset;
    sector = byteOffset / deviceContext->SectorSize;

    ZvioBlkDbgPrint("Write: offset=0x%llX length=0x%zX sector=%llu",
        byteOffset, Length, sector);

    //
    // Check if device is read-only
    //
    if (deviceContext->ReadOnly) {
        ZvioBlkDbgError("Write: Device is read-only");
        WdfRequestComplete(Request, STATUS_MEDIA_WRITE_PROTECTED);
        return;
    }

    //
    // Validate request
    //
    if (Length == 0) {
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    if ((byteOffset % deviceContext->SectorSize) != 0 ||
        (Length % deviceContext->SectorSize) != 0) {
        ZvioBlkDbgError("Write: Unaligned request");
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    if (sector + (Length / deviceContext->SectorSize) > deviceContext->Capacity) {
        ZvioBlkDbgError("Write: Request beyond capacity");
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    //
    // Get MDL for the input buffer
    //
    status = WdfRequestRetrieveInputWdmMdl(Request, &mdl);
    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Write: Failed to get MDL: 0x%08X", status);
        WdfRequestComplete(Request, status);
        return;
    }

    //
    // Submit the write request
    //
    status = ZvioBlkSubmitRequest(
        deviceContext,
        Request,
        VIRTIO_BLK_T_OUT,
        sector,
        mdl,
        (ULONG)Length
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlkDbgError("Write: Failed to submit: 0x%08X", status);
        WdfRequestComplete(Request, status);
    }

    // Request will be completed asynchronously
}

/*
 * ZvioBlkEvtIoDeviceControl - Handle IOCTL requests
 */
VOID
ZvioBlkEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    PZVIOBLK_DEVICE_CONTEXT deviceContext;
    PVOID outputBuffer;
    size_t bytesReturned = 0;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(InputBufferLength);

    deviceContext = ZvioBlkGetDeviceContext(WdfIoQueueGetDevice(Queue));

    ZvioBlkDbgPrint("IOCTL: code=0x%08X", IoControlCode);

    switch (IoControlCode) {

    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
        {
            DISK_GEOMETRY geometry;

            if (OutputBufferLength < sizeof(DISK_GEOMETRY)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(DISK_GEOMETRY), &outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }

            RtlZeroMemory(&geometry, sizeof(geometry));
            geometry.MediaType = FixedMedia;
            geometry.BytesPerSector = deviceContext->SectorSize;
            geometry.SectorsPerTrack = 63;
            geometry.TracksPerCylinder = 255;
            geometry.Cylinders.QuadPart = deviceContext->Capacity / (63 * 255);

            RtlCopyMemory(outputBuffer, &geometry, sizeof(DISK_GEOMETRY));
            bytesReturned = sizeof(DISK_GEOMETRY);

            if (IoControlCode == IOCTL_DISK_GET_DRIVE_GEOMETRY_EX &&
                OutputBufferLength >= sizeof(DISK_GEOMETRY_EX)) {
                PDISK_GEOMETRY_EX geometryEx = (PDISK_GEOMETRY_EX)outputBuffer;
                geometryEx->DiskSize.QuadPart = deviceContext->Capacity * deviceContext->SectorSize;
                bytesReturned = FIELD_OFFSET(DISK_GEOMETRY_EX, Data);
            }
        }
        break;

    case IOCTL_DISK_GET_LENGTH_INFO:
        {
            PGET_LENGTH_INFORMATION lengthInfo;

            if (OutputBufferLength < sizeof(GET_LENGTH_INFORMATION)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(GET_LENGTH_INFORMATION), &outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }

            lengthInfo = (PGET_LENGTH_INFORMATION)outputBuffer;
            lengthInfo->Length.QuadPart = deviceContext->Capacity * deviceContext->SectorSize;
            bytesReturned = sizeof(GET_LENGTH_INFORMATION);
        }
        break;

    case IOCTL_DISK_IS_WRITABLE:
        if (deviceContext->ReadOnly) {
            status = STATUS_MEDIA_WRITE_PROTECTED;
        }
        break;

    case IOCTL_DISK_MEDIA_REMOVAL:
        // Not removable
        status = STATUS_SUCCESS;
        break;

    case IOCTL_STORAGE_GET_HOTPLUG_INFO:
        {
            PSTORAGE_HOTPLUG_INFO hotplugInfo;

            if (OutputBufferLength < sizeof(STORAGE_HOTPLUG_INFO)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(STORAGE_HOTPLUG_INFO), &outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }

            hotplugInfo = (PSTORAGE_HOTPLUG_INFO)outputBuffer;
            RtlZeroMemory(hotplugInfo, sizeof(STORAGE_HOTPLUG_INFO));
            hotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
            hotplugInfo->MediaRemovable = FALSE;
            hotplugInfo->MediaHotplug = FALSE;
            hotplugInfo->DeviceHotplug = FALSE;
            hotplugInfo->WriteCacheEnableOverride = FALSE;
            bytesReturned = sizeof(STORAGE_HOTPLUG_INFO);
        }
        break;

    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        {
            PSTORAGE_DEVICE_NUMBER deviceNumber;

            if (OutputBufferLength < sizeof(STORAGE_DEVICE_NUMBER)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(STORAGE_DEVICE_NUMBER), &outputBuffer, NULL);
            if (!NT_SUCCESS(status)) {
                break;
            }

            deviceNumber = (PSTORAGE_DEVICE_NUMBER)outputBuffer;
            deviceNumber->DeviceType = FILE_DEVICE_DISK;
            deviceNumber->DeviceNumber = 0; // Would need to track this globally
            deviceNumber->PartitionNumber = 0;
            bytesReturned = sizeof(STORAGE_DEVICE_NUMBER);
        }
        break;

    case IOCTL_STORAGE_CHECK_VERIFY:
    case IOCTL_STORAGE_CHECK_VERIFY2:
        // Always verified for virtual disks
        status = STATUS_SUCCESS;
        break;

    case IOCTL_DISK_CHECK_VERIFY:
        status = STATUS_SUCCESS;
        break;

    case IOCTL_DISK_FLUSH_CACHE:
    case IOCTL_STORAGE_FLUSH_CACHE:
        if (deviceContext->SupportsFlush && !deviceContext->ReadOnly) {
            status = ZvioBlkSubmitRequest(
                deviceContext,
                Request,
                VIRTIO_BLK_T_FLUSH,
                0,
                NULL,
                0
                );
            if (NT_SUCCESS(status)) {
                // Request will be completed asynchronously
                return;
            }
        }
        break;

    default:
        ZvioBlkDbgPrint("IOCTL: Unsupported code 0x%08X", IoControlCode);
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

/*
 * ZvioBlkSubmitRequest - Submit a block request to the virtqueue
 */
NTSTATUS
ZvioBlkSubmitRequest(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ ULONG Type,
    _In_ ULONGLONG Sector,
    _In_opt_ PMDL Mdl,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    PZVIOBLK_REQUEST blkRequest;
    WDF_OBJECT_ATTRIBUTES attributes;
    SCATTER_GATHER_LIST sgList;
    SCATTER_GATHER_ELEMENT sgElements[3];  // Header + Data + Status
    ULONG outCount = 1;  // Header is always out
    ULONG inCount = 1;   // Status is always in
    ULONG sgCount = 2;   // Header + Status

    if (DeviceContext->NumQueues == 0 || DeviceContext->Queues == NULL ||
        DeviceContext->Queues[0] == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    //
    // Create request context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ZVIOBLK_REQUEST);
    status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&blkRequest);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(blkRequest, sizeof(ZVIOBLK_REQUEST));
    blkRequest->Request = Request;
    blkRequest->Queue = DeviceContext->Queues[0];
    blkRequest->DataLength = Length;

    //
    // Allocate header buffer
    //
    WDF_COMMON_BUFFER_CONFIG bufferConfig;
    WDF_COMMON_BUFFER_CONFIG_INIT(&bufferConfig, sizeof(ULONGLONG));

    status = WdfCommonBufferCreate(
        DeviceContext->DmaEnabler,
        sizeof(VIRTIO_BLK_REQ_HDR),
        &bufferConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &blkRequest->HeaderBuffer
        );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    blkRequest->Header = (PVIRTIO_BLK_REQ_HDR)WdfCommonBufferGetAlignedVirtualAddress(blkRequest->HeaderBuffer);
    blkRequest->HeaderPhys = WdfCommonBufferGetAlignedLogicalAddress(blkRequest->HeaderBuffer);

    blkRequest->Header->Type = Type;
    blkRequest->Header->Reserved = 0;
    blkRequest->Header->Sector = Sector;

    //
    // Allocate status buffer
    //
    status = WdfCommonBufferCreate(
        DeviceContext->DmaEnabler,
        sizeof(UCHAR),
        &bufferConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &blkRequest->StatusBuffer
        );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(blkRequest->HeaderBuffer);
        return status;
    }

    blkRequest->Status = (PUCHAR)WdfCommonBufferGetAlignedVirtualAddress(blkRequest->StatusBuffer);
    blkRequest->StatusPhys = WdfCommonBufferGetAlignedLogicalAddress(blkRequest->StatusBuffer);
    *blkRequest->Status = 0xFF;  // Initialize to invalid

    //
    // Build scatter-gather list
    //
    sgList.NumberOfElements = 0;
    sgList.Reserved = 0;

    // Header (device-readable)
    sgElements[0].Address = blkRequest->HeaderPhys;
    sgElements[0].Length = sizeof(VIRTIO_BLK_REQ_HDR);
    sgList.NumberOfElements++;

    // Data buffer if present
    if (Mdl && Length > 0) {
        PHYSICAL_ADDRESS dataPhys = MmGetPhysicalAddress(MmGetMdlVirtualAddress(Mdl));
        sgElements[sgList.NumberOfElements].Address = dataPhys;
        sgElements[sgList.NumberOfElements].Length = Length;
        sgList.NumberOfElements++;
        sgCount++;

        if (Type == VIRTIO_BLK_T_OUT) {
            outCount++;  // Write: data is device-readable
        } else {
            inCount++;   // Read: data is device-writable
        }
    }

    // Status (device-writable)
    sgElements[sgList.NumberOfElements].Address = blkRequest->StatusPhys;
    sgElements[sgList.NumberOfElements].Length = sizeof(UCHAR);
    sgList.NumberOfElements++;

    sgList.Elements = sgElements;

    //
    // Submit to virtqueue
    //
    status = ZvioBlkQueueAddBuffers(
        blkRequest->Queue,
        &sgList,
        outCount,
        inCount,
        blkRequest,
        &blkRequest->HeadDescIdx
        );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(blkRequest->StatusBuffer);
        WdfObjectDelete(blkRequest->HeaderBuffer);
        return status;
    }

    //
    // Notify device
    //
    ZvioBlkQueueKick(blkRequest->Queue);

    return STATUS_SUCCESS;
}

/*
 * ZvioBlkCompleteRequest - Complete a block request
 */
VOID
ZvioBlkCompleteRequest(
    _In_ PZVIOBLK_REQUEST BlkRequest,
    _In_ UCHAR Status,
    _In_ ULONG BytesTransferred
    )
{
    NTSTATUS ntStatus;

    switch (Status) {
    case VIRTIO_BLK_S_OK:
        ntStatus = STATUS_SUCCESS;
        break;
    case VIRTIO_BLK_S_IOERR:
        ntStatus = STATUS_IO_DEVICE_ERROR;
        BytesTransferred = 0;
        break;
    case VIRTIO_BLK_S_UNSUPP:
        ntStatus = STATUS_NOT_SUPPORTED;
        BytesTransferred = 0;
        break;
    default:
        ntStatus = STATUS_UNSUCCESSFUL;
        BytesTransferred = 0;
        break;
    }

    //
    // Free DMA buffers
    //
    if (BlkRequest->HeaderBuffer) {
        WdfObjectDelete(BlkRequest->HeaderBuffer);
    }
    if (BlkRequest->StatusBuffer) {
        WdfObjectDelete(BlkRequest->StatusBuffer);
    }

    //
    // Complete the request
    //
    WdfRequestCompleteWithInformation(BlkRequest->Request, ntStatus, BytesTransferred);
}
