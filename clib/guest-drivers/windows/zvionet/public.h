/*
 * Zixiao VirtIO Network Driver - Public Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO network device structures and definitions.
 */

#pragma once

#include <ndis.h>
#include <wdf.h>

//
// Debug macros
//
#define ZVIONET_TAG     'teNZ'  // ZNet

#if DBG
#define ZvioNetDbgPrint(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "ZVIONET: " _fmt "\n", ##__VA_ARGS__)
#define ZvioNetDbgError(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "ZVIONET ERROR: " _fmt "\n", ##__VA_ARGS__)
#else
#define ZvioNetDbgPrint(_fmt, ...)
#define ZvioNetDbgError(_fmt, ...)
#endif

//
// Driver version
//
#define ZVIONET_NDIS_MAJOR_VERSION  6
#define ZVIONET_NDIS_MINOR_VERSION  50

//
// Hardware limits
//
#define ZVIONET_MAX_MTU             65535
#define ZVIONET_MIN_MTU             68
#define ZVIONET_DEFAULT_MTU         1500
#define ZVIONET_MAX_TX_QUEUES       8
#define ZVIONET_MAX_RX_QUEUES       8
#define ZVIONET_MAX_QUEUE_SIZE      256
#define ZVIONET_RX_BUFFER_SIZE      2048
#define ZVIONET_MAX_MULTICAST       64

//
// VirtIO Network Feature Bits
//
#define VIRTIO_NET_F_CSUM           (1ULL << 0)   // Host handles checksum
#define VIRTIO_NET_F_GUEST_CSUM     (1ULL << 1)   // Guest handles checksum
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS (1ULL << 2)
#define VIRTIO_NET_F_MTU            (1ULL << 3)   // MTU provided
#define VIRTIO_NET_F_MAC            (1ULL << 5)   // MAC address provided
#define VIRTIO_NET_F_GSO            (1ULL << 6)   // Deprecated
#define VIRTIO_NET_F_GUEST_TSO4     (1ULL << 7)   // Guest can receive TSO4
#define VIRTIO_NET_F_GUEST_TSO6     (1ULL << 8)   // Guest can receive TSO6
#define VIRTIO_NET_F_GUEST_ECN      (1ULL << 9)   // Guest can receive ECN
#define VIRTIO_NET_F_GUEST_UFO      (1ULL << 10)  // Guest can receive UFO
#define VIRTIO_NET_F_HOST_TSO4      (1ULL << 11)  // Host can handle TSO4
#define VIRTIO_NET_F_HOST_TSO6      (1ULL << 12)  // Host can handle TSO6
#define VIRTIO_NET_F_HOST_ECN       (1ULL << 13)  // Host can handle ECN
#define VIRTIO_NET_F_HOST_UFO       (1ULL << 14)  // Host can handle UFO
#define VIRTIO_NET_F_MRG_RXBUF      (1ULL << 15)  // Merge rx buffers
#define VIRTIO_NET_F_STATUS         (1ULL << 16)  // Link status
#define VIRTIO_NET_F_CTRL_VQ        (1ULL << 17)  // Control virtqueue
#define VIRTIO_NET_F_CTRL_RX        (1ULL << 18)  // Control RX mode
#define VIRTIO_NET_F_CTRL_VLAN      (1ULL << 19)  // Control VLAN filtering
#define VIRTIO_NET_F_CTRL_RX_EXTRA  (1ULL << 20)
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1ULL << 21)  // Guest can send announcements
#define VIRTIO_NET_F_MQ             (1ULL << 22)  // Multi-queue
#define VIRTIO_NET_F_CTRL_MAC_ADDR  (1ULL << 23)  // Set MAC via control
#define VIRTIO_NET_F_HOST_USO       (1ULL << 56)
#define VIRTIO_NET_F_HASH_REPORT    (1ULL << 57)
#define VIRTIO_NET_F_GUEST_HDRLEN   (1ULL << 59)
#define VIRTIO_NET_F_RSS            (1ULL << 60)
#define VIRTIO_NET_F_RSC_EXT        (1ULL << 61)
#define VIRTIO_NET_F_STANDBY        (1ULL << 62)
#define VIRTIO_NET_F_SPEED_DUPLEX   (1ULL << 63)

// Common VirtIO features
#define VIRTIO_F_VERSION_1          (1ULL << 32)
#define VIRTIO_F_RING_INDIRECT_DESC (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX     (1ULL << 29)

//
// VirtIO Network Header (prepended to packets)
//
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1   // Checksum required
#define VIRTIO_NET_HDR_F_DATA_VALID 2   // Checksum verified
#define VIRTIO_NET_HDR_F_RSC_INFO   4   // RSC info valid

