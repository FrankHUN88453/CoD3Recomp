// Xbox 360 kernel imports: memory, and the critical sections built on it.
//
// Guest memory is one 4 GB reservation with pages committed on first touch, so
// an allocator here only has to hand out addresses; the fault handler makes
// them usable. That is not how the console works, but it is enough to get the
// title through start up, and it keeps the bookkeeping small enough to reason
// about while everything else is still missing.

#include "kernel.h"
#include <atomic>

#include <cstdio>
#include <map>
#include <mutex>

#include <Windows.h>

namespace
{
    // Allocation type bits the title passes to NtAllocateVirtualMemory.
    constexpr uint32_t X_MEM_COMMIT      = 0x00001000;
    constexpr uint32_t X_MEM_RESERVE     = 0x00002000;
    constexpr uint32_t X_MEM_DECOMMIT    = 0x00004000;
    constexpr uint32_t X_MEM_RELEASE     = 0x00008000;
    constexpr uint32_t X_MEM_LARGE_PAGES = 0x20000000;
    constexpr uint32_t X_MEM_16MB_PAGES  = 0x80000000;

    constexpr uint32_t X_STATUS_SUCCESS               = 0x00000000;
    constexpr uint32_t X_STATUS_NO_MEMORY             = 0xC0000017;
    constexpr uint32_t X_STATUS_INVALID_PARAMETER     = 0xC000000D;

    constexpr uint32_t SmallPage = 4u << 10;
    constexpr uint32_t LargePage = 64u << 10;

    uint32_t Align(uint32_t value, uint32_t to) { return (value + to - 1) & ~(to - 1); }

    struct Region { uint32_t size; bool physical; };

    // Every live allocation, so a free can report a size and the address
    // space does not silently leak into a wrong answer later.
    // Allocation happens on every guest thread, so the table and the bump
    // pointers need a lock of their own.
    std::mutex g_allocatorMutex;

    std::map<uint32_t, Region>& Regions()
    {
        static std::map<uint32_t, Region> regions;
        return regions;
    }

    uint32_t g_virtualNext = Guest::VirtualHeapBase;
    uint32_t g_physicalNext = Guest::PhysicalHeapBase;

    // A bump allocator. Freed address space is not reused: the title allocates
    // a handful of large blocks at start up and this keeps every guest pointer
    // unique, which makes a stale one obvious instead of aliasing a live
    // allocation. Replace it when a real heap is needed.
    uint32_t Take(uint32_t size, uint32_t alignment, bool physical)
    {
        uint32_t& next = physical ? g_physicalNext : g_virtualNext;
        const uint32_t limit = physical ? Guest::PhysicalHeapLimit : Guest::VirtualHeapLimit;

        const uint32_t address = Align(next, alignment);
        if (address < next || address + size < address || address + size > limit)
            return 0;

        next = address + size;
        Regions()[address] = { size, physical };
        Kernel::Stats().bytesAllocated.fetch_add(size, std::memory_order_relaxed);
        return address;
    }
}

