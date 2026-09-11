#include "kernel.h"
#include "scheduler.h"
#include <atomic>
#include "sampler.h"
#include <utility>
#include <mutex>
#include <map>
#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <Windows.h>

#include <file.h>
#include <image.h>

namespace Guest
{
    uint8_t* Base = nullptr;
    std::filesystem::path GameRoot;
}

namespace
{
    void* g_reservation = nullptr;
    uint64_t g_demandCommitted = 0;

    // Guest memory is reserved in one block but committed lazily. The title
    // allocates through kernel calls that do not exist yet, so there is no way
    // to know in advance which pages it will touch. Faulting a page in on
    // first touch stands in until the Nt and Mm imports do it properly. The
    // limit matches the console's 512 MB, so a runaway pointer stops instead
    // of quietly filling memory with zeroed pages.
    constexpr uint64_t DemandCommitGranularity = 64u << 10;
    // Two gigabytes. Half a gigabyte was the console's own memory and looked
    // like the right ceiling, but physical memory is written through four
    // windows here and each one costs its own pages, so the real figure is
    // several times what the title thinks it is using.
    constexpr uint64_t DemandCommitLimit = 2048ull << 20;

    const char* AccessKind(ULONG_PTR type)
    {
        switch (type)
        {
        case 0:  return "read";
        case 1:  return "write";
        case 8:  return "execute";
        default: return "access";
        }
    }

