#include "installer.h"
#include "xdvdfs.h"

#include <cstdio>
#include <chrono>
#include <string>
#include <vector>

#include <Windows.h>
#include <ShObjIdl.h>

namespace fs = std::filesystem;

namespace
{
    // The main folder chosen, remembered next to the executable; and the
    // folder earlier builds' setup made next to it.
    constexpr wchar_t kPathFile[] = L"game.path";
    constexpr wchar_t kGameFolder[] = L"game";
    constexpr wchar_t kManifest[] = L".cod3install";

    void PrintRule() { printf("---------------------------------------------------------------\n"); }

    std::string Utf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    std::string Bytes(uint64_t n)
    {
        char buffer[64];
        if (n >= (1ull << 30)) snprintf(buffer, sizeof(buffer), "%.2f GB", n / 1073741824.0);
        else if (n >= (1ull << 20)) snprintf(buffer, sizeof(buffer), "%.1f MB", n / 1048576.0);
        else snprintf(buffer, sizeof(buffer), "%llu bytes", (unsigned long long)n);
        return buffer;
    }

    // A single line that rewrites itself, so a long copy does not scroll away
    // everything above it.
    void ShowProgress(uint64_t done, uint64_t total, const std::string& label)
    {
        const int percent = total > 0 ? int((done * 100) / total) : 0;
        std::string shown = label.size() > 40 ? "..." + label.substr(label.size() - 37) : label;
        printf("\r  [%3d%%] %-40s %12s ", percent, shown.c_str(), Bytes(done).c_str());
        fflush(stdout);
    }

    std::string ReadLine()
    {
        char buffer[256] = {};
        if (fgets(buffer, sizeof(buffer), stdin) == nullptr) return {};
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        return line;
    }

    // --- Native pickers --------------------------------------------------

    bool PickWithDialog(bool wantFolder, const wchar_t* title, fs::path& chosen)
    {
        bool picked = false;
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog))))
        {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM |
                (wantFolder ? FOS_PICKFOLDERS : 0));
            dialog->SetTitle(title);

            if (!wantFolder)
            {
                COMDLG_FILTERSPEC filters[] = {
                    { L"Xbox 360 disc image", L"*.iso" },
                    { L"All files", L"*.*" },
                };
                dialog->SetFileTypes(2, filters);
            }

            if (SUCCEEDED(dialog->Show(nullptr)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        chosen = path;
                        CoTaskMemFree(path);
                        picked = true;
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }

        if (SUCCEEDED(initialized)) CoUninitialize();
        return picked;
    }

    // --- Copying ----------------------------------------------------------

    bool MeasureFolder(const fs::path& source, uint64_t& bytes, uint32_t& files, std::string& error)
    {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(source, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) { error = "cannot read " + source.string() + ": " + ec.message(); return false; }
            if (it->is_regular_file(ec)) { bytes += it->file_size(ec); files++; }
        }
        return true;
    }

    bool CopyFolder(const fs::path& source, const fs::path& destination, std::string& error)
    {
        uint64_t total = 0;
        uint32_t fileCount = 0;
        printf("  measuring...\r");
        fflush(stdout);
        if (!MeasureFolder(source, total, fileCount, error)) return false;
        printf("  %u files, %s\n", fileCount, Bytes(total).c_str());

        std::error_code ec;
        fs::create_directories(destination, ec);
        if (ec) { error = "cannot create " + destination.string() + ": " + ec.message(); return false; }

        uint64_t done = 0;
        for (auto it = fs::recursive_directory_iterator(source, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) { error = "cannot read " + source.string() + ": " + ec.message(); return false; }

            const fs::path relative = fs::relative(it->path(), source, ec);
            if (ec) { error = "cannot resolve a path under " + source.string(); return false; }
            const fs::path target = destination / relative;

            if (it->is_directory(ec))
            {
                fs::create_directories(target, ec);
                if (ec) { error = "cannot create " + target.string() + ": " + ec.message(); return false; }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;

            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "cannot copy " + it->path().string() + ": " + ec.message();
                return false;
            }

            done += it->file_size(ec);
            ShowProgress(done, total, relative.string());
        }
        printf("\n");
        return true;
    }

    // --- Manifest ----------------------------------------------------------

    void WriteManifest(const fs::path& gameFolder, const std::string& sourceKind,
                       const fs::path& source)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, (gameFolder / kManifest).c_str(), L"w") != 0 || f == nullptr) return;

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char when[64] = {};
        tm parts{};
        if (gmtime_s(&parts, &now) == 0)
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &parts);

        fprintf(f, "# Written by CoD3 first time setup. Delete this file to run setup again.\n");
        fprintf(f, "installed = %s\n", when);
        fprintf(f, "source_kind = %s\n", sourceKind.c_str());
        fprintf(f, "source = %s\n", Utf8(source.wstring()).c_str());
        fclose(f);
    }

    // --- Validation ---------------------------------------------------------

    void ReportXexMismatch(const fs::path& xex)
    {
        std::error_code ec;
        const uint64_t size = fs::file_size(xex, ec);
        if (ec || size == Installer::ExpectedXexSize) return;

        printf("\n");
        printf("  WARNING: default.xex is %llu bytes, this build was recompiled\n",
            (unsigned long long)size);
        printf("           from a %llu byte executable. A different region or\n",
            (unsigned long long)Installer::ExpectedXexSize);
        printf("           revision will not match the recompiled code.\n");
        printf("\n");
    }
}

