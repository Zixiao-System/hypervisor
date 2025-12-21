/*
 * Zixiao VirtIO Network Driver - NDIS Miniport
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Main NDIS 6.x miniport implementation.
 */

#include "public.h"

NDIS_HANDLE g_NdisMiniportDriverHandle = NULL;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, ZvioNetMiniportInitialize)
#pragma alloc_text(PAGE, ZvioNetMiniportHalt)
#endif

/*
 * DriverEntry - Main driver entry point
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_STATUS status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS miniportChars;

    ZvioNetDbgPrint("DriverEntry: Zixiao VirtIO Network Driver v1.0");

    NdisZeroMemory(&miniportChars, sizeof(miniportChars));

    miniportChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    miniportChars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    miniportChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;

    miniportChars.MajorNdisVersion = ZVIONET_NDIS_MAJOR_VERSION;
    miniportChars.MinorNdisVersion = ZVIONET_NDIS_MINOR_VERSION;
    miniportChars.MajorDriverVersion = 1;
    miniportChars.MinorDriverVersion = 0;

    miniportChars.InitializeHandlerEx = ZvioNetMiniportInitialize;
    miniportChars.HaltHandlerEx = ZvioNetMiniportHalt;
    miniportChars.PauseHandler = ZvioNetMiniportPause;
    miniportChars.RestartHandler = ZvioNetMiniportRestart;
    miniportChars.ShutdownHandlerEx = ZvioNetMiniportShutdown;
    miniportChars.OidRequestHandler = ZvioNetMiniportOidRequest;
    miniportChars.SendNetBufferListsHandler = ZvioNetMiniportSendNetBufferLists;
    miniportChars.ReturnNetBufferListsHandler = ZvioNetMiniportReturnNetBufferLists;
    miniportChars.CancelSendHandler = ZvioNetMiniportCancelSend;
    miniportChars.CheckForHangHandlerEx = ZvioNetMiniportCheckForHang;
    miniportChars.ResetHandlerEx = ZvioNetMiniportReset;
    miniportChars.DevicePnPEventNotifyHandler = ZvioNetMiniportDevicePnPEvent;
    miniportChars.UnloadHandler = ZvioNetMiniportUnload;

    status = NdisMRegisterMiniportDriver(
        DriverObject,
        RegistryPath,
        NULL,
        &miniportChars,
        &g_NdisMiniportDriverHandle
        );

    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("NdisMRegisterMiniportDriver failed: 0x%08X", status);
        return status;
    }

    ZvioNetDbgPrint("DriverEntry: Driver registered successfully");
    return NDIS_STATUS_SUCCESS;
}

/*
 * ZvioNetMiniportUnload - Driver unload handler
 */
VOID
ZvioNetMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

    ZvioNetDbgPrint("MiniportUnload");

    if (g_NdisMiniportDriverHandle) {
        NdisMDeregisterMiniportDriver(g_NdisMiniportDriverHandle);
        g_NdisMiniportDriverHandle = NULL;
    }
}

/*
 * ZvioNetMiniportInitialize - Initialize adapter
 */
