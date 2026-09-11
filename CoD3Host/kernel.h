#pragma once

// Shared declarations for the host side of the recompilation.
//
// ppc_recomp_shared.h brings in ppc_config.h and ppc_context.h, which is what
// gives PPC_FUNC_IMPL, PPCContext and the guest memory macros. Including it
// here means the kernel stubs are declared exactly the way the recompiled code
// calls them, so the linker matches them up.
#include "ppc_recomp_shared.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <string>
#include <filesystem>
#include <mutex>
#include <vector>

namespace Kernel
{
    // One kernel import: the name the XEX records for it, and the host
    // function that stands in for it.
    struct Import
    {
        const char* name;
        PPCFunc*    host;
    };

    // Called by every unimplemented import. Reports which one the game reached
    // and how it got there, then stops. It never returns: continuing past a
    // missing kernel call would corrupt guest state and produce a crash far
    // from the real cause.
    [[noreturn]] void Unimplemented(const char* name, PPCContext& ctx, uint8_t* base);

    // Ends the process. When the console belongs to this program, which is the
    // case when it was started from Explorer rather than from a shell, it waits
    // for a key first: otherwise the window closes the instant the process ends
    // and everything printed goes with it.
    [[noreturn]] void Exit(int code);

    // Prepares the saves folder. Reads resolve against Guest::GameRoot, which
    // the installer has already located.
    void InitializeFileSystem(const std::filesystem::path& exeDirectory);

    // NtClose serves both the object table and the file table. This lets the
    // object implementation hand a handle it does not recognise to the file
    // layer before reporting it invalid.
    // Every mutant a thread still owned when it ended, handed back. The
    // console does this; without it the next waiter waits for a thread that
    // has gone.
    void AbandonMutants(uint32_t osThreadId);

    bool CloseFileHandle(uint32_t handle);

    // Bytes actually read, per file, and a report of the busiest ones.
    void CountFileRead(const std::string& guestPath, uint32_t bytes);
    void ReportFileReads();

    // One lock and one condition variable cover every kernel object's signal
    // state, whether it lives in the handle table or in guest memory. A single
    // pair is coarse, but it makes the ordering between a signal and a wait
    // impossible to get wrong, which matters far more here than contention.
    // A guest thread, for the stack sampler.
    struct ThreadSample
    {
        void* handle = nullptr;
        uint32_t id = 0;
        uint32_t osId = 0;   // so a stack can be matched to a lock owner
    };
    std::vector<ThreadSample> SampleGuestThreads();

    // The thread that entered the guest is not one ExCreateThread made, so it
    // has to be registered by hand or the sampler cannot see the one thread
    // that has been running longest.
    void RegisterEntryThread();

    // Reports an object guest threads keep waiting on that nothing signals.
    void ReportWaitTraffic();

    std::mutex& DispatcherLock();
    std::condition_variable& DispatcherChanged();

    // A heartbeat, so a run that neither stops nor draws anything still shows
    // whether the title is doing work. Without a picture there is no other way
    // to tell progress from a spin.
    struct Counters
    {
        std::atomic<uint64_t> frames{ 0 };
        std::atomic<uint64_t> filesOpened{ 0 };
        std::atomic<uint64_t> fileBytesRead{ 0 };
        std::atomic<uint64_t> threadsCreated{ 0 };
        std::atomic<uint64_t> bytesAllocated{ 0 };
        std::atomic<uint64_t> audioFrames{ 0 };
    };
    Counters& Stats();

    // Which kernel calls the title is making, and how often.
    //
    // A title that has stopped making progress is almost always waiting on
    // something, and what it calls while it waits names what it is waiting for.
    // There is no central place imports are dispatched, so each one counts
    // itself on the way in.
    void CountImport(const char* name);

    // The same, with the call's first argument. A loop of waits says nothing
    // until you know which object each wait was on.
    void CountImportOn(const char* name, uint32_t subject);
    void ReportImports();

    // Text the title formatted for itself, which is the closest thing it has
    // to telling this runtime what it is doing.
    void ReportGuestText(const std::string& text);

    // The last few kernel calls each thread made, in order. A thread that has
    // stopped making progress is repeating something, and the repetition names
    // what it is waiting for far better than a count does.
    void ReportRecentCalls(uint32_t onlyThread = 0);

