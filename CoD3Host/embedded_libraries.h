#pragma once

// The libraries the executable carries inside itself (FFmpeg's, for the XMA
// sound): written out once into a folder of their own under the user's local
// application data, keyed by what they are, and loaded from there.

#include <string>

namespace EmbeddedLibraries
{
    // The folder they are in, written out on the first call if they are not
    // there yet; empty when this build carries none or they cannot be written.
    const std::wstring& Folder();
}