NDIS_STATUS
ZvioNetMiniportInitialize(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
    )
{
    NDIS_STATUS status;
    PZVIONET_ADAPTER adapter = NULL;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES regAttributes;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES genAttributes;
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES offloadAttributes;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS interruptChars;
    PNDIS_RESOURCE_LIST resourceList;
    ULONG i;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDriverContext);

    ZvioNetDbgPrint("MiniportInitialize");

    //
    // Allocate adapter context
    //
    adapter = (PZVIONET_ADAPTER)NdisAllocateMemoryWithTagPriority(
        MiniportAdapterHandle,
        sizeof(ZVIONET_ADAPTER),
        ZVIONET_TAG,
        NormalPoolPriority
        );

    if (!adapter) {
        ZvioNetDbgError("Failed to allocate adapter context");
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(adapter, sizeof(ZVIONET_ADAPTER));
    adapter->AdapterHandle = MiniportAdapterHandle;
    adapter->Mtu = ZVIONET_DEFAULT_MTU;
    adapter->LinkSpeed = 10000000000ULL; // 10 Gbps default
    adapter->LinkUp = FALSE;
    adapter->MediaConnectState = MediaConnectStateDisconnected;
    InitializeListHead(&adapter->RxFreeList);
    NdisAllocateSpinLock(&adapter->RxFreeLock);

    //
    // Set registration attributes
    //
    NdisZeroMemory(&regAttributes, sizeof(regAttributes));
    regAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    regAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
    regAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
    regAttributes.MiniportAdapterContext = adapter;
    regAttributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
                                   NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    regAttributes.CheckForHangTimeInSeconds = 0;
    regAttributes.InterfaceType = NdisInterfacePci;

    status = NdisMSetMiniportAttributes(
        MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&regAttributes
        );

    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("NdisMSetMiniportAttributes (reg) failed: 0x%08X", status);
        goto Error;
    }

    //
    // Map PCI resources
    //
    resourceList = MiniportInitParameters->AllocatedResources;
    if (!resourceList) {
        ZvioNetDbgError("No allocated resources");
        status = NDIS_STATUS_RESOURCES;
        goto Error;
    }

    for (i = 0; i < resourceList->Count; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &resourceList->PartialDescriptors[i];

        if (desc->Type == CmResourceTypeMemory) {
            ULONG barIndex = 0;
            // Find next available BAR slot
            while (barIndex < 6 && adapter->BarVA[barIndex] != NULL) {
                barIndex++;
            }

            if (barIndex < 6) {
                adapter->BarPA[barIndex] = desc->u.Memory.Start;
                adapter->BarLength[barIndex] = desc->u.Memory.Length;

                status = NdisMMapIoSpace(
                    &adapter->BarVA[barIndex],
                    MiniportAdapterHandle,
                    desc->u.Memory.Start,
                    desc->u.Memory.Length
                    );

                if (status != NDIS_STATUS_SUCCESS) {
                    ZvioNetDbgError("Failed to map BAR%d", barIndex);
                    goto Error;
                }

                ZvioNetDbgPrint("BAR%d: PA=0x%llX Len=0x%X VA=0x%p",
                    barIndex, desc->u.Memory.Start.QuadPart,
                    desc->u.Memory.Length, adapter->BarVA[barIndex]);
            }
        }
    }

    //
    // Parse VirtIO capabilities
    //
    status = ZvioNetPciParseCapabilities(adapter);
    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("Failed to parse VirtIO capabilities");
        goto Error;
    }

    //
    // Initialize VirtIO device
    //
    status = ZvioNetDeviceInit(adapter);
    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("Failed to initialize VirtIO device");
        goto Error;
    }

    //
    // Read MAC address and configuration
    //
    if (adapter->DeviceCfg) {
        for (i = 0; i < 6; i++) {
            adapter->PermanentMacAddress[i] = READ_REGISTER_UCHAR(&adapter->DeviceCfg->Mac[i]);
            adapter->CurrentMacAddress[i] = adapter->PermanentMacAddress[i];
        }

        if (adapter->DriverFeatures & VIRTIO_NET_F_MTU) {
            adapter->Mtu = READ_REGISTER_USHORT(&adapter->DeviceCfg->Mtu);
        }

        if (adapter->DriverFeatures & VIRTIO_NET_F_STATUS) {
            USHORT netStatus = READ_REGISTER_USHORT(&adapter->DeviceCfg->Status);
            adapter->LinkUp = (netStatus & VIRTIO_NET_S_LINK_UP) != 0;
            adapter->MediaConnectState = adapter->LinkUp ?
                MediaConnectStateConnected : MediaConnectStateDisconnected;
        } else {
            adapter->LinkUp = TRUE;
            adapter->MediaConnectState = MediaConnectStateConnected;
        }

        ZvioNetDbgPrint("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
            adapter->CurrentMacAddress[0], adapter->CurrentMacAddress[1],
            adapter->CurrentMacAddress[2], adapter->CurrentMacAddress[3],
            adapter->CurrentMacAddress[4], adapter->CurrentMacAddress[5]);
        ZvioNetDbgPrint("MTU: %d, LinkUp: %d", adapter->Mtu, adapter->LinkUp);
    }

    //
    // Set general attributes
    //
    NdisZeroMemory(&genAttributes, sizeof(genAttributes));
    genAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    genAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    genAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    genAttributes.MediaType = NdisMedium802_3;
    genAttributes.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    genAttributes.MtuSize = adapter->Mtu;
    genAttributes.MaxXmitLinkSpeed = adapter->LinkSpeed;
    genAttributes.MaxRcvLinkSpeed = adapter->LinkSpeed;
    genAttributes.XmitLinkSpeed = adapter->LinkUp ? adapter->LinkSpeed : 0;
    genAttributes.RcvLinkSpeed = adapter->LinkUp ? adapter->LinkSpeed : 0;
    genAttributes.MediaConnectState = adapter->MediaConnectState;
    genAttributes.MediaDuplexState = MediaDuplexStateFull;
    genAttributes.LookaheadSize = adapter->Mtu;
    genAttributes.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                               NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                               NDIS_MAC_OPTION_NO_LOOPBACK;
    genAttributes.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                           NDIS_PACKET_TYPE_MULTICAST |
                                           NDIS_PACKET_TYPE_ALL_MULTICAST |
                                           NDIS_PACKET_TYPE_BROADCAST |
                                           NDIS_PACKET_TYPE_PROMISCUOUS;
    genAttributes.MaxMulticastListSize = ZVIONET_MAX_MULTICAST;
    genAttributes.MacAddressLength = 6;
    NdisMoveMemory(genAttributes.PermanentMacAddress, adapter->PermanentMacAddress, 6);
    NdisMoveMemory(genAttributes.CurrentMacAddress, adapter->CurrentMacAddress, 6);
    genAttributes.RecvScaleCapabilities = NULL;
    genAttributes.AccessType = NET_IF_ACCESS_BROADCAST;
    genAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    genAttributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    genAttributes.IfType = IF_TYPE_ETHERNET_CSMACD;
    genAttributes.IfConnectorPresent = TRUE;
    genAttributes.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                                        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_RCV |
                                        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_XMIT;
    genAttributes.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    genAttributes.AutoNegotiationFlags = NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;

    status = NdisMSetMiniportAttributes(
        MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&genAttributes
        );

    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("NdisMSetMiniportAttributes (gen) failed: 0x%08X", status);
        goto Error;
    }

    //
    // Register interrupt
    //
    NdisZeroMemory(&interruptChars, sizeof(interruptChars));
    interruptChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    interruptChars.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    interruptChars.Header.Revision = NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    interruptChars.InterruptHandler = ZvioNetInterruptHandler;
    interruptChars.InterruptDpcHandler = ZvioNetInterruptDpc;
    interruptChars.EnableInterruptHandler = ZvioNetEnableInterrupt;
    interruptChars.DisableInterruptHandler = ZvioNetDisableInterrupt;

    status = NdisMRegisterInterruptEx(
        MiniportAdapterHandle,
        adapter,
        &interruptChars,
        &adapter->InterruptHandle
        );

    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("NdisMRegisterInterruptEx failed: 0x%08X", status);
        goto Error;
    }

    //
    // Initialize RX buffers
    //
    status = ZvioNetInitRxBuffers(adapter);
    if (status != NDIS_STATUS_SUCCESS) {
        ZvioNetDbgError("Failed to initialize RX buffers");
        goto Error;
    }

    adapter->Running = TRUE;
    ZvioNetDbgPrint("Adapter initialized successfully");
    return NDIS_STATUS_SUCCESS;