bool Installer::IsGameFolder(const fs::path& folder)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return false;
    if (!fs::is_regular_file(folder / "default.xex", ec)) return false;
    // The single player data is what the recompiled executable needs; without
    // it the folder is something else that happens to hold a XEX.
    if (!fs::is_directory(folder / "sp", ec)) return false;
    return true;
}

namespace
{
    // The main folder chosen, remembered next to the executable.
    bool Remember(const fs::path& pathFile, const fs::path& main, std::string& error)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, pathFile.c_str(), L"w") != 0 || f == nullptr)
        {
            error = "cannot write game.path next to the executable (is its folder read only?)";
            return false;
        }
        fprintf(f, "%s\n", Utf8(main.wstring()).c_str());
        fclose(f);
        return true;
    }

    bool Found(const fs::path& main, Installer::GameLocation& out)
    {
        out.root = main;
        out.executable = main / "default.xex";
        ReportXexMismatch(out.executable);
        return true;
    }

    bool AskYes(const char* question)
    {
        printf("%s [y/N]: ", question);
        fflush(stdout);
        const std::string answer = ReadLine();
        return answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes";
    }

    // Whether a folder has anything in it (a missing one has nothing).
    bool HasEntries(const fs::path& folder)
    {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return false;
        return fs::directory_iterator(folder, ec) != fs::directory_iterator();
    }

    // The main folder a fresh install goes into: the one named on the command
    // line, else one the user picks. One that already holds things is used
    // only if the user says so (the disc's files go straight into it).
    bool ChooseInstallFolder(const Installer::Source& source, fs::path& main, std::string& error)
    {
        main = source.main;
        if (main.empty())
        {
            printf("Choose the main folder to install the game into (an empty folder is best)...\n");
            if (!PickWithDialog(true, L"Choose the main folder to install Call of Duty 3 into", main))
                return false;
            printf("  %s\n\n", Utf8(main.wstring()).c_str());
            if (HasEntries(main) && !Installer::IsGameFolder(main))
            {
                printf("That folder is not empty. The game's files and its save folder\n");
                printf("would go in beside what is already there.\n");
                if (!AskYes("Install into it anyway?")) return false;
                printf("\n");
            }
        }
        std::error_code ec;
        fs::create_directories(main, ec);
        if (ec)
        {
            error = "cannot create " + Utf8(main.wstring()) + ": " + ec.message();
            return false;
        }
        return true;
    }

    // A fresh install from a disc image into `main`.
    bool InstallFromImage(const Installer::Source& source, const fs::path& main, std::string& error)
    {
        fs::path iso = source.iso;
        if (iso.empty())
        {
            printf("Choose your Xbox 360 disc image of the game...\n");
            if (!PickWithDialog(false, L"Select the Call of Duty 3 disc image", iso))
                return false;
        }
        printf("  %s\n\n", Utf8(iso.wstring()).c_str());

        XDvdFs::Image image;
        if (!image.Open(iso, error))
            return false;

        printf("  filesystem: %s at offset 0x%llX\n",
            image.FormatName(), (unsigned long long)image.BaseOffset());

        std::vector<XDvdFs::Entry> files;
        if (!image.ListFiles(files, error))
            return false;

        // Check the executable before writing several gigabytes to disk. A
        // wrong disc, or the right game from another region, is much better
        // caught now than after the extraction finishes.
        const XDvdFs::Entry* xexEntry = nullptr;
        uint64_t contentBytes = 0;
        for (const auto& file : files)
        {
            contentBytes += file.size;
            if (file.path == "default.xex") xexEntry = &file;
        }
        if (xexEntry == nullptr)
        {
            error = "the image has no default.xex at its root, so it is not Call of Duty 3";
            return false;
        }
        printf("  contents:   %zu files, %s\n", files.size(), Bytes(contentBytes).c_str());
        if (xexEntry->size != Installer::ExpectedXexSize)
        {
            error = "the image holds a " + std::to_string(xexEntry->size) +
                    " byte default.xex, but this build was recompiled from a " +
                    std::to_string(Installer::ExpectedXexSize) +
                    " byte one. A different region or revision will not run.";
            return false;
        }
        printf("  executable: matches the one this build was recompiled from\n\n");

        printf("Extracting to %s\n", Utf8(main.wstring()).c_str());
        uint64_t lastReported = 0;
        if (!image.ExtractAll(main,
                [&](const XDvdFs::Progress& p)
                {
                    if (p.bytesDone - lastReported < (8u << 20) && p.filesDone != p.filesTotal)
                        return;
                    lastReported = p.bytesDone;
                    ShowProgress(p.bytesDone, p.bytesTotal, p.current);
                },
                error))
        {
            return false;
        }
        printf("\n");
        WriteManifest(main, "iso", iso);
        return true;
    }
}

