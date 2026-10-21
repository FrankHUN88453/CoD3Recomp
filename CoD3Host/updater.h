#pragma once

// The updater: on start, the newest release of the project on GitHub
// (FrankHUN88453/CoD3Recomp, the pre-releases too) held against this build's
// version; a newer one with a CoD3SP_s.exe is downloaded beside this
// executable, put in its place and started with the same command line, and
// this process ends.

namespace Updater
{
    // The executable an earlier update replaced (<this>.old), removed.
    void CleanUp();

    // Looks and, when there is a newer version, updates and restarts: it
    // does not return then. Returns when there is nothing to do or it could
    // not (offline, the folder read only): the game goes on as it is.
    void UpdateAndRestart();
}
