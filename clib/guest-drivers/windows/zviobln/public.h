/*
 * Zixiao VirtIO Balloon Driver - Public Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO memory balloon device structures and definitions.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

//
// Debug macros
//
#define ZVIOBLN_TAG     'nlBZ'  // ZBln

#if DBG
#define ZvioBlnDbgPrint(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "ZVIOBLN: " _fmt "\n", ##__VA_ARGS__)
#define ZvioBlnDbgError(_fmt, ...)  \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "ZVIOBLN ERROR: " _fmt "\n", ##__VA_ARGS__)
#else
#define ZvioBlnDbgPrint(_fmt, ...)
#define ZvioBlnDbgError(_fmt, ...)
#endif

//
// VirtIO Balloon Feature Bits
//
#define VIRTIO_BALLOON_F_MUST_TELL_HOST     (1ULL << 0)  // Must tell host before releasing pages
#define VIRTIO_BALLOON_F_STATS_VQ           (1ULL << 1)  // Statistics virtqueue
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM     (1ULL << 2)  // Deflate on OOM
#define VIRTIO_BALLOON_F_FREE_PAGE_HINT     (1ULL << 3)  // Free page hinting
#define VIRTIO_BALLOON_F_PAGE_POISON        (1ULL << 4)  // Page poisoning
#define VIRTIO_BALLOON_F_REPORTING          (1ULL << 5)  // Free page reporting

// Common VirtIO features
#define VIRTIO_F_VERSION_1                  (1ULL << 32)
#define VIRTIO_F_RING_INDIRECT_DESC         (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX             (1ULL << 29)

//
// VirtIO Balloon Device Configuration
//
#pragma pack(push, 1)
typedef struct _VIRTIO_BALLOON_CONFIG {
    ULONG       NumPages;           // Number of pages host wants in balloon
    ULONG       ActualPages;        // Number of pages actually in balloon
    ULONG       FreePageHintCmdId;  // Free page hint command ID (if VIRTIO_BALLOON_F_FREE_PAGE_HINT)
    ULONG       PoisonVal;          // Page poison value (if VIRTIO_BALLOON_F_PAGE_POISON)
} VIRTIO_BALLOON_CONFIG, *PVIRTIO_BALLOON_CONFIG;
#pragma pack(pop)

//
// VirtIO Balloon Statistics Tags
//
#define VIRTIO_BALLOON_S_SWAP_IN        0   // Amount of memory swapped in
#define VIRTIO_BALLOON_S_SWAP_OUT       1   // Amount of memory swapped out
#define VIRTIO_BALLOON_S_MAJFLT         2   // Major page faults
#define VIRTIO_BALLOON_S_MINFLT         3   // Minor page faults
#define VIRTIO_BALLOON_S_MEMFREE        4   // Free memory
#define VIRTIO_BALLOON_S_MEMTOT         5   // Total memory
#define VIRTIO_BALLOON_S_AVAIL          6   // Available memory
#define VIRTIO_BALLOON_S_CACHES         7   // Disk caches
#define VIRTIO_BALLOON_S_HTLB_PGALLOC   8   // Huge TLB page allocations
#define VIRTIO_BALLOON_S_HTLB_PGFAIL    9   // Huge TLB page allocation failures
#define VIRTIO_BALLOON_S_NR             10  // Number of stats

#pragma pack(push, 1)
typedef struct _VIRTIO_BALLOON_STAT {
    USHORT      Tag;
    ULONGLONG   Val;
} VIRTIO_BALLOON_STAT, *PVIRTIO_BALLOON_STAT;
#pragma pack(pop)

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
typedef struct _ZVIOBLN_DEVICE_CONTEXT ZVIOBLN_DEVICE_CONTEXT, *PZVIOBLN_DEVICE_CONTEXT;
typedef struct _ZVIOBLN_VIRTQUEUE ZVIOBLN_VIRTQUEUE, *PZVIOBLN_VIRTQUEUE;

//
// Balloon Page Entry - tracks allocated pages
//
typedef struct _ZVIOBLN_PAGE_ENTRY {
    LIST_ENTRY      Link;
    PMDL            Mdl;
    PVOID           VirtualAddress;
    PFN_NUMBER      PageFrameNumber;
} ZVIOBLN_PAGE_ENTRY, *PZVIOBLN_PAGE_ENTRY;

//
// Virtqueue Context
//
struct _ZVIOBLN_VIRTQUEUE {
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

    WDFCOMMONBUFFER         RingBuffer;
    WDFSPINLOCK             Lock;

    PUCHAR                  NotifyAddr;

    PZVIOBLN_DEVICE_CONTEXT DeviceContext;
};

//
// Device Context
//
struct _ZVIOBLN_DEVICE_CONTEXT {
    WDFDEVICE               Device;
    WDFTIMER                WorkTimer;

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
    PVIRTIO_BALLOON_CONFIG  DeviceCfg;

    // Feature negotiation
    ULONGLONG               DeviceFeatures;
    ULONGLONG               DriverFeatures;

    // Virtqueues
    PZVIOBLN_VIRTQUEUE      InflateQueue;   // Queue 0: Inflate (add pages to balloon)
    PZVIOBLN_VIRTQUEUE      DeflateQueue;   // Queue 1: Deflate (remove pages from balloon)
    PZVIOBLN_VIRTQUEUE      StatsQueue;     // Queue 2: Statistics (optional)

    // DMA
    WDFDMAENABLER           DmaEnabler;

    // Interrupt
    WDFINTERRUPT            Interrupt;
    BOOLEAN                 UseMsix;

    // Balloon state
    LIST_ENTRY              PageList;       // List of inflated pages
    WDFSPINLOCK             PageListLock;
    ULONG                   NumPages;       // Current pages in balloon
    ULONG                   TargetPages;    // Target pages requested by host

    // Page frame number buffer for communication with host
    WDFCOMMONBUFFER         PfnBuffer;
    PULONG                  PfnArray;
    PHYSICAL_ADDRESS        PfnArrayPhys;
    ULONG                   PfnArraySize;

    // Statistics buffer
    WDFCOMMONBUFFER         StatsBuffer;
    PVIRTIO_BALLOON_STAT    Stats;
    PHYSICAL_ADDRESS        StatsPhys;

    // Resources
    WDFCMRESLIST            ResourcesRaw;
    WDFCMRESLIST            ResourcesTranslated;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ZVIOBLN_DEVICE_CONTEXT, ZvioBlnGetDeviceContext)

//
// Maximum pages per inflate/deflate operation
//
#define ZVIOBLN_MAX_PAGES_PER_OP    256

//
// Function Prototypes
//

// driver.c
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ZvioBlnEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ZvioBlnEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE ZvioBlnEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ZvioBlnEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY ZvioBlnEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT ZvioBlnEvtDeviceD0Exit;

// balloon.c
EVT_WDF_TIMER ZvioBlnWorkTimerCallback;

NTSTATUS
ZvioBlnInflate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG NumPages
    );

NTSTATUS
ZvioBlnDeflate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG NumPages
    );

VOID
ZvioBlnUpdateStats(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

// virtqueue.c
NTSTATUS
ZvioBlnQueueCreate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIOBLN_VIRTQUEUE *Queue
    );

VOID
ZvioBlnQueueDestroy(
    _In_ PZVIOBLN_VIRTQUEUE Queue
    );

NTSTATUS
ZvioBlnQueueAddBuffer(
    _In_ PZVIOBLN_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    );

PVOID
ZvioBlnQueueGetBuffer(
    _In_ PZVIOBLN_VIRTQUEUE Queue,
    _Out_ PULONG Length
    );

VOID
ZvioBlnQueueKick(
    _In_ PZVIOBLN_VIRTQUEUE Queue
    );

// interrupt.c
EVT_WDF_INTERRUPT_ISR ZvioBlnEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC ZvioBlnEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE ZvioBlnEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE ZvioBlnEvtInterruptDisable;

// pci.c
NTSTATUS
ZvioBlnPciReadConfig(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
ZvioBlnPciParseCapabilities(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

// virtio.c
NTSTATUS
ZvioBlnDeviceInit(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioBlnDeviceReset(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

ULONGLONG
ZvioBlnGetDeviceFeatures(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioBlnSetDriverFeatures(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONGLONG Features
    );

UCHAR
ZvioBlnGetDeviceStatus(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    );

VOID
ZvioBlnSetDeviceStatus(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    );