#define VIRTIO_NET_HDR_GSO_NONE     0
#define VIRTIO_NET_HDR_GSO_TCPV4    1
#define VIRTIO_NET_HDR_GSO_UDP      3
#define VIRTIO_NET_HDR_GSO_TCPV6    4
#define VIRTIO_NET_HDR_GSO_UDP_L4   5
#define VIRTIO_NET_HDR_GSO_ECN      0x80

#pragma pack(push, 1)
typedef struct _VIRTIO_NET_HDR {
    UCHAR       Flags;
    UCHAR       GsoType;
    USHORT      HdrLen;         // Ethernet + IP + TCP header length
    USHORT      GsoSize;        // Max segment size
    USHORT      CsumStart;      // Position to start checksumming
    USHORT      CsumOffset;     // Offset after CsumStart to store checksum
    USHORT      NumBuffers;     // Number of merged rx buffers (if MRG_RXBUF)
} VIRTIO_NET_HDR, *PVIRTIO_NET_HDR;
#pragma pack(pop)

//
// VirtIO Network Device Configuration
//
#pragma pack(push, 1)
typedef struct _VIRTIO_NET_CONFIG {
    UCHAR       Mac[6];         // MAC address
    USHORT      Status;         // Link status
    USHORT      MaxVirtqueuePairs; // Max TX/RX queue pairs
    USHORT      Mtu;            // MTU (if VIRTIO_NET_F_MTU)
    ULONG       Speed;          // Link speed (if VIRTIO_NET_F_SPEED_DUPLEX)
    UCHAR       Duplex;         // Duplex mode
    UCHAR       RssMaxKeySize;
    USHORT      RssMaxIndirectionTableLength;
    ULONG       SupportedHashTypes;
} VIRTIO_NET_CONFIG, *PVIRTIO_NET_CONFIG;
#pragma pack(pop)

#define VIRTIO_NET_S_LINK_UP        1
#define VIRTIO_NET_S_ANNOUNCE       2

//
// VirtIO Control Commands
//
#define VIRTIO_NET_CTRL_RX          0
#define VIRTIO_NET_CTRL_RX_PROMISC      0
#define VIRTIO_NET_CTRL_RX_ALLMULTI     1
#define VIRTIO_NET_CTRL_RX_ALLUNI       2
#define VIRTIO_NET_CTRL_RX_NOMULTI      3
#define VIRTIO_NET_CTRL_RX_NOUNI        4
#define VIRTIO_NET_CTRL_RX_NOBCAST      5

#define VIRTIO_NET_CTRL_MAC         1
#define VIRTIO_NET_CTRL_MAC_TABLE_SET   0
#define VIRTIO_NET_CTRL_MAC_ADDR_SET    1

#define VIRTIO_NET_CTRL_VLAN        2
#define VIRTIO_NET_CTRL_VLAN_ADD        0
#define VIRTIO_NET_CTRL_VLAN_DEL        1

#define VIRTIO_NET_CTRL_MQ          4
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET     0
#define VIRTIO_NET_CTRL_MQ_RSS_CONFIG       1
#define VIRTIO_NET_CTRL_MQ_HASH_CONFIG      2

#pragma pack(push, 1)
typedef struct _VIRTIO_NET_CTRL_HDR {
    UCHAR       Class;
    UCHAR       Cmd;
} VIRTIO_NET_CTRL_HDR, *PVIRTIO_NET_CTRL_HDR;
#pragma pack(pop)

#define VIRTIO_NET_OK               0
#define VIRTIO_NET_ERR              1

//
// Virtqueue Ring Structures
//
#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2
#define VRING_DESC_F_INDIRECT   4

#pragma pack(push, 1)
typedef struct _VRING_DESC {
    ULONGLONG   Addr;
    ULONG       Len;
    USHORT      Flags;
    USHORT      Next;
} VRING_DESC, *PVRING_DESC;

typedef struct _VRING_AVAIL {
    USHORT      Flags;
    USHORT      Idx;
    USHORT      Ring[1];
} VRING_AVAIL, *PVRING_AVAIL;

typedef struct _VRING_USED_ELEM {
    ULONG       Id;
    ULONG       Len;
} VRING_USED_ELEM, *PVRING_USED_ELEM;

typedef struct _VRING_USED {
    USHORT          Flags;
    USHORT          Idx;
    VRING_USED_ELEM Ring[1];
} VRING_USED, *PVRING_USED;
#pragma pack(pop)

#define VRING_AVAIL_F_NO_INTERRUPT  1
#define VRING_USED_F_NO_NOTIFY      1

