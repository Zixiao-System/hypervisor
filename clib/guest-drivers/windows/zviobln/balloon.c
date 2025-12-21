/*
 * Zixiao VirtIO Balloon Driver - Balloon Operations
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Memory balloon inflate/deflate and statistics operations.
 */

#include "public.h"

/*
 * ZvioBlnWorkTimerCallback - Periodic work timer callback
 *
 * Checks host's requested balloon size and adjusts accordingly.
 */
VOID
ZvioBlnWorkTimerCallback(
    _In_ WDFTIMER Timer
    )
{
    WDFDEVICE device;
    PZVIOBLN_DEVICE_CONTEXT deviceContext;
    ULONG targetPages;
    ULONG currentPages;
    LONG delta;

    device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    deviceContext = ZvioBlnGetDeviceContext(device);

    if (!deviceContext->DeviceCfg) {
        return;
    }

    //
    // Read target from device config
    //
    targetPages = READ_REGISTER_ULONG(&deviceContext->DeviceCfg->NumPages);

    WdfSpinLockAcquire(deviceContext->PageListLock);
    currentPages = deviceContext->NumPages;
    WdfSpinLockRelease(deviceContext->PageListLock);

    deviceContext->TargetPages = targetPages;

    delta = (LONG)targetPages - (LONG)currentPages;

    if (delta > 0) {
        //
        // Need to inflate (give memory to host)
        //
        ZvioBlnInflate(deviceContext, (ULONG)delta);
    } else if (delta < 0) {
        //
        // Need to deflate (reclaim memory from host)
        //
        ZvioBlnDeflate(deviceContext, (ULONG)(-delta));
    }

    //
    // Update statistics if supported
    //
    if (deviceContext->DriverFeatures & VIRTIO_BALLOON_F_STATS_VQ) {
        ZvioBlnUpdateStats(deviceContext);
    }

    //
    // Update actual pages in device config
    //
    WdfSpinLockAcquire(deviceContext->PageListLock);
    WRITE_REGISTER_ULONG(&deviceContext->DeviceCfg->ActualPages, deviceContext->NumPages);
    WdfSpinLockRelease(deviceContext->PageListLock);
}

/*
 * ZvioBlnInflate - Inflate balloon (allocate pages and give to host)
 */
NTSTATUS
ZvioBlnInflate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG NumPages
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG pagesToInflate;
    ULONG inflated = 0;
    ULONG i;
    PZVIOBLN_PAGE_ENTRY pageEntry;
    PHYSICAL_ADDRESS lowAddr, highAddr, skipBytes;
    PMDL mdl;

    ZvioBlnDbgPrint("Inflate: requesting %u pages", NumPages);

    lowAddr.QuadPart = 0;
    highAddr.QuadPart = (ULONGLONG)-1;
    skipBytes.QuadPart = 0;

    while (inflated < NumPages) {
        pagesToInflate = min(NumPages - inflated, ZVIOBLN_MAX_PAGES_PER_OP);

        //
        // Allocate pages
        //
        for (i = 0; i < pagesToInflate; i++) {
            //
            // Allocate a single page using MmAllocatePagesForMdl
            //
            mdl = MmAllocatePagesForMdl(lowAddr, highAddr, skipBytes, PAGE_SIZE);
            if (!mdl) {
                ZvioBlnDbgPrint("Failed to allocate page %u/%u", i, pagesToInflate);
                break;
            }

            //
            // Create page entry to track this allocation
            //
            pageEntry = (PZVIOBLN_PAGE_ENTRY)ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                sizeof(ZVIOBLN_PAGE_ENTRY),
                ZVIOBLN_TAG
                );

            if (!pageEntry) {
                MmFreePagesFromMdl(mdl);
                ExFreePool(mdl);
                ZvioBlnDbgPrint("Failed to allocate page entry");
                break;
            }

            pageEntry->Mdl = mdl;
            pageEntry->PageFrameNumber = MmGetMdlPfnArray(mdl)[0];
            pageEntry->VirtualAddress = NULL;

            //
            // Add PFN to array for host notification
            //
            DeviceContext->PfnArray[i] = (ULONG)pageEntry->PageFrameNumber;

            //
            // Add to page list
            //
            WdfSpinLockAcquire(DeviceContext->PageListLock);
            InsertTailList(&DeviceContext->PageList, &pageEntry->Link);
            DeviceContext->NumPages++;
            WdfSpinLockRelease(DeviceContext->PageListLock);
        }

        if (i == 0) {
            //
            // Couldn't allocate any pages
            //
            ZvioBlnDbgPrint("No pages allocated, stopping inflate");
            break;
        }

        //
        // Notify host about inflated pages via inflate queue
        //
        status = ZvioBlnQueueAddBuffer(
            DeviceContext->InflateQueue,
            DeviceContext->PfnArrayPhys,
            i * sizeof(ULONG),
            FALSE,
            NULL
            );

        if (NT_SUCCESS(status)) {
            ZvioBlnQueueKick(DeviceContext->InflateQueue);
        }

        inflated += i;
    }

    ZvioBlnDbgPrint("Inflated %u pages (requested %u)", inflated, NumPages);
    return status;
}

/*
 * ZvioBlnDeflate - Deflate balloon (reclaim pages from host)
 */