Error:
    if (adapter) {
        ZvioNetMiniportHalt(adapter, NdisHaltDeviceInitializationFailed);
    }
    return status;
}

/*
 * ZvioNetMiniportHalt - Halt and cleanup adapter
 */
VOID
ZvioNetMiniportHalt(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;
    ULONG i;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(HaltAction);

    ZvioNetDbgPrint("MiniportHalt");

    if (!adapter) {
        return;
    }

    adapter->Running = FALSE;

    //
    // Deregister interrupt
    //
    if (adapter->InterruptHandle) {
        NdisMDeregisterInterruptEx(adapter->InterruptHandle);
        adapter->InterruptHandle = NULL;
    }

    //
    // Reset device
    //
    ZvioNetDeviceReset(adapter);

    //
    // Free RX buffers
    //
    ZvioNetFreeRxBuffers(adapter);

    //
    // Destroy virtqueues
    //
    if (adapter->RxQueue) {
        ZvioNetQueueDestroy(adapter->RxQueue);
        adapter->RxQueue = NULL;
    }
    if (adapter->TxQueue) {
        ZvioNetQueueDestroy(adapter->TxQueue);
        adapter->TxQueue = NULL;
    }
    if (adapter->CtrlQueue) {
        ZvioNetQueueDestroy(adapter->CtrlQueue);
        adapter->CtrlQueue = NULL;
    }

    //
    // Unmap BARs
    //
    for (i = 0; i < 6; i++) {
        if (adapter->BarVA[i]) {
            NdisMUnmapIoSpace(adapter->AdapterHandle, adapter->BarVA[i], adapter->BarLength[i]);
            adapter->BarVA[i] = NULL;
        }
    }

    //
    // Free NBL/NB pools
    //
    if (adapter->NblPool) {
        NdisFreeNetBufferListPool(adapter->NblPool);
        adapter->NblPool = NULL;
    }
    if (adapter->NbPool) {
        NdisFreeNetBufferPool(adapter->NbPool);
        adapter->NbPool = NULL;
    }

    NdisFreeSpinLock(&adapter->RxFreeLock);
    NdisFreeMemory(adapter, sizeof(ZVIONET_ADAPTER), 0);
}