// NTSTATUS NtAllocateVirtualMemory(PVOID* base, SIZE_T* size, ULONG type,
//                                  ULONG protect, ULONG unknown)
PPC_FUNC(__imp__NtAllocateVirtualMemory)
{
    Kernel::CountImport("NtAllocateVirtualMemory");
    struct Refused
    {
        PPCContext& ctx;
        uint32_t out;
        ~Refused()
        {
            if (ctx.r3.u32 == 0) return;   // success is zero
            printf("memory: allocation refused, status 0x%08X, from 0x%08X\n",
                ctx.r3.u32, uint32_t(ctx.lr));
            fflush(stdout);
        }
    } refused{ ctx, 0 };
    std::lock_guard<std::mutex> lock(g_allocatorMutex);

    const uint32_t baseAddressPtr = ctx.r3.u32;
    const uint32_t sizePtr = ctx.r4.u32;
    const uint32_t allocationType = ctx.r5.u32;

    if (baseAddressPtr == 0 || sizePtr == 0)
    {
        ctx.r3.u32 = X_STATUS_INVALID_PARAMETER;
        return;
    }

    const uint32_t requested = Guest::Read32(base, baseAddressPtr);
    const uint32_t requestedSize = Guest::Read32(base, sizePtr);

    const uint32_t pageSize =
        (allocationType & (X_MEM_LARGE_PAGES | X_MEM_16MB_PAGES)) ? LargePage : SmallPage;
    const uint32_t size = Align(requestedSize, pageSize);
    if (size == 0)
    {
        ctx.r3.u32 = X_STATUS_INVALID_PARAMETER;
        return;
    }

    uint32_t address;
    if (requested != 0)
    {
        // The title picked an address. Everything is already reserved, so
        // honour it and record the region.
        address = requested & ~(pageSize - 1);
        Regions()[address] = { size, false };
    }
    else
    {
        address = Take(size, pageSize, false);
        if (address == 0)
        {
            fprintf(stderr, "NtAllocateVirtualMemory: out of guest address space "
                            "for %u bytes\n", size);
            ctx.r3.u32 = X_STATUS_NO_MEMORY;
            return;
        }
    }

    // MEM_COMMIT means the title expects to touch it now, and expects zeroes.
    // The fault handler would commit lazily, but zeroing here matches what the
    // console guarantees and turns a use-before-write into a predictable zero
    // rather than whatever the last user of the page left behind.
    if (allocationType & X_MEM_COMMIT)
    {
        if (VirtualAlloc(Guest::Ptr(address), size, MEM_COMMIT, PAGE_READWRITE) != nullptr)
            memset(Guest::Ptr(address), 0, size);
    }

    Guest::Write32(base, baseAddressPtr, address);
    Guest::Write32(base, sizePtr, size);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtFreeVirtualMemory(PVOID* base, SIZE_T* size, ULONG type, ULONG)
PPC_FUNC(__imp__NtFreeVirtualMemory)
{
    Kernel::CountImport("NtFreeVirtualMemory");
    std::lock_guard<std::mutex> lock(g_allocatorMutex);

    const uint32_t baseAddressPtr = ctx.r3.u32;
    const uint32_t sizePtr = ctx.r4.u32;
    const uint32_t freeType = ctx.r5.u32;

    if (baseAddressPtr == 0)
    {
        ctx.r3.u32 = X_STATUS_INVALID_PARAMETER;
        return;
    }

    const uint32_t address = Guest::Read32(base, baseAddressPtr);
    auto found = Regions().find(address);
    const uint32_t size = found != Regions().end() ? found->second.size : 0;

    if (freeType & X_MEM_RELEASE)
    {
        // Address space is never handed back, so a release only forgets the
        // region. Decommitting would return the memory but also make a stale
        // guest pointer fault in the demand handler and be silently recommitted.
        if (found != Regions().end())
            Regions().erase(found);
    }
    else if ((freeType & X_MEM_DECOMMIT) && size != 0)
    {
        // Zeroing is what a decommit looks like from the guest's side, but it
        // is also the one place this runtime erases memory it was not asked to
        // erase if the recorded size is wrong.
        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 4)
        {
            printf("memory: decommit zeroes 0x%08X to 0x%08X\n",
                address, address + size);
            fflush(stdout);
        }
        memset(Guest::Ptr(address), 0, size);
    }

    if (sizePtr != 0)
        Guest::Write32(base, sizePtr, size);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// PVOID MmAllocatePhysicalMemoryEx(ULONG flags, SIZE_T size, ULONG protect,
//                                  ULONG minAddress, ULONG maxAddress,
//                                  ULONG alignment)
PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx)
{
    Kernel::CountImport("MmAllocatePhysicalMemoryEx");
    std::lock_guard<std::mutex> lock(g_allocatorMutex);

    const uint32_t size = Align(ctx.r4.u32, LargePage);
    uint32_t alignment = ctx.r8.u32;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        alignment = LargePage;

    const uint32_t address = Take(size, alignment, true);
    if (address == 0)
    {
        fprintf(stderr, "MmAllocatePhysicalMemoryEx: out of physical space for %u bytes\n", size);
        ctx.r3.u32 = 0;
        return;
    }

    if (VirtualAlloc(Guest::Ptr(address), size, MEM_COMMIT, PAGE_READWRITE) != nullptr)
        memset(Guest::Ptr(address), 0, size);

    ctx.r3.u32 = address;
}

// VOID MmFreePhysicalMemory(ULONG type, PVOID address)
PPC_FUNC(__imp__MmFreePhysicalMemory)
{
    Kernel::CountImport("MmFreePhysicalMemory");
    {
        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 4)
        {
            printf("memory: physical free of 0x%08X, from 0x%08X\n",
                ctx.r4.u32, uint32_t(ctx.lr));
            fflush(stdout);
        }
    }
    std::lock_guard<std::mutex> lock(g_allocatorMutex);

    const uint32_t address = ctx.r4.u32;
    auto found = Regions().find(address);
    if (found != Regions().end())
        Regions().erase(found);
}

// ULONG MmGetPhysicalAddress(PVOID address)
PPC_FUNC(__imp__MmGetPhysicalAddress)
{
    Kernel::CountImport("MmGetPhysicalAddress");
    // There is no MMU here and no GPU to hand an address to, so a guest
    // pointer is its own physical address. This has to change the moment
    // anything outside the CPU reads guest memory.
    ctx.r3.u32 = ctx.r3.u32;
}

// ULONG MmQueryAllocationSize(PVOID address)
PPC_FUNC(__imp__MmQueryAllocationSize)
{
    Kernel::CountImport("MmQueryAllocationSize");
    std::lock_guard<std::mutex> lock(g_allocatorMutex);

    auto found = Regions().find(ctx.r3.u32);
    ctx.r3.u32 = found != Regions().end() ? found->second.size : 0;
}

// ULONG MmQueryAddressProtect(PVOID address)
PPC_FUNC(__imp__MmQueryAddressProtect)
{
    Kernel::CountImport("MmQueryAddressProtect");
    // This has to tell the truth. The title walks the address space 64 KB at a
    // time asking about each block and stops when one comes back inaccessible,
    // so a runtime that answers "writable" for everything sends it round that
    // loop forever. The host already knows which pages are committed.
    const uint32_t address = ctx.r3.u32;

    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(Guest::Ptr(address), &information, sizeof(information)) == 0 ||
        information.State != MEM_COMMIT)
    {
        ctx.r3.u32 = 0;   // nothing mapped here
        return;
    }

    ctx.r3.u32 = 0x04;    // PAGE_READWRITE
}