    // A hardware watch on one word of guest memory. The processor raises an
    // exception on the instruction that writes it, which is the only way to
    // find out who changed a value that used to be right.
    void WatchWrite(uint32_t guestAddress);
    void ArmWatchpoints();
    bool ReportWatchpoint(void* winContext);
    bool SelfTestWatchpoint();

    // Signals an event by handle, for a kernel call that completes work the
    // title asked to be told about. Returns false when the handle is not one.
    bool SignalHandle(uint32_t handle);

    // The guest memory a thread handle refers to, and the signal that a thread
    // has ended so a wait on it completes.
    uint32_t ThreadObjectFor(uint32_t handle);

    // "os 1234, started at 0x82527B70" for a thread handle, empty otherwise.
    std::string DescribeThreadHandle(uint32_t handle);
    void SignalThreadExit(uint32_t osId);
    void RecordThreadPointer(uint32_t osId, uint32_t block);

    // Completion routines for asynchronous work: queued against the thread
    // that asked, run when that thread next waits.
    void QueueApc(uint32_t routine, uint32_t context, uint32_t statusBlock,
                  uint32_t status, uint32_t information);
    // Completion routines run when the thread waits and says it may be
    // interrupted. A thread that waits without saying so is not expecting its
    // own code to run underneath it, and running it there turns a sequence the
    // title wrote into a reentrant one. They are delivered anyway once they
    // have waited long enough that something is clearly wrong.
    void DeliverApcs(PPCContext& ctx, bool alertable);
    void ReportApcs();

    // The audio render callback is driven from a thread of its own.
    void StopAudioPump();
    uint64_t AudioCallbacks();
}

namespace Guest
{
    // Guest memory is big endian. Every read and write of a guest structure
    // goes through these, so the byte order is never left to chance.
    inline uint32_t Read32(const uint8_t* base, uint32_t address)
    {
        return __builtin_bswap32(*reinterpret_cast<const volatile uint32_t*>(base + address));
    }
    // The same compare and exchange the recompiled code uses, so a lock the
    // title takes inline and a lock it takes through the kernel are the same
    // lock.
    inline bool CompareExchange32(uint8_t* base, uint32_t address,
                                  uint32_t expected, uint32_t desired)
    {
        return __sync_bool_compare_and_swap(
            reinterpret_cast<uint32_t*>(base + address),
            __builtin_bswap32(expected), __builtin_bswap32(desired));
    }

    // Adds to a guest word atomically and returns the new value, in the byte
    // order the guest sees.
    inline int32_t AtomicAdd32(uint8_t* base, uint32_t address, int32_t delta)
    {
        uint32_t* word = reinterpret_cast<uint32_t*>(base + address);
        for (;;)
        {
            const uint32_t raw = __atomic_load_n(word, __ATOMIC_ACQUIRE);
            const int32_t next = int32_t(__builtin_bswap32(raw)) + delta;
            if (__sync_bool_compare_and_swap(word, raw,
                    __builtin_bswap32(uint32_t(next))))
                return next;
        }
    }

    inline void Write32(uint8_t* base, uint32_t address, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t*>(base + address) = __builtin_bswap32(value);
    }
    // A single byte has no order to get wrong, so it goes straight through.
    inline uint8_t Read8(const uint8_t* base, uint32_t address)
    {
        return *reinterpret_cast<const volatile uint8_t*>(base + address);
    }
    inline void Write8(uint8_t* base, uint32_t address, uint8_t value)
    {
        *reinterpret_cast<volatile uint8_t*>(base + address) = value;
    }
    inline uint16_t Read16(const uint8_t* base, uint32_t address)
    {
        return __builtin_bswap16(*reinterpret_cast<const volatile uint16_t*>(base + address));
    }
    inline void Write16(uint8_t* base, uint32_t address, uint16_t value)
    {
        *reinterpret_cast<volatile uint16_t*>(base + address) = __builtin_bswap16(value);
    }
    inline uint64_t Read64(const uint8_t* base, uint32_t address)
    {
        return __builtin_bswap64(*reinterpret_cast<const volatile uint64_t*>(base + address));
    }
    inline void Write64(uint8_t* base, uint32_t address, uint64_t value)
    {
        *reinterpret_cast<volatile uint64_t*>(base + address) = __builtin_bswap64(value);
    }

    // Guest pointers are 32 bit, so the whole 4 GB has to be addressable: the
    // title maps its heap and physical memory well above the image. Reserved
    // in one block, committed a page at a time as it is touched.
    inline constexpr uint64_t TotalSize = 0x100000000ull;