    LONG CALLBACK GuestFaultHandler(EXCEPTION_POINTERS* info)
    {
        const auto* record = info->ExceptionRecord;

        // A hardware watch fires as a single step. The processor has already
        // done the write, so reporting and continuing is all that is wanted.
        if (record->ExceptionCode == EXCEPTION_SINGLE_STEP)
        {
            if (Kernel::ReportWatchpoint(info->ContextRecord))
                return EXCEPTION_CONTINUE_EXECUTION;
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Anything else that reaches here kills the process without a word,
        // which is the worst way for a run to end: no message, no stack, no
        // idea. Saying what it was and where costs nothing and is often the
        // whole answer.
        if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || Guest::Base == nullptr)
        {
            static std::atomic<int> announced{ 0 };
            if ((record->ExceptionCode & 0x20000000) == 0 &&
                announced.fetch_add(1) < 4)
            {
                printf("\n");
                uint32_t functions[12] = {};
                const int count =
                    Sampler::WalkGuestStack(info->ContextRecord, functions, 12);
                if (count > 0)
                {
                    printf("  guest call chain, innermost first:");
                    for (int i = 0; i < count; i++)
                        printf("%ssub_%08X", i == 0 ? " " : " <- ", functions[i]);
                    printf("\n");
                }

                const char* name = "an exception";
                switch (record->ExceptionCode)
                {
                case EXCEPTION_STACK_OVERFLOW:    name = "a stack overflow"; break;
                case EXCEPTION_ILLEGAL_INSTRUCTION: name = "an illegal instruction"; break;
                case EXCEPTION_INT_DIVIDE_BY_ZERO: name = "a divide by zero"; break;
                case EXCEPTION_PRIV_INSTRUCTION:  name = "a privileged instruction"; break;
                case EXCEPTION_IN_PAGE_ERROR:     name = "an in page error"; break;
                case 0xE06D7363:                  name = "a C++ throw"; break;
                default: break;
                }
                printf("Unhandled: %s, code 0x%08lX at %p\n", name,
                    record->ExceptionCode, record->ExceptionAddress);

                // What was thrown.
                //
                // A C++ throw carries a description of the type with it, and
                // that description ends in the type's own name. Following it
                // turns "the game raised an exception" into the sentence the
                // programmer wrote, which is the difference between guessing
                // at a dirty disc and knowing what was actually wrong.
                if (record->ExceptionCode == 0xE06D7363 &&
                    record->NumberParameters >= 3)
                {
                    const uint32_t throwInfo =
                        uint32_t(record->ExceptionInformation[2]);
                    if (throwInfo >= 0x82000000u && throwInfo < 0x82C30000u)
                    {
                        const uint32_t array =
                            Guest::Read32(Guest::Base, throwInfo + 12);
                        if (array >= 0x82000000u && array < 0x82C30000u)
                        {
                            const uint32_t count =
                                Guest::Read32(Guest::Base, array + 0);
                            for (uint32_t i = 0; i < count && i < 4; i++)
                            {
                                const uint32_t catchable =
                                    Guest::Read32(Guest::Base, array + 4 + i * 4);
                                if (catchable < 0x82000000u ||
                                    catchable >= 0x82C30000u) continue;
                                const uint32_t descriptor =
                                    Guest::Read32(Guest::Base, catchable + 4);
                                if (descriptor < 0x82000000u ||
                                    descriptor >= 0x82C30000u) continue;

                                char text[128];
                                uint32_t at = 0;
                                for (; at + 1 < sizeof(text); at++)
                                {
                                    const uint8_t byte = Guest::Read8(
                                        Guest::Base, descriptor + 8 + at);
                                    if (byte == 0) break;
                                    text[at] = char(byte);
                                }
                                text[at] = 0;
                                if (at > 0)
                                    printf("  what was thrown: %s\n", text);
                            }
                        }
                    }
                }
                fflush(stdout);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto faulting = static_cast<uint8_t*>(
            reinterpret_cast<void*>(record->ExceptionInformation[1]));

        if (faulting < Guest::Base || faulting >= Guest::Base + Guest::TotalSize)
        {
            // Outside guest memory entirely. The usual cause is an indirect
            // call through a table entry that was never filled in, which lands
            // on a null pointer.
            printf("\n");
            {
                uint32_t functions[12] = {};
                const int count =
                    Sampler::WalkGuestStack(info->ContextRecord, functions, 12);
                if (count > 0)
                {
                    printf("\n  guest call chain, innermost first:");
                    for (int i = 0; i < count; i++)
                        printf("%ssub_%08X", i == 0 ? " " : " <- ", functions[i]);
                    printf("\n");
                }
            }
            printf("Host fault: %s at %p, outside the guest address space.\n"
                   "  guest memory runs from %p to %p\n",
                AccessKind(record->ExceptionInformation[0]), (void*)faulting,
                (void*)Guest::Base, (void*)(Guest::Base + Guest::TotalSize));
            // An indirect call reads the function table, which sits at a fixed
            // offset from the base, so the address it read says which guest
            // address it was looking for. A fault below the base is that
            // arithmetic going negative, which only happens for an address
            // below the start of the code: nearly always zero.
            const int64_t offset = int64_t(faulting - Guest::Base);

            // Just past the end of guest memory is a guest address near
            // 0xFFFFFFFF, which is a small negative number being used as a
            // pointer: an error code returned where an object was expected.
            if (offset >= int64_t(Guest::TotalSize) &&
                offset < int64_t(Guest::TotalSize) + 0x1000)
            {
                printf("  That is the very top of the address space, reached by an "
                       "access that started just below 0xFFFFFFFF: a small\n"
                       "  negative number used as a pointer, so something handed back\n"
                       "  an error code where the caller expected an object.\n");
            }

            const int64_t tableStart = int64_t(Guest::FuncTableBase);
            const int64_t implied =
                int64_t(PPC_CODE_BASE) + ((offset - tableStart) / 2);
            if (implied >= -0x1000 && implied < int64_t(PPC_CODE_BASE) + 0x1000000)
            {
                printf("  This is the function table being read for guest address "
                       "0x%08X.\n", uint32_t(implied));
                if (implied == 0)
                {
                    printf("  The guest called through a null function pointer: an "
                           "object it\n  expected to be set up was not. The call "
                           "chain above says which one.\n");
                }
                else
                {
                    printf("  No recompiled function stands behind that address.\n");
                }
            }
            else if (reinterpret_cast<uintptr_t>(faulting) < 0x10000)
            {
                printf("  A near null address on an %s means an indirect call through\n",
                    AccessKind(record->ExceptionInformation[0]));
                printf("  an empty slot in the function table: the guest asked for a\n");
                printf("  code address that has no recompiled function behind it.\n");
            }
            printf("  %.1f MB of guest memory had been committed on demand.\n",
                g_demandCommitted / 1048576.0);
            fflush(stdout);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const uint64_t offset = static_cast<uint64_t>(faulting - Guest::Base);
        const uint64_t block = offset & ~(DemandCommitGranularity - 1);

        // The first page of the address space is not memory on this console,
        // and an access there is a null pointer being followed. Committing it
        // on demand hands back zeros and lets the guest carry on with a null
        // object, which it then does for a long time before dying somewhere
        // that says nothing about the cause. Refusing it puts the report at
        // the first mistake instead of the last symptom.
        if (g_demandCommitted < DemandCommitLimit)
        {
            // Sixty four kilobytes at a time, because that is the granularity
            // the console hands memory out in and it keeps the number of
            // commits down. A block that straddles something already mapped
            // differently is refused whole, so the page on its own is tried
            // next: the aim is to keep the guest running, not to be tidy.
            if (VirtualAlloc(Guest::Base + block, DemandCommitGranularity,
                             MEM_COMMIT, PAGE_READWRITE) != nullptr)
            {
                g_demandCommitted += DemandCommitGranularity;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            const uint64_t page = offset & ~uint64_t(0xFFF);
            if (VirtualAlloc(Guest::Base + page, 0x1000,
                             MEM_COMMIT, PAGE_READWRITE) != nullptr)
            {
                g_demandCommitted += 0x1000;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        printf("\n");
        printf("Guest fault: %s at guest address 0x%08llX, which could not be "
               "committed (error %lu)\n",
            AccessKind(record->ExceptionInformation[0]),
            (unsigned long long)offset, GetLastError());
        printf("  %.1f MB had already been committed on demand.\n",
            g_demandCommitted / 1048576.0);
        fflush(stdout);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // The first pages are physical memory on this console, and the graphics
    // driver puts fences there: one of the first is written to physical
    // address 0x100. Committing them up front keeps that off the fault path.
    void CommitLowMemory()
    {
        VirtualAlloc(Guest::Base, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    }

    bool Commit(uint64_t offset, uint64_t size, const char* what)
    {
        // Round out to page boundaries so adjacent commits cannot leave a hole.
        const uint64_t begin = offset & ~uint64_t(0xFFF);
        const uint64_t end = (offset + size + 0xFFF) & ~uint64_t(0xFFF);

        if (VirtualAlloc(Guest::Base + begin, end - begin, MEM_COMMIT, PAGE_READWRITE) == nullptr)
        {
            fprintf(stderr, "  failed to commit %s at 0x%llX (%llu bytes): error %lu\n",
                what, (unsigned long long)begin, (unsigned long long)(end - begin), GetLastError());
            return false;
        }
        return true;
    }
}

bool Guest::Initialize(const char* xexPath)
{
    printf("Guest address space\n");
    printf("  image        0x%08llX .. 0x%08llX\n",
        (unsigned long long)PPC_IMAGE_BASE,
        (unsigned long long)(PPC_IMAGE_BASE + PPC_IMAGE_SIZE));
    printf("  code         0x%08llX .. 0x%08llX\n",
        (unsigned long long)PPC_CODE_BASE,
        (unsigned long long)(PPC_CODE_BASE + PPC_CODE_SIZE));
    printf("  call table   0x%08llX .. 0x%08llX\n",
        (unsigned long long)FuncTableBase, (unsigned long long)FuncTableEnd);
    printf("  reserving    %.0f GB of address space\n", TotalSize / 1073741824.0);

    // One contiguous reservation covering the whole 32 bit guest range, so any
    // guest pointer resolves to base + address. Reserve everything, commit
    // only what is actually touched: committing 4 GB up front would cost real
    // memory for pages the guest never uses.
    g_reservation = VirtualAlloc(nullptr, TotalSize, MEM_RESERVE, PAGE_NOACCESS);
    if (g_reservation == nullptr)
    {
        fprintf(stderr, "  reservation failed: error %lu\n", GetLastError());
        return false;
    }
    Base = static_cast<uint8_t*>(g_reservation);

    // Physical memory is reachable through four windows on this console, and a
    // write through one is visible through all of them. Here they are separate
    // storage, and only writes this runtime makes itself are mirrored. Making
    // them genuinely the same memory means one section mapped in four places,
    // which was tried: releasing the single reservation to make room for the
    // views left the address space in a state the loader could not use, and the
    // title stopped much earlier. It is a real gap and not the one that matters
    // yet, because the guest reads back through the window it wrote to.

    // PPC_FUNC_PROLOGUE asserts this, and the vector loads assume it.
    if ((reinterpret_cast<uintptr_t>(Base) & 0x1F) != 0)
    {
        fprintf(stderr, "  base %p is not 32 byte aligned\n", Base);
        return false;
    }

    AddVectoredExceptionHandler(1, GuestFaultHandler);
    CommitLowMemory();

    if (!Commit(PPC_IMAGE_BASE, PPC_IMAGE_SIZE, "image")) return false;
    if (!Commit(FuncTableBase, FuncTableSize, "function table")) return false;
    if (!Commit(StackBase - StackSize, StackSize, "stack")) return false;

    // --- Load the executable ---------------------------------------------
    printf("\nLoading %s\n", xexPath);
    const auto file = LoadFile(xexPath);
    if (file.empty())
    {
        fprintf(stderr, "  cannot read the file\n");
        return false;
    }

    auto image = Image::ParseImage(file.data(), file.size());
    printf("  entry point 0x%llX\n", (unsigned long long)image.entry_point);

    // The recompiled code reads guest data (string literals, tables, vtables)
    // straight out of this memory, so every section has to land at its own
    // virtual address, big-endian bytes untouched.
    size_t mapped = 0;
    size_t skipped = 0;
    for (const auto& section : image.sections)
    {
        // The indirect call table sits immediately after the image, and
        // .reloc runs into it. Skipping .reloc is safe: base relocations are
        // applied only by a loader placing an image somewhere other than its
        // preferred base, and this one is at its preferred base by
        // construction. Any other section landing there is a real problem and
        // must not be dropped quietly.
        if (section.base < FuncTableEnd && section.base + section.size > FuncTableBase)
        {
            if (section.name == ".reloc")
            {
                skipped++;
                continue;
            }
            fprintf(stderr, "  section %s (0x%zX .. 0x%zX) overlaps the call table "
                            "at 0x%llX\n",
                section.name.c_str(), section.base, section.base + section.size,
                (unsigned long long)FuncTableBase);
            return false;
        }
        if (section.base + section.size > TotalSize)
        {
            fprintf(stderr, "  section %s ends at 0x%zX, past the guest address space\n",
                section.name.c_str(), section.base + section.size);
            return false;
        }
        memcpy(Base + section.base, section.data, section.size);
        mapped += section.size;
    }
    printf("  mapped %zu sections, %.2f MB", image.sections.size() - skipped, mapped / 1048576.0);
    if (skipped > 0) printf(" (.reloc not needed, skipped)");
    printf("\n");

    // --- Indirect call table ----------------------------------------------
    // PPC_LOOKUP_FUNC turns a guest code address into a host function pointer
    // by indexing this table at (address - PPC_CODE_BASE) * 2.
    size_t entries = 0;
    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        const size_t guest = PPCFuncMappings[i].guest;
        if (guest < PPC_CODE_BASE || guest >= PPC_CODE_BASE + PPC_CODE_SIZE)
        {
            fprintf(stderr, "  function 0x%zX is outside the code range\n", guest);
            return false;
        }
        auto** slot = reinterpret_cast<PPCFunc**>(
            Base + FuncTableBase + (uint64_t(uint32_t(guest) - PPC_CODE_BASE) * 2));
        *slot = PPCFuncMappings[i].host;
        entries++;
    }
    printf("  %zu functions in the indirect call table\n", entries);

    // Import thunks are not recompiled functions, so they are absent from
    // PPCFuncMappings. A direct branch to one is turned into a call to the
    // host stub at recompile time, but a call through a function pointer goes
    // through the table at run time and would find a null entry. Fill those in
    // by name, using the symbols the XEX itself carries.
    static const Kernel::Import kImports[] = {
#include "kernel_import_table.inc"
    };

    size_t importsBound = 0;
    size_t importsUnbound = 0;
    for (const auto& symbol : image.symbols)
    {
        if (symbol.name.empty()) continue;

        std::string name = symbol.name;
        const std::string prefix = "__imp__";
        if (name.rfind(prefix, 0) == 0) name = name.substr(prefix.size());

        PPCFunc* host = nullptr;
        for (const auto& import : kImports)
        {
            if (name == import.name) { host = import.host; break; }
        }

        if (host == nullptr)
        {
            fprintf(stderr, "  no host stub for import %s\n", name.c_str());
            importsUnbound++;
            continue;
        }
        if (symbol.address < PPC_CODE_BASE || symbol.address >= PPC_CODE_BASE + PPC_CODE_SIZE)
        {
            fprintf(stderr, "  import %s at 0x%zX is outside the code range\n",
                name.c_str(), symbol.address);
            importsUnbound++;
            continue;
        }

        auto** slot = reinterpret_cast<PPCFunc**>(
            Base + FuncTableBase + (uint64_t(uint32_t(symbol.address) - PPC_CODE_BASE) * 2));
        *slot = host;
        importsBound++;
    }
    printf("  %zu kernel imports bound", importsBound);
    if (importsUnbound > 0) printf(", %zu could not be bound", importsUnbound);
    printf("\n");

    return true;
}

// Gives the address space back.
//
// Only safe when nothing else is running in it. A thread that fails does not
// get to do this: every other thread would then fault on memory that is no
// longer there, each fault reported as one that could not be committed, and the
// message that actually mattered scrolls away. Report and stop instead.
void Guest::Shutdown()
{
    if (g_reservation != nullptr)
    {
        VirtualFree(g_reservation, 0, MEM_RELEASE);
        g_reservation = nullptr;
        Base = nullptr;
    }
}

Kernel::Counters& Kernel::Stats()
{
    static Counters counters;
    return counters;
}

void Kernel::Exit(int code)
{
    // GetConsoleProcessList reports how many processes share this console.
    // One means the console was created for us, so nothing else will be left
    // to read the output after the process ends.
    DWORD processes[2] = {};
    const DWORD attached = GetConsoleProcessList(processes, 2);
    if (attached <= 1)
    {
        printf("\nPress Enter to close this window.\n");
        fflush(stdout);
        (void)getchar();
    }
    ExitProcess(static_cast<UINT>(code));
}

void Kernel::Unimplemented(const char* name, PPCContext& ctx, uint8_t* base)
{
    (void)base;

    printf("\n");
    printf("=====================================================================\n");
    printf(" The game called an Xbox 360 kernel function that does not exist yet\n");
    printf("=====================================================================\n");
    printf("\n");
    printf("  import   %s\n", name);
    printf("  link     0x%08X   (guest address it will return to)\n", uint32_t(ctx.lr));
    printf("\n");
    printf("  arguments, r3 first:\n");
    printf("    r3 = 0x%08X   r4 = 0x%08X   r5 = 0x%08X\n",
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
    printf("    r6 = 0x%08X   r7 = 0x%08X   r8 = 0x%08X\n",
        ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
    printf("\n");
    printf("  This is the expected result today. The recompiled game code is\n");
    printf("  complete, but the runtime beneath it is not: 179 kernel imports\n");
    printf("  are still stubs. Implement this one in CoD3Host/kernel.cpp,\n");
    printf("  remove its line from kernel_stubs.cpp, and run again to find the\n");
    printf("  next. See STATUS.md.\n");
    printf("\n");

    fflush(stdout);
    Guest::Shutdown();
    Kernel::Exit(1);
}

namespace
{
    std::mutex g_importMutex;
    std::map<const char*, uint64_t> g_importCounts;
}

namespace
{
    // A short history per thread, kept in a fixed ring so it costs one store.
    constexpr size_t HistoryLength = 24;

    struct History
    {
        const char* names[HistoryLength] = {};
        uint32_t subjects[HistoryLength] = {};
        size_t next = 0;
    };

    std::mutex g_historyMutex;
    std::map<uint32_t, History> g_histories;
}

void Kernel::CountImportOn(const char* name, uint32_t subject)
{
    // Every kernel call is a place a thread can hand its hardware thread over.
    // Putting the check here rather than in a handful of chosen imports is what
    // makes the sharing actually happen: a thread that only ever calls one
    // unremarkable function would otherwise keep the slot to itself.
    Scheduler::Checkpoint();

    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        History& history = g_histories[GetCurrentThreadId()];
        history.names[history.next % HistoryLength] = name;
        history.subjects[history.next % HistoryLength] = subject;
        history.next++;
    }

    std::lock_guard<std::mutex> lock(g_importMutex);
    g_importCounts[name]++;
}

void Kernel::CountImport(const char* name) { CountImportOn(name, 0); }

void Kernel::ReportRecentCalls(uint32_t onlyThread)
{
    std::lock_guard<std::mutex> lock(g_historyMutex);
    printf("recent kernel calls %s, oldest first:\n",
        onlyThread ? "on the thread that threw" : "per thread");
    for (const auto& entry : g_histories)
    {
        const History& history = entry.second;
        if (history.next == 0) continue;
        if (onlyThread != 0 && entry.first != onlyThread) continue;

        printf("  os %-6u:", entry.first);
        const size_t count = history.next < HistoryLength ? history.next : HistoryLength;
        const size_t first = history.next - count;
        for (size_t i = 0; i < count; i++)
        {
            const size_t slot = (first + i) % HistoryLength;
            const char* name = history.names[slot];
            printf(" %s", name ? name : "?");
            if (history.subjects[slot] != 0) printf("(%08X)", history.subjects[slot]);
        }
        printf("\n");
    }
    fflush(stdout);
}

void Kernel::ReportImports()
{
    std::vector<std::pair<uint64_t, const char*>> ordered;
    {
        std::lock_guard<std::mutex> lock(g_importMutex);
        for (const auto& entry : g_importCounts)
            ordered.push_back({ entry.second, entry.first });
    }
    if (ordered.empty()) return;

    std::sort(ordered.begin(), ordered.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    printf("kernel calls, busiest first:");
    for (size_t i = 0; i < ordered.size() && i < 80; i++)
        printf(" %s x%llu", ordered[i].second, (unsigned long long)ordered[i].first);
    printf("\n");
    fflush(stdout);
}
