#include "installer.h"
#include "kernel.h"
#include <cstdlib>
#include "scheduler.h"
#include "sampler.h"
#include "window.h"
#include "xdvdfs.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>

namespace fs = std::filesystem;

namespace
{
    fs::path ExecutableDirectory()
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
        if (length == 0) return fs::current_path();
        return fs::path(buffer).parent_path();
    }

    void Banner()
    {
        printf("Call of Duty 3 - static recompilation\n");
        printf("Recompiled from the Xbox 360 executable with XenonRecomp.\n");
        printf("\n");
    }

    void Usage()
    {
        printf("Usage: CoD3 [options]\n");
        printf("\n");
        printf("With no options, the game starts if it is already installed next to\n");
        printf("this program, and first time setup runs if it is not.\n");
        printf("\n");
        printf("  --iso <file>      install from this disc image, no file picker\n");
        printf("  --game <folder>   use this extracted folder where it is\n");
        printf("  --copy <folder>   copy this extracted folder into game/\n");
        printf("  --list-iso <file> show what is inside a disc image and exit\n");
        printf("  --extract-one <iso> <path in iso> <output file>\n");
        printf("                    pull a single file out of a disc image\n");
        printf("  --install-only    run setup but do not start the game\n");
        printf("  --help            this text\n");
        printf("\n");
    }

    // Reads a disc image and prints its contents. Useful on its own for
    // checking an image before committing to a multi-gigabyte extraction.
    int ListIso(const fs::path& isoPath)
    {
        XDvdFs::Image image;
        std::string error;
        if (!image.Open(isoPath, error))
        {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        printf("%s\n", isoPath.string().c_str());
        printf("  filesystem  %s, starting at offset 0x%llX\n",
            image.FormatName(), (unsigned long long)image.BaseOffset());

        std::vector<XDvdFs::Entry> files;
        if (!image.ListFiles(files, error))
        {
            fprintf(stderr, "  %s\n", error.c_str());
            return 1;
        }

        uint64_t total = 0;
        bool hasXex = false;
        for (const auto& file : files)
        {
            total += file.size;
            if (file.path == "default.xex") hasXex = true;
        }

        printf("  contents    %zu files, %.2f GB\n", files.size(), total / 1073741824.0);
        printf("  default.xex %s\n\n", hasXex ? "present" : "MISSING, this is not the game");

        for (const auto& file : files)
            printf("  %12llu  %s\n", (unsigned long long)file.size, file.path.c_str());

        return hasXex ? 0 : 1;
    }

    // Pulls one file out of a disc image and writes it where asked. Reading a
    // known file and comparing it byte for byte is how the reader gets checked
    // without writing a whole disc to disk.
    int ExtractOne(const fs::path& isoPath, const std::string& insidePath,
                   const fs::path& destination)
    {
        XDvdFs::Image image;
        std::string error;
        if (!image.Open(isoPath, error))
        {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        std::vector<XDvdFs::Entry> files;
        if (!image.ListFiles(files, error))
        {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        for (const auto& file : files)
        {
            if (file.path != insidePath) continue;

            std::vector<uint8_t> data;
            if (!image.ReadEntry(file, data, error))
            {
                fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }

            FILE* out = nullptr;
            if (_wfopen_s(&out, destination.c_str(), L"wb") != 0 || out == nullptr)
            {
                fprintf(stderr, "cannot write %s\n", destination.string().c_str());
                return 1;
            }
            fwrite(data.data(), 1, data.size(), out);
            fclose(out);

            printf("%s -> %s (%zu bytes)\n",
                insidePath.c_str(), destination.string().c_str(), data.size());
            return 0;
        }

        fprintf(stderr, "%s is not in the image\n", insidePath.c_str());
        return 1;
    }

    void ExplainIncompleteRuntime()
    {
        printf("\n");
        printf("Note: this boots, renders, and reaches the loading screen. It does\n");
        printf("not reach the menu: the title streams part of its archive and then\n");
        printf("waits. What it waits for is reported as it runs. See STATUS.md.\n");
        printf("\n");
    }
}

namespace
{
    int Run(int argc, char** argv);
}

int main(int argc, char** argv)
{
    // Every exit goes through Kernel::Exit, which waits for a key when this
    // program owns the console. Started from Explorer that is the difference
    // between a readable window and one that vanishes instantly.
    Kernel::Exit(Run(argc, argv));
}

namespace
{

int Run(int argc, char** argv)
{
    // --- Command line ------------------------------------------------------
    Installer::Source source;
    bool installOnly = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string option = argv[i];
        const bool hasValue = (i + 1) < argc;

        if (option == "--help" || option == "-h" || option == "/?")
        {
            Usage();
            return 0;
        }
        if (option == "--install-only")
        {
            installOnly = true;
        }
        else if (option == "--list-iso" && hasValue)
        {
            return ListIso(fs::path(argv[++i]));
        }
        else if (option == "--extract-one" && (i + 3) < argc)
        {
            const fs::path iso = argv[i + 1];
            const std::string inside = argv[i + 2];
            const fs::path destination = argv[i + 3];
            return ExtractOne(iso, inside, destination);
        }
        else if (option == "--iso" && hasValue)
        {
            source.iso = fs::path(argv[++i]);
        }
        else if (option == "--game" && hasValue)
        {
            source.folder = fs::path(argv[++i]);
            source.copyFolder = false;
        }
        else if (option == "--copy" && hasValue)
        {
            source.folder = fs::path(argv[++i]);
            source.copyFolder = true;
        }
        else
        {
            fprintf(stderr, "Unrecognised option: %s\n\n", option.c_str());
            Usage();
            return 1;
        }
    }

    Banner();

    const fs::path exeDirectory = ExecutableDirectory();

    // --- Find the game, running first time setup if needed ----------------
    Installer::GameLocation game;
    std::string error;
    if (!Installer::Locate(exeDirectory, source, game, error))
    {
        if (error.empty())
        {
            printf("Cancelled. Nothing was changed.\n");
            return 0;
        }
        fprintf(stderr, "Setup failed: %s\n", error.c_str());
        return 1;
    }

    printf("Game: %s\n\n", game.root.string().c_str());

    // Useful while the runtime is still missing: install the game without
    // watching the boot stop at the first kernel call.
    if (installOnly)
    {
        printf("Setup done, not booting (--install-only).\n");
        return 0;
    }

    ExplainIncompleteRuntime();

    // --- Bring up the guest -----------------------------------------------
    // The file imports resolve guest paths under the installed game, and put
    // anything the title writes in a saves folder next to this program.
    Guest::GameRoot = game.root;
    Kernel::InitializeFileSystem(exeDirectory);

    if (!Guest::Initialize(game.executable.string().c_str()))
    {
        fprintf(stderr, "\nCould not set up the guest address space.\n");
        Guest::Shutdown();
        return 1;
    }

    // A fresh context. The stack pointer is the one register the entry point
    // cannot supply for itself.
    PPCContext ctx{};
    ctx.r1.u32 = Guest::StackBase - 0x100;
    ctx.r13.u32 = Guest::CreateThreadPointer(1);
    ctx.fpscr.loadFromHost();

    // Reports where the guest threads are, so a stall in translated code can be
    // located without a debugger.
    // Opened before the guest starts so there is something on screen from the
    // first moment, rather than a console and nothing else.
    Window::Open();

    Scheduler::Attach(Guest::CurrentProcessor(ctx));
    Kernel::RegisterEntryThread();
    Sampler::Start(10);

    // Whatever ends this process, say so. A run that simply stops says nothing
    // about whether the title asked to quit or something ended it.
    atexit([]() {
        printf("\nThe process is exiting.\n");
        fflush(stdout);
    });

    // A guest address to look at, named in the environment rather than built
    // in, so chasing a particular object costs a run and not a rebuild.
    // COD3_WATCH takes a hexadecimal guest address: its first four words are
    // printed here, before any guest code runs, and a hardware write watchpoint
    // is set on it so the next write to it says which guest function did it.
    if (const char* watch = getenv("COD3_WATCH"))
    {
        const uint32_t address = uint32_t(strtoul(watch, nullptr, 16));
        if (address != 0)
        {
            printf("watch: 0x%08X holds %08X %08X %08X %08X before the guest starts\n",
                address,
                Guest::Read32(Guest::Base, address),
                Guest::Read32(Guest::Base, address + 4),
                Guest::Read32(Guest::Base, address + 8),
                Guest::Read32(Guest::Base, address + 12));
            Kernel::WatchWrite(address);
            Kernel::ArmWatchpoints();
        }
    }

    printf("\nEntering guest code at _xstart\n");
    fflush(stdout);

    _xstart(ctx, Guest::Base);

    // Reaching here would mean the guest returned from its entry point, which
    // it should not do while the kernel imports are still stubs.
    printf("\nGuest returned from _xstart with r3 = 0x%08X\n", ctx.r3.u32);
    Guest::Shutdown();
    return 0;
}

}   // namespace
