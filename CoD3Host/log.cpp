// The log: everything printed goes to the console as before and to CoD3.log.
//
// The C runtime's standard output and error are put onto a pipe, and a
// thread of its own copies whatever comes out of the pipe to the console
// and to the file. Copying at the pipe rather than at each printf means
// nothing else has to change: every thread's output, and the watchdog's
// report of a hang, arrives in both places in the order it was written.

#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <io.h>
#include <Windows.h>

namespace
{
    std::atomic<bool> g_started{ false };
    HANDLE g_readEnd = nullptr;   // the pipe's end the copier reads, to see it empty

    // A run prints its report every ten seconds, some tens of megabytes an
    // hour. Past this the file starts over: what is wanted from it is the
    // end, the minutes before a freeze and the watchdog's report after.
    constexpr uint64_t StartOverBytes = 64ull << 20;

    void Copier(HANDLE readEnd, HANDLE file, HANDLE console)
    {
        char buffer[8192];
        DWORD got = 0;
        uint64_t inFile = 0;
        while (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) && got != 0)
        {
            DWORD written = 0;
            if (inFile + got > StartOverBytes)
            {
                SetFilePointer(file, 0, nullptr, FILE_BEGIN);
                SetEndOfFile(file);
                static const char note[] = "log: started over at 64 MB\n";
                WriteFile(file, note, DWORD(sizeof(note) - 1), &written, nullptr);
                inFile = sizeof(note) - 1;
            }
            WriteFile(file, buffer, got, &written, nullptr);
            inFile += got;
            if (console != nullptr) WriteFile(console, buffer, got, &written, nullptr);
        }
    }
}

void Log::Start()
{
    const char* setting = getenv("COD3_LOG");
    if (setting != nullptr && strcmp(setting, "0") == 0) return;

    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD type = (out == nullptr || out == INVALID_HANDLE_VALUE) ? FILE_TYPE_UNKNOWN : GetFileType(out);
    // Already written to a file or a pipe: whoever started the run keeps it.
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) return;

    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
    const std::filesystem::path directory = std::filesystem::path(exe).parent_path();
    const std::filesystem::path path = setting != nullptr && setting[0] != 0 && strcmp(setting, "1") != 0
#ifdef COD3_MP
        ? std::filesystem::path(setting) : directory / L"CoD3MP.log";
#else
        ? std::filesystem::path(setting) : directory / L"CoD3.log";
#endif
    std::error_code ignored;
    std::filesystem::path previous = path;
    previous.replace_filename(path.stem().wstring() + L".previous" + path.extension().wstring());
    std::filesystem::rename(path, previous, ignored);

    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    // The console gets a handle of its own. Putting the pipe onto the C
    // runtime's descriptor 1 closes the handle that was there, and its
    // number is soon handed out again, to the pipe itself as it happens:
    // the copier then wrote everything it read back into the pipe, and a
    // log came to gigabytes in seconds.
    HANDLE console = nullptr;
    if (type == FILE_TYPE_CHAR &&
        !DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &console, 0, FALSE, DUPLICATE_SAME_ACCESS))
        console = nullptr;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, nullptr, 1 << 16))
    {
        if (console != nullptr) CloseHandle(console);
        CloseHandle(file);
        return;
    }

    // A process started with no console at all has no standard streams to
    // put anywhere; they are opened on nothing first so there is something
    // to redirect.
    // 0.5.216: stdin first. In a windows program (CoD3MP since 0.5.215)
    // descriptors 0-2 are all free, so stdout reopened first took descriptor
    // 0: the pipe went onto 1 and 2 below, stdout kept writing to 0 (NUL)
    // and 0.5.215's CoD3MP.log stayed empty.
    if (type == FILE_TYPE_UNKNOWN)
    {
        FILE* ignoredStream = nullptr;
        if (_fileno(stdin) < 0) freopen_s(&ignoredStream, "NUL", "r", stdin);
        freopen_s(&ignoredStream, "NUL", "w", stdout);
        freopen_s(&ignoredStream, "NUL", "w", stderr);
    }
    fflush(stdout);
    fflush(stderr);
    const int descriptor = _open_osfhandle(intptr_t(writeEnd), _O_TEXT);
    if (descriptor < 0)
    {
        if (console != nullptr) CloseHandle(console);
        CloseHandle(readEnd); CloseHandle(writeEnd); CloseHandle(file);
        return;
    }
    _dup2(descriptor, 1);
    _dup2(descriptor, 2);
    // And wherever the C streams really are (not 1 and 2 when something
    // opened them in another order), onto the pipe as well.
    if (_fileno(stdout) >= 0 && _fileno(stdout) != 1 && _fileno(stdout) != descriptor) _dup2(descriptor, _fileno(stdout));
    if (_fileno(stderr) >= 0 && _fileno(stderr) != 2 && _fileno(stderr) != descriptor) _dup2(descriptor, _fileno(stderr));
    // Unbuffered, as a console is: a prompt must show before the program
    // waits for an answer, and a line must be out before a crash can lose
    // it. A normal run prints a few lines every ten seconds.
    // 0.5.370: stdout BUFFERED (64 KB) and written out every 100 ms by a
    // small thread. Unbuffered, every printf was a write to the pipe: with
    // bots on, some 300 lines a second, 7% of the game's main thread
    // (BIG-PROFILE-32, WriteFile). A crash still flushes it first
    // (kernel.cpp's last-chance handler); stderr stays unbuffered.
    // COD3_LOG_UNBUFFERED=1: as before.
    if (getenv("COD3_LOG_UNBUFFERED") != nullptr) setvbuf(stdout, nullptr, _IONBF, 0);
    else
    {
        setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
        std::thread([] { for (;;) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); fflush(stdout); } }).detach();
    }
    setvbuf(stderr, nullptr, _IONBF, 0);

    g_readEnd = readEnd;
    std::thread(Copier, readEnd, file, console).detach();
    g_started.store(true);
    printf("log: %ls\n", path.c_str());
}

void Log::Finish()
{
    if (!g_started.load()) return;
    fflush(stdout);
    fflush(stderr);
    // The copier takes what is in the pipe on its own; this only waits for
    // the pipe to come up empty, and never long enough to hold up an exit.
    for (int i = 0; i < 50; i++)
    {
        DWORD waiting = 0;
        if (!PeekNamedPipe(g_readEnd, nullptr, 0, nullptr, &waiting, nullptr) || waiting == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // What was read last may still be on its way to the file.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
