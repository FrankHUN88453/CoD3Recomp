// The watchdog: what every host thread is doing when the runtime stops.
//
// A run that hangs says nothing, and the diagnostics that would say
// something are printed by the very threads that have stopped. This thread
// prints nothing until the vertical blank counter has not moved for a
// while, and then walks the native stack of every thread in the process
// with dbghelp, symbolised from the executable's own debug information.
// The lock one thread holds and another waits for is on those stacks.

#include "kernel.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include <Windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    std::atomic<bool> g_running{ false };
    std::thread g_thread;

    void PrintFrame(HANDLE process, DWORD64 pc);
    void WalkFrames(HANDLE process, HANDLE thread, CONTEXT& context);

    void PrintThreadStack(HANDLE process, DWORD threadId)
    {
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_INFORMATION, FALSE, threadId);
        if (thread == nullptr) return;

        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        bool captured = false;
        if (SuspendThread(thread) != DWORD(-1))
        {
            captured = GetThreadContext(thread, &context) != FALSE;
            ResumeThread(thread);
        }
        if (!captured) { CloseHandle(thread); return; }

        printf("  thread %5lu:", threadId);
        Kernel::SetUnwinding(true);
        WalkFrames(process, thread, context);
        Kernel::SetUnwinding(false);
        printf("\n");
        CloseHandle(thread);
    }

    void PrintFrame(HANDLE process, DWORD64 pc)
    {
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, pc, &displacement, symbol))
        {
            printf(" %s+0x%llx", symbol->Name, (unsigned long long)displacement);
            // The line, for the runtime's own code: a profile by symbol says
            // which function, and a lambda the size of the pixel loop needs
            // more than that.
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, pc, &lineDisplacement, &line))
            {
                const char* file = strrchr(line.FileName, '\\');
                printf("@%s:%lu", file != nullptr ? file + 1 : line.FileName, line.LineNumber);
            }
            // And the address within the module, for llvm-symbolizer, which
            // knows what was inlined where and this does not.
            if (const DWORD64 module = SymGetModuleBase64(process, pc))
                printf("@+%llx", (unsigned long long)(pc - module));
            return;
        }
        const DWORD64 module = SymGetModuleBase64(process, pc);
        char name[64] = "?";
        if (module != 0)
        {
            IMAGEHLP_MODULE64 info{};
            info.SizeOfStruct = sizeof(info);
            if (SymGetModuleInfo64(process, module, &info))
                snprintf(name, sizeof(name), "%s", info.ModuleName);
        }
        printf(" %s!%llx", name, (unsigned long long)(pc - module));
    }

    void WalkFrames(HANDLE process, HANDLE thread, CONTEXT& context)
    {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        __try
        {
            for (int depth = 0; depth < 24; depth++)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                        nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (frame.AddrPC.Offset == 0) break;
                PrintFrame(process, frame.AddrPC.Offset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            printf(" (walk faulted)");
        }
    }

    void LoadSymbols(HANDLE process)
    {
        static bool symbols = false;
        if (!symbols)
        {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
        }
    }

    void DumpAllThreads()
    {
        const HANDLE process = GetCurrentProcess();
        LoadSymbols(process);

        printf("\nwatchdog: host stacks of every thread:\n");
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        const DWORD self = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
            PrintThreadStack(process, entry.th32ThreadID);
        }
        CloseHandle(snapshot);
        fflush(stdout);
    }

    void WatchdogThread()
    {
        uint64_t lastFrames = 0;
        auto lastChange = std::chrono::steady_clock::now();
        bool reported = false;
        while (g_running.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const uint64_t frames = Kernel::Stats().frames.load(std::memory_order_relaxed);
            const auto now = std::chrono::steady_clock::now();

            // COD3_WATCHDOG=N dumps every N seconds whether or not anything
            // has stopped: what every host thread is doing, on demand.
            static const int every = []() {
                const char* text = getenv("COD3_WATCHDOG");
                return text != nullptr ? int(strtol(text, nullptr, 10)) : 0;
            }();
            static auto lastDump = now;
            if (every > 0 && now - lastDump > std::chrono::seconds(every))
            {
                lastDump = now;
                DumpAllThreads();
            }

            if (frames != lastFrames)
            {
                lastFrames = frames;
                lastChange = now;
                reported = false;
                continue;
            }
            if (!reported && frames != 0 && now - lastChange > std::chrono::seconds(8))
            {
                reported = true;
                DumpAllThreads();
            }
        }
    }
}

void Kernel::PrintHostStack(void* context)
{
    const HANDLE process = GetCurrentProcess();
    LoadSymbols(process);
    CONTEXT copy = *static_cast<CONTEXT*>(context);
    printf("  host stack:");
    WalkFrames(process, GetCurrentThread(), copy);
    printf("\n");
    fflush(stdout);
}

void Kernel::StartWatchdog()
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    g_thread = std::thread(WatchdogThread);
    g_thread.detach();
}
