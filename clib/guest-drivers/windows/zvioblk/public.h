/*
 * Zixiao VirtIO Block Driver - Public Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO block device structures and definitions.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <ntdddisk.h>
#include <ntddscsi.h>

//
// Debug macros
//
#define ZVIOBLK_TAG     'klBZ'  // ZBlk

#if DBG
#define ZvioBlkDbgPrint(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "ZVIOBLK: " _fmt "\n", ##__VA_ARGS__)
#define ZvioBlkDbgError(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "ZVIOBLK ERROR: " _fmt "\n", ##__VA_ARGS__)
#else
#define ZvioBlkDbgPrint(_fmt, ...)
#define ZvioBlkDbgError(_fmt, ...)
#endif

//
// VirtIO Block Feature Bits
//
#define VIRTIO_BLK_F_SIZE_MAX       (1ULL << 1)   // Max segment size
#define VIRTIO_BLK_F_SEG_MAX        (1ULL << 2)   // Max # of segments
#define VIRTIO_BLK_F_GEOMETRY       (1ULL << 4)   // Legacy geometry
#define VIRTIO_BLK_F_RO             (1ULL << 5)   // Read-only device
#define VIRTIO_BLK_F_BLK_SIZE       (1ULL << 6)   // Block size
#define VIRTIO_BLK_F_FLUSH          (1ULL << 9)   // Flush command
#define VIRTIO_BLK_F_TOPOLOGY       (1ULL << 10)  // Topology info
#define VIRTIO_BLK_F_CONFIG_WCE     (1ULL << 11)  // Writeback mode
#define VIRTIO_BLK_F_MQ             (1ULL << 12)  // Multi-queue
#define VIRTIO_BLK_F_DISCARD        (1ULL << 13)  // TRIM/UNMAP support
#define VIRTIO_BLK_F_WRITE_ZEROES   (1ULL << 14)  // Write zeroes
#define VIRTIO_BLK_F_LIFETIME       (1ULL << 15)  // Lifetime tracking
#define VIRTIO_BLK_F_SECURE_ERASE   (1ULL << 16)  // Secure erase

// Common VirtIO features
#define VIRTIO_F_VERSION_1          (1ULL << 32)
#define VIRTIO_F_RING_INDIRECT_DESC (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX     (1ULL << 29)

//
// VirtIO Block Request Types
//
#define VIRTIO_BLK_T_IN             0   // Read
#define VIRTIO_BLK_T_OUT            1   // Write
#define VIRTIO_BLK_T_FLUSH          4   // Flush
#define VIRTIO_BLK_T_GET_ID         8   // Get device ID
#define VIRTIO_BLK_T_DISCARD        11  // Discard/TRIM
#define VIRTIO_BLK_T_WRITE_ZEROES   13  // Write zeroes

//
// VirtIO Block Status Values
//
#define VIRTIO_BLK_S_OK             0   // Success
#define VIRTIO_BLK_S_IOERR          1   // I/O error
#define VIRTIO_BLK_S_UNSUPP         2   // Unsupported operation

//
// VirtIO Block Device Configuration
//
#pragma pack(push, 1)
typedef struct _VIRTIO_BLK_CONFIG {
    ULONGLONG   Capacity;           // Capacity in 512-byte sectors
    ULONG       SizeMax;            // Max segment size (if VIRTIO_BLK_F_SIZE_MAX)
    ULONG       SegMax;             // Max # segments (if VIRTIO_BLK_F_SEG_MAX)
    struct {
        USHORT  Cylinders;
        UCHAR   Heads;
        UCHAR   Sectors;
    } Geometry;                     // Legacy geometry (if VIRTIO_BLK_F_GEOMETRY)
    ULONG       BlkSize;            // Block size (if VIRTIO_BLK_F_BLK_SIZE)
    struct {
        UCHAR   PhysicalBlockExp;   // # of logical blocks per physical block (log2)
        UCHAR   AlignmentOffset;    // Offset of first aligned logical block
        USHORT  MinIoSize;          // Min I/O size (# logical blocks)
        ULONG   OptIoSize;          // Optimal I/O size (# logical blocks)
    } Topology;                     // Topology info (if VIRTIO_BLK_F_TOPOLOGY)
    UCHAR       Writeback;          // Writeback mode (if VIRTIO_BLK_F_CONFIG_WCE)
    UCHAR       Unused0;
    USHORT      NumQueues;          // Number of request queues (if VIRTIO_BLK_F_MQ)
    ULONG       MaxDiscardSectors;  // Max discard sectors
    ULONG       MaxDiscardSeg;      // Max discard segments
    ULONG       DiscardSectorAlignment; // Discard alignment
    ULONG       MaxWriteZeroesSectors;  // Max write zeroes sectors
    ULONG       MaxWriteZeroesSeg;      // Max write zeroes segments
    UCHAR       WriteZeroesUnmap;       // Write zeroes can unmap
    UCHAR       Unused1[3];
    ULONG       MaxSecureEraseSectors;
    ULONG       MaxSecureEraseSeg;
    ULONG       SecureEraseSectorAlignment;
} VIRTIO_BLK_CONFIG, *PVIRTIO_BLK_CONFIG;
#pragma pack(pop)

//
// VirtIO Block Request Header
//
#pragma pack(push, 1)
typedef struct _VIRTIO_BLK_REQ_HDR {
    ULONG       Type;               // Request type (VIRTIO_BLK_T_*)
    ULONG       Reserved;           // Reserved (used for priority)
    ULONGLONG   Sector;             // Starting sector for read/write
} VIRTIO_BLK_REQ_HDR, *PVIRTIO_BLK_REQ_HDR;
#pragma pack(pop)

//
// VirtIO Block Discard/Write Zeroes Segment
//
#pragma pack(push, 1)
typedef struct _VIRTIO_BLK_DISCARD_WRITE_ZEROES {
    ULONGLONG   Sector;             // Starting sector
    ULONG       NumSectors;         // Number of sectors
    ULONG       Flags;              // Flags
} VIRTIO_BLK_DISCARD_WRITE_ZEROES, *PVIRTIO_BLK_DISCARD_WRITE_ZEROES;
#pragma pack(pop)

//
// Virtqueue Ring Structures (from VirtIO spec)
//
#define VRING_DESC_F_NEXT       1   // Buffer continues via 'next'
#define VRING_DESC_F_WRITE      2   // Buffer is write-only (device writes)
#define VRING_DESC_F_INDIRECT   4   // Buffer contains list of descriptors

#pragma pack(push, 1)
typedef struct _VRING_DESC {
    ULONGLONG   Addr;               // Physical address
    ULONG       Len;                // Length in bytes
    USHORT      Flags;              // Descriptor flags
    USHORT      Next;               // Next descriptor index
} VRING_DESC, *PVRING_DESC;

typedef struct _VRING_AVAIL {
    USHORT      Flags;
    USHORT      Idx;
    USHORT      Ring[1];            // Variable size
} VRING_AVAIL, *PVRING_AVAIL;

typedef struct _VRING_USED_ELEM {
    ULONG       Id;                 // Index of used descriptor chain
    ULONG       Len;                // Total bytes written
} VRING_USED_ELEM, *PVRING_USED_ELEM;

typedef struct _VRING_USED {
    USHORT          Flags;
    USHORT          Idx;
    VRING_USED_ELEM Ring[1];        // Variable size
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

//
// VirtIO Device Status Bits
//
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_FAILED        128

//
// Virtqueue Context
//
typedef struct _ZVIOBLK_VIRTQUEUE {
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

    PVOID                   *DescData;          // User data per descriptor

    WDFCOMMONBUFFER         RingBuffer;         // DMA buffer for rings
    WDFSPINLOCK             Lock;

    PUCHAR                  NotifyAddr;         // Notify register address

    struct _ZVIOBLK_DEVICE_CONTEXT *DeviceContext;
} ZVIOBLK_VIRTQUEUE, *PZVIOBLK_VIRTQUEUE;

//
// Block Request Context
//
typedef struct _ZVIOBLK_REQUEST {
    WDFREQUEST              Request;            // WDF request handle
    PZVIOBLK_VIRTQUEUE      Queue;              // Target virtqueue
    WDFCOMMONBUFFER         HeaderBuffer;       // DMA buffer for header
    PVIRTIO_BLK_REQ_HDR     Header;             // Request header
    PHYSICAL_ADDRESS        HeaderPhys;
    WDFCOMMONBUFFER         StatusBuffer;       // DMA buffer for status
    PUCHAR                  Status;             // Status byte
    PHYSICAL_ADDRESS        StatusPhys;
    ULONG                   DataLength;         // Data transfer length
    USHORT                  HeadDescIdx;        // First descriptor index
} ZVIOBLK_REQUEST, *PZVIOBLK_REQUEST;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ZVIOBLK_REQUEST, ZvioBlkGetRequestContext)

//
// Device Context
//
typedef struct _ZVIOBLK_DEVICE_CONTEXT {
    WDFDEVICE               Device;
    WDFQUEUE                IoQueue;

    // PCI Bus Interface
    BUS_INTERFACE_STANDARD  BusInterface;

    // BAR mappings
    PVOID                   BarVA[6];
    PHYSICAL_ADDRESS        BarPA[6];
    ULONG                   BarLength[6];

    // VirtIO configuration
    PVIRTIO_PCI_COMMON_CFG  CommonCfg;
    PVOID                   NotifyBase;
    ULONG                   NotifyOffMultiplier;
    PUCHAR                  IsrStatus;
    PVIRTIO_BLK_CONFIG      DeviceCfg;

    // Feature negotiation
    ULONGLONG               DeviceFeatures;
    ULONGLONG               DriverFeatures;

    // Virtqueues
    PZVIOBLK_VIRTQUEUE      *Queues;
    USHORT                  NumQueues;

    // DMA
    WDFDMAENABLER           DmaEnabler;

    // Interrupt
    WDFINTERRUPT            Interrupt;
    BOOLEAN                 UseMsix;

    // Block device info
    ULONGLONG               Capacity;           // In sectors
    ULONG                   SectorSize;         // Usually 512
    ULONG                   MaxSegments;
    ULONG                   MaxSegmentSize;
    BOOLEAN                 ReadOnly;
    BOOLEAN                 SupportsFlush;
    BOOLEAN                 SupportsDiscard;

    // Device ID string
    CHAR                    DeviceId[20 + 1];

    // Resources
    WDFCMRESLIST            ResourcesRaw;
    WDFCMRESLIST            ResourcesTranslated;
} ZVIOBLK_DEVICE_CONTEXT, *PZVIOBLK_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ZVIOBLK_DEVICE_CONTEXT, ZvioBlkGetDeviceContext)

//
// Function Prototypes
//

// driver.c
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ZvioBlkEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ZvioBlkEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE ZvioBlkEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ZvioBlkEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY ZvioBlkEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT ZvioBlkEvtDeviceD0Exit;

// virtqueue.c
NTSTATUS
ZvioBlkQueueCreate(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIOBLK_VIRTQUEUE *Queue
    );

VOID
ZvioBlkQueueDestroy(
    _In_ PZVIOBLK_VIRTQUEUE Queue
    );

NTSTATUS
ZvioBlkQueueAddBuffers(
    _In_ PZVIOBLK_VIRTQUEUE Queue,
    _In_ PSCATTER_GATHER_LIST SgList,
    _In_ ULONG OutCount,
    _In_ ULONG InCount,
    _In_opt_ PVOID UserData,
    _Out_ PUSHORT HeadIdx
    );

PVOID
ZvioBlkQueueGetBuffer(
    _In_ PZVIOBLK_VIRTQUEUE Queue,
    _Out_ PULONG Length
    );

VOID
ZvioBlkQueueKick(
    _In_ PZVIOBLK_VIRTQUEUE Queue
    );

// blk_io.c
EVT_WDF_IO_QUEUE_IO_READ ZvioBlkEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE ZvioBlkEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ZvioBlkEvtIoDeviceControl;

NTSTATUS
ZvioBlkSubmitRequest(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ ULONG Type,
    _In_ ULONGLONG Sector,
    _In_opt_ PMDL Mdl,
    _In_ ULONG Length
    );

VOID
ZvioBlkCompleteRequest(
    _In_ PZVIOBLK_REQUEST BlkRequest,
    _In_ UCHAR Status,
    _In_ ULONG BytesTransferred
    );

// interrupt.c
EVT_WDF_INTERRUPT_ISR ZvioBlkEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC ZvioBlkEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE ZvioBlkEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE ZvioBlkEvtInterruptDisable;

// pci.c
NTSTATUS
ZvioBlkPciReadConfig(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
ZvioBlkPciParseCapabilities(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    );

// virtio.c
NTSTATUS
ZvioBlkDeviceInit(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioBlkDeviceReset(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    );

ULONGLONG
ZvioBlkGetDeviceFeatures(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioBlkSetDriverFeatures(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ ULONGLONG Features
    );

UCHAR
ZvioBlkGetDeviceStatus(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext
    );

VOID
ZvioBlkSetDeviceStatus(
    _In_ PZVIOBLK_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    );