    // PPC_LOOKUP_FUNC hard codes where the indirect call table lives: directly
    // after the image, inside the guest address space rather than beyond it.
    // That region is therefore not available to the guest.
    inline constexpr uint64_t FuncTableBase = PPC_IMAGE_BASE + PPC_IMAGE_SIZE;
    inline constexpr uint64_t FuncTableSize = PPC_CODE_SIZE * 2;
    inline constexpr uint64_t FuncTableEnd  = FuncTableBase + FuncTableSize;

    // Guest stack, placed at the top of the virtual allocation region.
    inline constexpr uint32_t StackBase = 0x70000000;
    inline constexpr uint32_t StackSize = 1u << 20;

    // Where kernel allocations come from. The title asks for virtual memory
    // through Nt* and for physical memory through Mm*, and on the console
    // those live in different parts of the map. Handing out addresses from
    // separate ranges keeps that distinction visible when debugging.
    inline constexpr uint32_t VirtualHeapBase  = 0x40000000;
    inline constexpr uint32_t VirtualHeapLimit = 0x6F000000;   // stops below the stack
    inline constexpr uint32_t PhysicalHeapBase  = 0xA0000000;
    inline constexpr uint32_t PhysicalHeapLimit = 0xC0000000;

    extern uint8_t* Base;

    // Where the installed game lives on the host. Guest paths such as
    // game:\sp\global.cod resolve underneath this.
    extern std::filesystem::path GameRoot;

    bool Initialize(const char* xexPath);
    void Shutdown();

    inline uint8_t* Ptr(uint32_t guestAddress) { return Base + guestAddress; }

    // Physical memory is visible through several windows on this hardware: a
    // physical address P can be reached at P itself and at 0xA0000000,
    // 0xC0000000 and 0xE0000000 plus P, and they are all the same bytes. This
    // runtime keeps them as separate memory, so anything the GPU writes for the
    // title to read has to go to each of them or the title reads a stale copy.
    // Where a physical address is readable in this runtime's flat memory.
    inline uint32_t PhysicalAlias(uint32_t physicalAddress)
    {
        return 0xA0000000u | (physicalAddress & 0x1FFFFFFF);
    }

    inline void WritePhysical32(uint32_t physicalAddress, uint32_t value)
    {
        const uint32_t offset = physicalAddress & 0x1FFFFFFF;
        Write32(Base, offset, value);
        Write32(Base, 0xA0000000u | offset, value);
        Write32(Base, 0xC0000000u | offset, value);
        Write32(Base, 0xE0000000u | offset, value);
    }

    // Turns a guest code address into the recompiled function behind it, the
    // same way PPC_LOOKUP_FUNC does inside the translated code. Returns null
    // when nothing was recompiled at that address.
    inline PPCFunc* Lookup(uint32_t guestAddress)
    {
        if (guestAddress < PPC_CODE_BASE || guestAddress >= PPC_CODE_BASE + PPC_CODE_SIZE)
            return nullptr;
        return *reinterpret_cast<PPCFunc**>(
            Base + FuncTableBase + (uint64_t(guestAddress - PPC_CODE_BASE) * 2));
    }

    // Reserves a stack for a new guest thread and returns the address to put
    // in r1. Zero means the address space is exhausted.
    uint32_t AllocateStack(uint32_t size);

    // The block r13 points at. Guest code reads its thread id, its last error
    // and a millisecond timestamp out of this rather than calling the kernel,
    // so every thread that runs guest code needs one.
    // The processor argument is which of the console's six hardware threads
    // this one runs on. It is not cosmetic: guest code reads it out of the
    // block and uses it as an index.
    uint32_t CreateThreadPointer(uint32_t threadId, int processor = -1);

    // Which hardware thread the caller is on, read out of its own thread block.
    int CurrentProcessor(const PPCContext& ctx);

    // Changes which of the six hardware threads a context says it is on.
    void SetProcessor(const PPCContext& ctx, int processor);

    // A small block of guest memory for a kernel object's dispatcher header.
    // Objects the title reaches through a handle also have to exist as memory,
    // because it can turn a handle into a pointer and wait on that instead.
    uint32_t AllocateKernelObject(uint32_t size);
    void StopThreadClock();

    // Hands a thread pointer block back when its thread has ended.
    void ReleaseThreadPointer(uint32_t block);
}