//
// VirtIO PCI Common Configuration
//
#pragma pack(push, 1)
typedef struct _VIRTIO_PCI_COMMON_CFG {
    ULONG       DeviceFeatureSel;
    ULONG       DeviceFeature;
    ULONG       DriverFeatureSel;
    ULONG       DriverFeature;
    USHORT      MsixConfig;
    USHORT      NumQueues;
    UCHAR       DeviceStatus;
    UCHAR       ConfigGeneration;
    USHORT      QueueSel;
    USHORT      QueueSize;
    USHORT      QueueMsixVector;
    USHORT      QueueEnable;
    USHORT      QueueNotifyOff;
    ULONGLONG   QueueDesc;
    ULONGLONG   QueueDriver;
    ULONGLONG   QueueDevice;
    USHORT      QueueNotifyData;
    USHORT      QueueReset;
} VIRTIO_PCI_COMMON_CFG, *PVIRTIO_PCI_COMMON_CFG;
#pragma pack(pop)

#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_FAILED        128

//
// Forward declarations
//
typedef struct _ZVIONET_ADAPTER ZVIONET_ADAPTER, *PZVIONET_ADAPTER;
typedef struct _ZVIONET_VIRTQUEUE ZVIONET_VIRTQUEUE, *PZVIONET_VIRTQUEUE;

//
// Receive Buffer
//
typedef struct _ZVIONET_RX_BUFFER {
    LIST_ENTRY          Link;
    PZVIONET_ADAPTER    Adapter;
    PNET_BUFFER_LIST    Nbl;
    PMDL                Mdl;
    PVOID               Buffer;
    PHYSICAL_ADDRESS    BufferPA;
    ULONG               BufferSize;
    NDIS_HANDLE         PoolHandle;
} ZVIONET_RX_BUFFER, *PZVIONET_RX_BUFFER;

//
// Transmit Context
//
typedef struct _ZVIONET_TX_CONTEXT {
    PNET_BUFFER_LIST    Nbl;
    PNET_BUFFER         Nb;
    PZVIONET_ADAPTER    Adapter;
    ULONG               DescCount;
    USHORT              HeadIdx;
} ZVIONET_TX_CONTEXT, *PZVIONET_TX_CONTEXT;

//
// Virtqueue Context
//
struct _ZVIONET_VIRTQUEUE {
    USHORT                  Index;
    USHORT                  Size;
    USHORT                  NumFree;
    USHORT                  FreeHead;
    USHORT                  LastUsedIdx;

    PVRING_DESC             Desc;
    PHYSICAL_ADDRESS        DescPhys;

    PVRING_AVAIL            Avail;
    PHYSICAL_ADDRESS        AvailPhys;

    PVRING_USED             Used;
    PHYSICAL_ADDRESS        UsedPhys;

    PVOID                   *DescData;

    PVOID                   RingBuffer;
    PHYSICAL_ADDRESS        RingBufferPA;
    SIZE_T                  RingBufferSize;

    NDIS_SPIN_LOCK          Lock;

    PUCHAR                  NotifyAddr;

    PZVIONET_ADAPTER        Adapter;
};

//
// Adapter Context
//
struct _ZVIONET_ADAPTER {
    NDIS_HANDLE             AdapterHandle;
    NDIS_HANDLE             MiniportDmaHandle;

    // Device identification
    WDFDEVICE               WdfDevice;

    // PCI resources
    PVOID                   BarVA[6];
    PHYSICAL_ADDRESS        BarPA[6];
    ULONG                   BarLength[6];

    // VirtIO configuration
    PVIRTIO_PCI_COMMON_CFG  CommonCfg;
    PVOID                   NotifyBase;
    ULONG                   NotifyOffMultiplier;
    PUCHAR                  IsrStatus;
    PVIRTIO_NET_CONFIG      DeviceCfg;

    // Feature negotiation
    ULONGLONG               DeviceFeatures;
    ULONGLONG               DriverFeatures;

    // Virtqueues (0=RX, 1=TX, 2=Control if available)
    PZVIONET_VIRTQUEUE      RxQueue;
    PZVIONET_VIRTQUEUE      TxQueue;
    PZVIONET_VIRTQUEUE      CtrlQueue;

    // Interrupt
    NDIS_HANDLE             InterruptHandle;
    BOOLEAN                 UseMsix;

    // Network configuration
    UCHAR                   CurrentMacAddress[6];
    UCHAR                   PermanentMacAddress[6];
    ULONG                   Mtu;
    ULONG                   LinkSpeed;
    BOOLEAN                 LinkUp;
    NDIS_MEDIA_CONNECT_STATE MediaConnectState;

    // Packet filter
    ULONG                   PacketFilter;
    ULONG                   MulticastCount;
    UCHAR                   MulticastList[ZVIONET_MAX_MULTICAST][6];

    // Offload capabilities
    BOOLEAN                 TxChecksumOffload;
    BOOLEAN                 RxChecksumOffload;
    BOOLEAN                 LsoV1;
    BOOLEAN                 LsoV2Ipv4;
    BOOLEAN                 LsoV2Ipv6;

