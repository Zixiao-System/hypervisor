/*
 * Zixiao VirtIO PCI Bus Driver - Public Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * VirtIO 1.0+ PCI transport definitions for Windows KMDF driver.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <wdmguid.h>
#include <ntstrsafe.h>

//
// Suppress warning for flexible array members (C99 feature)
// MSVC treats zero-size arrays as non-standard extension
//
#pragma warning(disable: 4200)

//
// PCI Configuration Space Offsets
//
#define PCI_DEVICE_ID_OFFSET            0x02
#define PCI_SUBSYSTEM_ID_OFFSET         0x2E
#define PCI_CAPABILITYLIST_OFFSET       0x34

//
// WRITE_REGISTER_ULONG64 compatibility
// Some WDK versions don't have WRITE_REGISTER_ULONGLONG
//
#ifndef WRITE_REGISTER_ULONG64
#define WRITE_REGISTER_ULONG64(Register, Value) \
    do { \
        WRITE_REGISTER_ULONG((PULONG)(Register), (ULONG)(Value)); \
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)(Register) + 4), (ULONG)((Value) >> 32)); \
    } while (0)
#endif

#ifndef WRITE_REGISTER_ULONGLONG
#define WRITE_REGISTER_ULONGLONG WRITE_REGISTER_ULONG64
#endif

//
// VirtIO PCI Vendor/Device IDs
//
#define VIRTIO_PCI_VENDOR_ID            0x1AF4

// Transitional device IDs (legacy)
#define VIRTIO_PCI_DEVICE_ID_NET        0x1000
#define VIRTIO_PCI_DEVICE_ID_BLK        0x1001
#define VIRTIO_PCI_DEVICE_ID_BALLOON    0x1002
#define VIRTIO_PCI_DEVICE_ID_CONSOLE    0x1003
#define VIRTIO_PCI_DEVICE_ID_SCSI       0x1004
#define VIRTIO_PCI_DEVICE_ID_RNG        0x1005
#define VIRTIO_PCI_DEVICE_ID_9P         0x1009

// Modern device IDs (VirtIO 1.0+)
#define VIRTIO_PCI_DEVICE_ID_BASE       0x1040
#define VIRTIO_PCI_DEVICE_ID_NET_MODERN    (VIRTIO_PCI_DEVICE_ID_BASE + 1)
#define VIRTIO_PCI_DEVICE_ID_BLK_MODERN    (VIRTIO_PCI_DEVICE_ID_BASE + 2)
#define VIRTIO_PCI_DEVICE_ID_CONSOLE_MODERN (VIRTIO_PCI_DEVICE_ID_BASE + 3)
#define VIRTIO_PCI_DEVICE_ID_RNG_MODERN    (VIRTIO_PCI_DEVICE_ID_BASE + 4)
#define VIRTIO_PCI_DEVICE_ID_BALLOON_MODERN (VIRTIO_PCI_DEVICE_ID_BASE + 5)
#define VIRTIO_PCI_DEVICE_ID_SCSI_MODERN   (VIRTIO_PCI_DEVICE_ID_BASE + 8)
#define VIRTIO_PCI_DEVICE_ID_9P_MODERN     (VIRTIO_PCI_DEVICE_ID_BASE + 9)
#define VIRTIO_PCI_DEVICE_ID_GPU_MODERN    (VIRTIO_PCI_DEVICE_ID_BASE + 16)
#define VIRTIO_PCI_DEVICE_ID_INPUT_MODERN  (VIRTIO_PCI_DEVICE_ID_BASE + 18)
#define VIRTIO_PCI_DEVICE_ID_VSOCK_MODERN  (VIRTIO_PCI_DEVICE_ID_BASE + 19)

//
// VirtIO Device Types
//
typedef enum _VIRTIO_DEVICE_TYPE {
    VirtioDevTypeUnknown = 0,
    VirtioDevTypeNet = 1,
    VirtioDevTypeBlock = 2,
    VirtioDevTypeConsole = 3,
    VirtioDevTypeEntropy = 4,
    VirtioDevTypeBalloon = 5,
    VirtioDevTypeIOMemory = 6,
    VirtioDevTypeRPMsg = 7,
    VirtioDevTypeSCSI = 8,
    VirtioDevType9P = 9,
    VirtioDevTypeMac80211 = 10,
    VirtioDevTypeRProcSerial = 11,
    VirtioDevTypeCAIF = 12,
    VirtioDevTypeMemoryBalloon = 13,
    VirtioDevTypeGPU = 16,
    VirtioDevTypeClock = 17,
    VirtioDevTypeInput = 18,
    VirtioDevTypeVSock = 19,
    VirtioDevTypeCrypto = 20,
    VirtioDevTypeSignal = 21,
    VirtioDevTypePMem = 22,
    VirtioDevTypeIOMMU = 23,
    VirtioDevTypeMem = 24,
    VirtioDevTypeSound = 25,
    VirtioDevTypeFS = 26,
    VirtioDevTypePMEM2 = 27,
    VirtioDevTypeBT = 40,
    VirtioDevTypeMax
} VIRTIO_DEVICE_TYPE;

//
// VirtIO PCI Capability Types (VirtIO 1.0 spec section 4.1.4)
//
#define VIRTIO_PCI_CAP_COMMON_CFG       1   // Common configuration
#define VIRTIO_PCI_CAP_NOTIFY_CFG       2   // Notifications
#define VIRTIO_PCI_CAP_ISR_CFG          3   // ISR status
#define VIRTIO_PCI_CAP_DEVICE_CFG       4   // Device-specific config
#define VIRTIO_PCI_CAP_PCI_CFG          5   // PCI configuration access
#define VIRTIO_PCI_CAP_SHARED_MEM_CFG   8   // Shared memory regions

//
// VirtIO PCI Capability Structure
//
#pragma pack(push, 1)
typedef struct _VIRTIO_PCI_CAP {
    UCHAR   CapVndr;        // 0x09 (PCI_CAP_ID_VENDOR_SPECIFIC)
    UCHAR   CapNext;        // Offset to next capability
    UCHAR   CapLen;         // Length of this capability
    UCHAR   CfgType;        // VIRTIO_PCI_CAP_* type
    UCHAR   Bar;            // BAR index (0-5)
    UCHAR   Id;             // Capability ID (for shared memory)
    UCHAR   Padding[2];     // Padding
    ULONG   Offset;         // Offset within BAR
    ULONG   Length;         // Length of structure
} VIRTIO_PCI_CAP, *PVIRTIO_PCI_CAP;

//
// VirtIO PCI Notify Capability (extends VIRTIO_PCI_CAP)
//
typedef struct _VIRTIO_PCI_NOTIFY_CAP {
    VIRTIO_PCI_CAP  Cap;
    ULONG           NotifyOffMultiplier;   // Multiplier for queue notify offset
} VIRTIO_PCI_NOTIFY_CAP, *PVIRTIO_PCI_NOTIFY_CAP;

//
// VirtIO Common Configuration Structure (mapped from BAR)
//
typedef struct _VIRTIO_PCI_COMMON_CFG {
    // Device
    ULONG   DeviceFeatureSel;       // 0x00: Feature selection
    ULONG   DeviceFeature;          // 0x04: Feature bits 0-31 or 32-63
    ULONG   DriverFeatureSel;       // 0x08: Feature selection
    ULONG   DriverFeature;          // 0x0C: Feature bits 0-31 or 32-63
    USHORT  MsixConfig;             // 0x10: MSI-X config vector
    USHORT  NumQueues;              // 0x12: Number of queues
    UCHAR   DeviceStatus;           // 0x14: Device status
    UCHAR   ConfigGeneration;       // 0x15: Configuration atomicity
    // Queue
    USHORT  QueueSel;               // 0x16: Queue selection
    USHORT  QueueSize;              // 0x18: Queue size
    USHORT  QueueMsixVector;        // 0x1A: Queue MSI-X vector
    USHORT  QueueEnable;            // 0x1C: Queue enable
    USHORT  QueueNotifyOff;         // 0x1E: Queue notify offset
    ULONGLONG QueueDesc;            // 0x20: Descriptor table address
    ULONGLONG QueueDriver;          // 0x28: Available ring address
    ULONGLONG QueueDevice;          // 0x30: Used ring address
} VIRTIO_PCI_COMMON_CFG, *PVIRTIO_PCI_COMMON_CFG;
#pragma pack(pop)

//
// VirtIO Device Status Bits
//
#define VIRTIO_STATUS_ACKNOWLEDGE       0x01
#define VIRTIO_STATUS_DRIVER            0x02
#define VIRTIO_STATUS_DRIVER_OK         0x04
#define VIRTIO_STATUS_FEATURES_OK       0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED            0x80

//
// VirtIO Feature Bits (Common)
//
#define VIRTIO_F_NOTIFY_ON_EMPTY        (1ULL << 24)
#define VIRTIO_F_ANY_LAYOUT             (1ULL << 27)
#define VIRTIO_F_RING_INDIRECT_DESC     (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX         (1ULL << 29)
#define VIRTIO_F_VERSION_1              (1ULL << 32)
#define VIRTIO_F_ACCESS_PLATFORM        (1ULL << 33)
#define VIRTIO_F_RING_PACKED            (1ULL << 34)
#define VIRTIO_F_IN_ORDER               (1ULL << 35)
#define VIRTIO_F_ORDER_PLATFORM         (1ULL << 36)
#define VIRTIO_F_SR_IOV                 (1ULL << 37)
#define VIRTIO_F_NOTIFICATION_DATA      (1ULL << 38)

//
// Virtqueue Descriptor Flags
//
#define VRING_DESC_F_NEXT               0x01
#define VRING_DESC_F_WRITE              0x02
#define VRING_DESC_F_INDIRECT           0x04

//
// Virtqueue Available Ring Flags
//
#define VRING_AVAIL_F_NO_INTERRUPT      0x01

//
// Virtqueue Used Ring Flags
//
#define VRING_USED_F_NO_NOTIFY          0x01

//
// Virtqueue Structures
//
#pragma pack(push, 1)
typedef struct _VRING_DESC {
    ULONGLONG   Addr;           // Physical address of buffer
    ULONG       Len;            // Length of buffer
    USHORT      Flags;          // VRING_DESC_F_*
    USHORT      Next;           // Next descriptor index
} VRING_DESC, *PVRING_DESC;

typedef struct _VRING_AVAIL {
    USHORT      Flags;          // VRING_AVAIL_F_*
    USHORT      Idx;            // Next available index
    USHORT      Ring[];         // Available descriptor indices
    // Followed by: USHORT UsedEvent; (if VIRTIO_F_RING_EVENT_IDX)
} VRING_AVAIL, *PVRING_AVAIL;

typedef struct _VRING_USED_ELEM {
    ULONG       Id;             // Descriptor index
    ULONG       Len;            // Length written by device
} VRING_USED_ELEM, *PVRING_USED_ELEM;

typedef struct _VRING_USED {
    USHORT          Flags;      // VRING_USED_F_*
    USHORT          Idx;        // Next used index
    VRING_USED_ELEM Ring[];     // Used elements
    // Followed by: USHORT AvailEvent; (if VIRTIO_F_RING_EVENT_IDX)
} VRING_USED, *PVRING_USED;
#pragma pack(pop)

//
// MSI-X Vector Constants
//
#define VIRTIO_MSI_NO_VECTOR            0xFFFF
#define VIRTIO_MSI_CONFIG_VECTOR        0

//
// PCI Configuration Space Offsets
//
#define PCI_CAP_ID_VENDOR_SPECIFIC      0x09
#define PCI_CFG_SPACE_SIZE              256
#define PCI_CFG_SPACE_EXP_SIZE          4096

//
// Forward Declarations
//
typedef struct _ZVIO_DEVICE_CONTEXT ZVIO_DEVICE_CONTEXT, *PZVIO_DEVICE_CONTEXT;
typedef struct _ZVIO_VIRTQUEUE ZVIO_VIRTQUEUE, *PZVIO_VIRTQUEUE;

//
// Virtqueue Context Structure
//
typedef struct _ZVIO_VIRTQUEUE {
    USHORT              Index;              // Queue index
    USHORT              Size;               // Number of descriptors
    USHORT              NumFree;            // Number of free descriptors
    USHORT              FreeHead;           // First free descriptor
    USHORT              LastUsedIdx;        // Last seen used index

    PVRING_DESC         Desc;               // Descriptor table
    PVRING_AVAIL        Avail;              // Available ring
    PVRING_USED         Used;               // Used ring

    PHYSICAL_ADDRESS    DescPhys;           // Physical address of descriptors
    PHYSICAL_ADDRESS    AvailPhys;          // Physical address of avail ring
    PHYSICAL_ADDRESS    UsedPhys;           // Physical address of used ring

    PVOID               NotifyAddr;         // Queue-specific notify address
    WDFSPINLOCK         Lock;               // Queue lock

    // Tracking for in-flight descriptors
    PVOID              *DescData;           // User data per descriptor
    WDFCOMMONBUFFER     RingBuffer;         // DMA buffer for rings

    PZVIO_DEVICE_CONTEXT DeviceContext;     // Back-pointer to device
} ZVIO_VIRTQUEUE, *PZVIO_VIRTQUEUE;

//
// Device Context Structure
//
typedef struct _ZVIO_DEVICE_CONTEXT {
    WDFDEVICE               Device;             // WDF device handle
    VIRTIO_DEVICE_TYPE      DeviceType;         // VirtIO device type

    // PCI Resources
    WDFCMRESLIST            ResourcesRaw;
    WDFCMRESLIST            ResourcesTranslated;
    BUS_INTERFACE_STANDARD  BusInterface;       // PCI bus interface

    // BAR Mappings
    PVOID                   BarVA[6];           // Virtual addresses of BARs
    PHYSICAL_ADDRESS        BarPA[6];           // Physical addresses of BARs
    SIZE_T                  BarLength[6];       // Lengths of BARs
    ULONG                   BarType[6];         // Memory or I/O

    // VirtIO Configuration Pointers (within BARs)
    PVIRTIO_PCI_COMMON_CFG  CommonCfg;          // Common config structure
    PVOID                   NotifyBase;         // Notification base address
    ULONG                   NotifyOffMultiplier;// Notify offset multiplier
    PUCHAR                  IsrStatus;          // ISR status byte
    PVOID                   DeviceCfg;          // Device-specific config
    ULONG                   DeviceCfgLen;       // Device config length

    // Feature Negotiation
    ULONGLONG               DeviceFeatures;     // Features offered by device
    ULONGLONG               DriverFeatures;     // Features accepted by driver

    // Virtqueues
    USHORT                  NumQueues;          // Number of queues
    PZVIO_VIRTQUEUE        *Queues;             // Array of queue pointers

    // Interrupts
    WDFINTERRUPT           *Interrupts;         // MSI-X interrupt objects
    USHORT                  NumInterrupts;      // Number of MSI-X vectors
    BOOLEAN                 UseMsix;            // Using MSI-X

    // DMA
    WDFDMAENABLER           DmaEnabler;         // DMA enabler for 64-bit

} ZVIO_DEVICE_CONTEXT, *PZVIO_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ZVIO_DEVICE_CONTEXT, ZvioGetDeviceContext)

//
// Child PDO Context (for bus enumeration)
//
typedef struct _ZVIO_PDO_CONTEXT {
    VIRTIO_DEVICE_TYPE      DeviceType;
    USHORT                  SubSystemDeviceId;
    USHORT                  SubSystemVendorId;
} ZVIO_PDO_CONTEXT, *PZVIO_PDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ZVIO_PDO_CONTEXT, ZvioGetPdoContext)

//
// Function Prototypes - Driver Entry
//
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ZvioEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ZvioEvtDriverContextCleanup;

//
// Function Prototypes - PnP
//
EVT_WDF_DEVICE_PREPARE_HARDWARE ZvioEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ZvioEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY ZvioEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT ZvioEvtDeviceD0Exit;

//
// Function Prototypes - PCI Configuration
//
NTSTATUS
ZvioPciReadConfig(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
ZvioPciWriteConfig(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
ZvioPciParseCapabilities(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

//
// Function Prototypes - VirtIO Operations
//
NTSTATUS
ZvioDeviceInit(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioDeviceReset(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

ULONGLONG
ZvioGetDeviceFeatures(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

NTSTATUS
ZvioSetDriverFeatures(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ ULONGLONG Features
    );

UCHAR
ZvioGetDeviceStatus(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

VOID
ZvioSetDeviceStatus(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ UCHAR Status
    );

//
// Function Prototypes - Virtqueue Operations
//
NTSTATUS
ZvioQueueCreate(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext,
    _In_ USHORT Index,
    _Out_ PZVIO_VIRTQUEUE *Queue
    );

VOID
ZvioQueueDestroy(
    _In_ PZVIO_VIRTQUEUE Queue
    );

NTSTATUS
ZvioQueueAddBuffer(
    _In_ PZVIO_VIRTQUEUE Queue,
    _In_ PHYSICAL_ADDRESS PhysAddr,
    _In_ ULONG Length,
    _In_ BOOLEAN DeviceWritable,
    _In_opt_ PVOID UserData
    );

PVOID
ZvioQueueGetBuffer(
    _In_ PZVIO_VIRTQUEUE Queue,
    _Out_ PULONG Length
    );

VOID
ZvioQueueKick(
    _In_ PZVIO_VIRTQUEUE Queue
    );

BOOLEAN
ZvioQueueEnableInterrupts(
    _In_ PZVIO_VIRTQUEUE Queue,
    _In_ BOOLEAN Enable
    );

//
// Function Prototypes - Interrupt Handling
//
EVT_WDF_INTERRUPT_ISR ZvioEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC ZvioEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE ZvioEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE ZvioEvtInterruptDisable;

//
// Function Prototypes - Bus Enumeration
//
NTSTATUS
ZvioBusEnumerateChildren(
    _In_ PZVIO_DEVICE_CONTEXT DeviceContext
    );

//
// Logging/Tracing
//
#define ZVIO_POOL_TAG 'oivZ'

// WPP Tracing would be configured here
// For now, use DbgPrint for debugging
#if DBG
#define ZvioDbgPrint(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[zviopci] " fmt "\n", ##__VA_ARGS__)
#else
#define ZvioDbgPrint(fmt, ...)
#endif

#define ZvioDbgError(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[zviopci] ERROR: " fmt "\n", ##__VA_ARGS__)
