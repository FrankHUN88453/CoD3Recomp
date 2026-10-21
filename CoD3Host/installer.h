#pragma once

// First run setup.
//
// The recompiled executable needs the game's own data files, which cannot ship
// with it. They live in the main folder: the extracted disc (default.xex, sp,
// mp and the rest), which also holds the saves (its save folder). On first
// launch, when no main folder is known, the user is asked whether this is a
// fresh install (the disc image extracted into a main folder they choose) or
// the game is already installed (they choose its main folder). On every later
// launch the main folder is known and the game starts straight away.

#include <filesystem>
#include <string>

namespace Installer
{
    struct GameLocation
    {
        std::filesystem::path root;      // the main folder, holding default.xex
        std::filesystem::path executable;// <root>/default.xex
    };

    // Size of the default.xex this build was recompiled from. A different size
    // means a different region or revision, and the recompiled code will not
    // match it.
    inline constexpr uint64_t ExpectedXexSize = 7254016;

    // A source named on the command line, so setup can run without the
    // interactive menu. All empty means ask the user.
    struct Source
    {
        std::filesystem::path iso;     // a fresh install: extract this disc image
        std::filesystem::path folder;  // an extracted folder: the main folder as it is, or copied
        bool copyFolder = false;       // copy it into the main folder rather than using it in place
        std::filesystem::path main;    // where a fresh install (or a copy) goes; asked when empty
    };

    // True when `folder` looks like an extracted Call of Duty 3 disc.
    bool IsGameFolder(const std::filesystem::path& folder);

    // Finds the main folder, running first time setup if none is known yet.
    // Returns false if the user cancelled or setup failed; `error` is empty on
    // a deliberate cancellation.
    bool Locate(const std::filesystem::path& exeDirectory,
                const Source& source,
                GameLocation& out,
                std::string& error);
}