/*
 * ZvioNetMiniportPause - Pause adapter
 */
NDIS_STATUS
ZvioNetMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    ZvioNetDbgPrint("MiniportPause");

    adapter->Paused = TRUE;
    return NDIS_STATUS_SUCCESS;
}

/*
 * ZvioNetMiniportRestart - Restart adapter
 */
NDIS_STATUS
ZvioNetMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    ZvioNetDbgPrint("MiniportRestart");

    adapter->Paused = FALSE;
    return NDIS_STATUS_SUCCESS;
}

/*
 * ZvioNetMiniportShutdown - Shutdown adapter
 */
VOID
ZvioNetMiniportShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    ZvioNetDbgPrint("MiniportShutdown");

    if (adapter) {
        adapter->Running = FALSE;
        ZvioNetDeviceReset(adapter);
    }
}

/*
 * ZvioNetMiniportSendNetBufferLists - Send packets
 */
VOID
ZvioNetMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    if (!adapter->Running || adapter->Paused) {
        PNET_BUFFER_LIST nbl = NetBufferLists;
        while (nbl) {
            NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_PAUSED;
            nbl = NET_BUFFER_LIST_NEXT_NBL(nbl);
        }
        NdisMSendNetBufferListsComplete(adapter->AdapterHandle, NetBufferLists, 0);
        return;
    }

    ZvioNetSendNetBufferLists(adapter, NetBufferLists);
}

/*
 * ZvioNetMiniportReturnNetBufferLists - Return received packets
 */