    // Statistics
    ULONG64                 TxPackets;
    ULONG64                 TxBytes;
    ULONG64                 RxPackets;
    ULONG64                 RxBytes;
    ULONG64                 TxErrors;
    ULONG64                 RxErrors;

    // Memory pools
    NDIS_HANDLE             NblPool;
    NDIS_HANDLE             NbPool;

    // RX buffer list
    LIST_ENTRY              RxFreeList;
    NDIS_SPIN_LOCK          RxFreeLock;
    ULONG                   RxBufferCount;

    // State
    BOOLEAN                 Running;
    BOOLEAN                 Paused;
};

//
// Function Prototypes
//

// driver.c - NDIS Miniport handlers
MINIPORT_INITIALIZE ZvioNetMiniportInitialize;
MINIPORT_HALT ZvioNetMiniportHalt;
MINIPORT_PAUSE ZvioNetMiniportPause;
MINIPORT_RESTART ZvioNetMiniportRestart;
MINIPORT_SHUTDOWN ZvioNetMiniportShutdown;
MINIPORT_OID_REQUEST ZvioNetMiniportOidRequest;
MINIPORT_SEND_NET_BUFFER_LISTS ZvioNetMiniportSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS ZvioNetMiniportReturnNetBufferLists;
MINIPORT_CANCEL_SEND ZvioNetMiniportCancelSend;
MINIPORT_CHECK_FOR_HANG ZvioNetMiniportCheckForHang;
MINIPORT_RESET ZvioNetMiniportReset;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY ZvioNetMiniportDevicePnPEvent;
MINIPORT_UNLOAD ZvioNetMiniportUnload;

// virtqueue.c
NTSTATUS
ZvioNetQueueCreate(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ USHORT Index,
    _Out_ PZVIONET_VIRTQUEUE *Queue
    );

VOID
ZvioNetQueueDestroy(
    _In_ PZVIONET_VIRTQUEUE Queue
    );

NTSTATUS
ZvioNetQueueAddBuffer(
    _In_ PZVIONET_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    );

NTSTATUS
ZvioNetQueueAddBufferChain(
    _In_ PZVIONET_VIRTQUEUE Queue,
    _In_ PSCATTER_GATHER_LIST SgList,
    _In_ ULONG OutCount,
    _In_ ULONG InCount,
    _In_opt_ PVOID UserData,
    _Out_ PUSHORT HeadIdx
    );

PVOID
ZvioNetQueueGetBuffer(
    _In_ PZVIONET_VIRTQUEUE Queue,
    _Out_ PULONG Length
    );

VOID
ZvioNetQueueKick(
    _In_ PZVIONET_VIRTQUEUE Queue
    );

// tx.c
VOID
ZvioNetSendNetBufferLists(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists
    );

VOID
ZvioNetCompleteTx(
    _In_ PZVIONET_ADAPTER Adapter
    );

// rx.c
NTSTATUS
ZvioNetInitRxBuffers(
    _In_ PZVIONET_ADAPTER Adapter
    );

VOID
ZvioNetFreeRxBuffers(
    _In_ PZVIONET_ADAPTER Adapter
    );

VOID
ZvioNetProcessRx(
    _In_ PZVIONET_ADAPTER Adapter
    );

VOID
ZvioNetReplenishRx(
    _In_ PZVIONET_ADAPTER Adapter
    );

// interrupt.c
MINIPORT_ISR ZvioNetInterruptHandler;
MINIPORT_INTERRUPT_DPC ZvioNetInterruptDpc;
MINIPORT_ENABLE_INTERRUPT ZvioNetEnableInterrupt;
MINIPORT_DISABLE_INTERRUPT ZvioNetDisableInterrupt;

// pci.c
NTSTATUS
ZvioNetPciParseCapabilities(
    _In_ PZVIONET_ADAPTER Adapter
    );

// virtio.c
NTSTATUS
ZvioNetDeviceInit(
    _In_ PZVIONET_ADAPTER Adapter
    );

NTSTATUS
ZvioNetDeviceReset(
    _In_ PZVIONET_ADAPTER Adapter
    );

ULONGLONG
ZvioNetGetDeviceFeatures(
    _In_ PZVIONET_ADAPTER Adapter
    );

NTSTATUS
ZvioNetSetDriverFeatures(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ ULONGLONG Features
    );

UCHAR
ZvioNetGetDeviceStatus(
    _In_ PZVIONET_ADAPTER Adapter
    );

VOID
ZvioNetSetDeviceStatus(
    _In_ PZVIONET_ADAPTER Adapter,
    _In_ UCHAR Status
    );