NTSTATUS
ZvioBlnDeflate(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG NumPages
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG pagesToDeflate;
    ULONG deflated = 0;
    ULONG i;
    PLIST_ENTRY entry;
    PZVIOBLN_PAGE_ENTRY pageEntry;
    PZVIOBLN_PAGE_ENTRY pageEntries[ZVIOBLN_MAX_PAGES_PER_OP];
    BOOLEAN mustTellHost;

    ZvioBlnDbgPrint("Deflate: releasing %u pages", NumPages);

    mustTellHost = (DeviceContext->DriverFeatures & VIRTIO_BALLOON_F_MUST_TELL_HOST) != 0;

    while (deflated < NumPages) {
        pagesToDeflate = min(NumPages - deflated, ZVIOBLN_MAX_PAGES_PER_OP);

        //
        // Collect pages to deflate
        //
        WdfSpinLockAcquire(DeviceContext->PageListLock);

        for (i = 0; i < pagesToDeflate; i++) {
            if (IsListEmpty(&DeviceContext->PageList)) {
                break;
            }

            entry = RemoveHeadList(&DeviceContext->PageList);
            pageEntry = CONTAINING_RECORD(entry, ZVIOBLN_PAGE_ENTRY, Link);
            DeviceContext->NumPages--;

            pageEntries[i] = pageEntry;
            DeviceContext->PfnArray[i] = (ULONG)pageEntry->PageFrameNumber;
        }

        WdfSpinLockRelease(DeviceContext->PageListLock);

        if (i == 0) {
            //
            // No more pages to deflate
            //
            ZvioBlnDbgPrint("No pages to deflate");
            break;
        }

        //
        // Notify host if required
        //
        if (mustTellHost) {
            status = ZvioBlnQueueAddBuffer(
                DeviceContext->DeflateQueue,
                DeviceContext->PfnArrayPhys,
                i * sizeof(ULONG),
                FALSE,
                NULL
                );

            if (NT_SUCCESS(status)) {
                ZvioBlnQueueKick(DeviceContext->DeflateQueue);
            }
        }

        //
        // Free the pages
        //
        for (ULONG j = 0; j < i; j++) {
            pageEntry = pageEntries[j];

            if (pageEntry->Mdl) {
                MmFreePagesFromMdl(pageEntry->Mdl);
                ExFreePool(pageEntry->Mdl);
            }
            ExFreePoolWithTag(pageEntry, ZVIOBLN_TAG);
        }

        deflated += i;
    }

    ZvioBlnDbgPrint("Deflated %u pages (requested %u)", deflated, NumPages);
    return status;
}

/*
 * ZvioBlnUpdateStats - Update memory statistics and send to host
 */
VOID
ZvioBlnUpdateStats(
    _In_ PZVIOBLN_DEVICE_CONTEXT DeviceContext
    )
{
    SYSTEM_BASIC_INFORMATION basicInfo;
    SYSTEM_PERFORMANCE_INFORMATION perfInfo;
    NTSTATUS status;
    ULONG statIndex = 0;

    if (!DeviceContext->Stats || !DeviceContext->StatsQueue) {
        return;
    }

    //
    // Query system memory information
    //
    status = ZwQuerySystemInformation(
        SystemBasicInformation,
        &basicInfo,
        sizeof(basicInfo),
        NULL
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to query basic info: 0x%X", status);
        return;
    }

    status = ZwQuerySystemInformation(
        SystemPerformanceInformation,
        &perfInfo,
        sizeof(perfInfo),
        NULL
        );

    if (!NT_SUCCESS(status)) {
        ZvioBlnDbgError("Failed to query perf info: 0x%X", status);
        return;
    }

    //
    // Fill in statistics
    //
    // Total memory
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_MEMTOT;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)basicInfo.NumberOfPhysicalPages * PAGE_SIZE;
    statIndex++;

    // Free memory
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_MEMFREE;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)perfInfo.AvailablePages * PAGE_SIZE;
    statIndex++;

    // Available memory (same as free for now)
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_AVAIL;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)perfInfo.AvailablePages * PAGE_SIZE;
    statIndex++;

    // Cache (system cache pages)
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_CACHES;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)perfInfo.ResidentSystemCachePage * PAGE_SIZE;
    statIndex++;

    // Page faults (major)
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_MAJFLT;
    DeviceContext->Stats[statIndex].Val = perfInfo.PageReadCount;
    statIndex++;

    // Page faults (minor - approximate with total minus major)
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_MINFLT;
    DeviceContext->Stats[statIndex].Val = perfInfo.DirtyPagesWriteCount;
    statIndex++;

    // Swap in
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_SWAP_IN;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)perfInfo.PageReadIoCount * PAGE_SIZE;
    statIndex++;

    // Swap out
    DeviceContext->Stats[statIndex].Tag = VIRTIO_BALLOON_S_SWAP_OUT;
    DeviceContext->Stats[statIndex].Val = (ULONGLONG)perfInfo.MappedPagesWriteCount * PAGE_SIZE;
    statIndex++;

    //
    // Send stats to host via stats queue
    //
    status = ZvioBlnQueueAddBuffer(
        DeviceContext->StatsQueue,
        DeviceContext->StatsPhys,
        statIndex * sizeof(VIRTIO_BALLOON_STAT),
        FALSE,
        NULL
        );

    if (NT_SUCCESS(status)) {
        ZvioBlnQueueKick(DeviceContext->StatsQueue);
    }
}