VOID
ZvioNetMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST nbl;

    UNREFERENCED_PARAMETER(ReturnFlags);

    //
    // Return NBLs to free list and replenish RX queue
    //
    for (nbl = NetBufferLists; nbl != NULL; nbl = NET_BUFFER_LIST_NEXT_NBL(nbl)) {
        PZVIONET_RX_BUFFER rxBuf = (PZVIONET_RX_BUFFER)NET_BUFFER_LIST_MINIPORT_RESERVED(nbl)[0];
        if (rxBuf) {
            NdisAcquireSpinLock(&adapter->RxFreeLock);
            InsertTailList(&adapter->RxFreeList, &rxBuf->Link);
            NdisReleaseSpinLock(&adapter->RxFreeLock);
        }
    }

    ZvioNetReplenishRx(adapter);
}

/*
 * ZvioNetMiniportCancelSend - Cancel pending sends
 */
VOID
ZvioNetMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);

    ZvioNetDbgPrint("MiniportCancelSend");
}

/*
 * ZvioNetMiniportCheckForHang - Check if adapter is hung
 */
BOOLEAN
ZvioNetMiniportCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    return FALSE;
}

/*
 * ZvioNetMiniportReset - Reset adapter
 */
NDIS_STATUS
ZvioNetMiniportReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;

    ZvioNetDbgPrint("MiniportReset");

    *AddressingReset = FALSE;

    ZvioNetDeviceReset(adapter);
    ZvioNetDeviceInit(adapter);

    return NDIS_STATUS_SUCCESS;
}

/*
 * ZvioNetMiniportDevicePnPEvent - Handle PnP events
 */
VOID
ZvioNetMiniportDevicePnPEvent(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    ZvioNetDbgPrint("DevicePnPEvent: %d", NetDevicePnPEvent->DevicePnPEvent);
}

/*
 * ZvioNetMiniportOidRequest - Handle OID requests
 */