bool Installer::Locate(const fs::path& exeDirectory, const Source& source,
                       GameLocation& out, std::string& error)
{
    error.clear();
    std::error_code ec;
    const fs::path pathFile = exeDirectory / kPathFile;

    // A main folder named on the command line, used where it is.
    if (!source.folder.empty() && !source.copyFolder)
    {
        if (!IsGameFolder(source.folder))
        {
            error = "that folder has no default.xex and sp folder, so it is not the main "
                    "folder of an installed Call of Duty 3";
            return false;
        }
        if (!Remember(pathFile, source.folder, error)) return false;
        return Found(source.folder, out);
    }

    if (source.iso.empty() && source.folder.empty())
    {
        // 1. The main folder chosen before, remembered next to the executable.
        if (fs::is_regular_file(pathFile, ec))
        {
            FILE* f = nullptr;
            if (_wfopen_s(&f, pathFile.c_str(), L"r") == 0 && f != nullptr)
            {
                char buffer[1024] = {};
                const bool read = fgets(buffer, sizeof(buffer), f) != nullptr;
                fclose(f);
                if (read)
                {
                    std::string line(buffer);
                    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                    const fs::path referenced = fs::u8path(line);
                    if (IsGameFolder(referenced)) return Found(referenced, out);
                    printf("game.path points at %s, which no longer holds the game.\n", line.c_str());
                    printf("Running setup again.\n\n");
                }
            }
        }
        // 2. The executable in the main folder itself.
        if (IsGameFolder(exeDirectory)) return Found(exeDirectory, out);
        // 3. A game folder next to the executable: the layout of earlier
        // builds, which first time setup made.
        if (IsGameFolder(exeDirectory / kGameFolder)) return Found(exeDirectory / kGameFolder, out);
    }

    // 4. First run: a fresh install, or a game already installed.
    PrintRule();
    printf(" Call of Duty 3 - first time setup\n");
    PrintRule();
    printf("\n");

    std::string choice;
    if (!source.iso.empty()) choice = "1";
    else if (!source.folder.empty()) choice = "copy";
    else
    {
        printf("Is this a fresh install, or is the game already installed?\n");
        printf("\n");
        printf("  1  Fresh install: extract the game from your own Xbox 360 disc image\n");
        printf("     (.iso) into a main folder you choose\n");
        printf("  2  Already installed: choose the game's main folder (the one that\n");
        printf("     holds default.xex)\n");
        printf("  q  Quit\n");
        printf("\n");
        printf("The game's data files are not included with this program; they come\n");
        printf("from your own copy of the disc. Your saves are kept in the main\n");
        printf("folder's save folder.\n");
        printf("\n");
        printf("Choice: ");
        fflush(stdout);
        choice = ReadLine();
        printf("\n");
    }

    if (choice == "q" || choice == "Q" || choice.empty())
        return false;   // deliberate cancellation, no error

    fs::path main;
    if (choice == "1")
    {
        // The image first, then the folder it goes into.
        Source withImage = source;
        if (withImage.iso.empty())
        {
            printf("Choose your Xbox 360 disc image of the game...\n");
            if (!PickWithDialog(false, L"Select the Call of Duty 3 disc image", withImage.iso))
                return false;
        }
        if (!ChooseInstallFolder(source, main, error)) return false;
        if (!InstallFromImage(withImage, main, error)) return false;
    }
    else if (choice == "2")
    {
        printf("Choose the game's main folder (the one that holds default.xex)...\n");
        if (!PickWithDialog(true, L"Select the main folder of the installed Call of Duty 3", main))
            return false;
        printf("  %s\n\n", Utf8(main.wstring()).c_str());
        if (!IsGameFolder(main))
        {
            error = "that folder has no default.xex and sp folder, so it is not the main "
                    "folder of an installed Call of Duty 3";
            return false;
        }
    }
    else if (choice == "copy")
    {
        // --copy <folder>: an extracted folder copied into the main folder.
        if (!IsGameFolder(source.folder))
        {
            error = "that folder has no default.xex and sp folder, so it is not an "
                    "extracted Call of Duty 3 disc";
            return false;
        }
        if (!ChooseInstallFolder(source, main, error)) return false;
        printf("Copying to %s\n", Utf8(main.wstring()).c_str());
        if (!CopyFolder(source.folder, main, error))
            return false;
        WriteManifest(main, "folder", source.folder);
    }
    else
    {
        error = "unrecognised choice: " + choice;
        return false;
    }

    if (!IsGameFolder(main))
    {
        error = "setup finished but " + Utf8(main.wstring()) +
                " still does not hold default.xex and an sp folder";
        return false;
    }
    // Remembered, unless the executable is in it (then it is found there).
    if (fs::weakly_canonical(main, ec) != fs::weakly_canonical(exeDirectory, ec) && !Remember(pathFile, main, error))
        return false;

    printf("Setup complete. The main folder is %s\n", Utf8(main.wstring()).c_str());
    printf("and your saves will be kept in its save folder.\n\n");
    return Found(main, out);
}