NDIS_STATUS
ZvioNetMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest
    )
{
    PZVIONET_ADAPTER adapter = (PZVIONET_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    ULONG bytesNeeded = 0;
    ULONG bytesWritten = 0;
    ULONG bytesRead = 0;
    PVOID infoBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG infoBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;

    switch (OidRequest->RequestType) {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        switch (OidRequest->DATA.QUERY_INFORMATION.Oid) {
        case OID_GEN_SUPPORTED_LIST:
            {
                static const NDIS_OID supportedOids[] = {
                    OID_GEN_SUPPORTED_LIST,
                    OID_GEN_HARDWARE_STATUS,
                    OID_GEN_MEDIA_SUPPORTED,
                    OID_GEN_MEDIA_IN_USE,
                    OID_GEN_MAXIMUM_FRAME_SIZE,
                    OID_GEN_LINK_SPEED,
                    OID_GEN_TRANSMIT_BUFFER_SPACE,
                    OID_GEN_RECEIVE_BUFFER_SPACE,
                    OID_GEN_VENDOR_ID,
                    OID_GEN_VENDOR_DESCRIPTION,
                    OID_GEN_CURRENT_PACKET_FILTER,
                    OID_GEN_MAXIMUM_TOTAL_SIZE,
                    OID_GEN_MAC_OPTIONS,
                    OID_GEN_MEDIA_CONNECT_STATUS,
                    OID_802_3_PERMANENT_ADDRESS,
                    OID_802_3_CURRENT_ADDRESS,
                    OID_802_3_MULTICAST_LIST,
                    OID_802_3_MAXIMUM_LIST_SIZE,
                };
                bytesNeeded = sizeof(supportedOids);
                if (infoBufferLength >= bytesNeeded) {
                    NdisMoveMemory(infoBuffer, supportedOids, sizeof(supportedOids));
                    bytesWritten = sizeof(supportedOids);
                } else {
                    status = NDIS_STATUS_BUFFER_TOO_SHORT;
                }
            }
            break;

        case OID_GEN_HARDWARE_STATUS:
            bytesNeeded = sizeof(NDIS_HARDWARE_STATUS);
            if (infoBufferLength >= bytesNeeded) {
                *(PNDIS_HARDWARE_STATUS)infoBuffer = NdisHardwareStatusReady;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            bytesNeeded = sizeof(NDIS_MEDIUM);
            if (infoBufferLength >= bytesNeeded) {
                *(PNDIS_MEDIUM)infoBuffer = NdisMedium802_3;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            bytesNeeded = sizeof(ULONG);
            if (infoBufferLength >= bytesNeeded) {
                *(PULONG)infoBuffer = adapter->Mtu;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_LINK_SPEED:
            bytesNeeded = sizeof(ULONG);
            if (infoBufferLength >= bytesNeeded) {
                *(PULONG)infoBuffer = (ULONG)(adapter->LinkSpeed / 100);
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_VENDOR_ID:
            bytesNeeded = sizeof(ULONG);
            if (infoBufferLength >= bytesNeeded) {
                *(PULONG)infoBuffer = 0x001AF4; // VirtIO vendor ID
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            {
                static const char vendorDesc[] = "Zixiao VirtIO Network Adapter";
                bytesNeeded = sizeof(vendorDesc);
                if (infoBufferLength >= bytesNeeded) {
                    NdisMoveMemory(infoBuffer, vendorDesc, sizeof(vendorDesc));
                    bytesWritten = sizeof(vendorDesc);
                } else {
                    status = NDIS_STATUS_BUFFER_TOO_SHORT;
                }
            }
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            bytesNeeded = sizeof(ULONG);
            if (infoBufferLength >= bytesNeeded) {
                *(PULONG)infoBuffer = adapter->PacketFilter;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            bytesNeeded = sizeof(NDIS_MEDIA_STATE);
            if (infoBufferLength >= bytesNeeded) {
                *(PNDIS_MEDIA_STATE)infoBuffer = adapter->LinkUp ?
                    NdisMediaStateConnected : NdisMediaStateDisconnected;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            bytesNeeded = 6;
            if (infoBufferLength >= bytesNeeded) {
                NdisMoveMemory(infoBuffer, adapter->PermanentMacAddress, 6);
                bytesWritten = 6;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_802_3_CURRENT_ADDRESS:
            bytesNeeded = 6;
            if (infoBufferLength >= bytesNeeded) {
                NdisMoveMemory(infoBuffer, adapter->CurrentMacAddress, 6);
                bytesWritten = 6;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            bytesNeeded = sizeof(ULONG);
            if (infoBufferLength >= bytesNeeded) {
                *(PULONG)infoBuffer = ZVIONET_MAX_MULTICAST;
                bytesWritten = bytesNeeded;
            } else {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            break;

        default:
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }
        OidRequest->DATA.QUERY_INFORMATION.BytesWritten = bytesWritten;
        OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = bytesNeeded;
        break;

    case NdisRequestSetInformation:
        infoBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
        infoBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;

        switch (OidRequest->DATA.SET_INFORMATION.Oid) {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (infoBufferLength >= sizeof(ULONG)) {
                adapter->PacketFilter = *(PULONG)infoBuffer;
                bytesRead = sizeof(ULONG);
            } else {
                status = NDIS_STATUS_INVALID_LENGTH;
            }
            break;

        case OID_802_3_MULTICAST_LIST:
            if (infoBufferLength % 6 == 0 &&
                infoBufferLength / 6 <= ZVIONET_MAX_MULTICAST) {
                adapter->MulticastCount = (ULONG)(infoBufferLength / 6);
                NdisMoveMemory(adapter->MulticastList, infoBuffer, infoBufferLength);
                bytesRead = infoBufferLength;
            } else {
                status = NDIS_STATUS_INVALID_DATA;
            }
            break;

        default:
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }
        OidRequest->DATA.SET_INFORMATION.BytesRead = bytesRead;
        break;

    default:
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    return status;
}
