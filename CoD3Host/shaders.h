#pragma once

// Capturing the shader microcode the title uploads.
//
// Nothing in the command stream says what a draw looks like. The geometry, the
// colours and the textures are all decided by two programs the title uploads
// ahead of each draw, in the GPU's own instruction set. Those programs are the
// missing input to everything that comes after this: a translator needs them,
// and so does anything that wants to know what colour a clear is.
//
// They arrive inline in the command stream, so they can simply be written out
// as they go past.

#include <cstdint>
#include <vector>
#include <vector>

namespace Shaders
{
    // One upload. The microcode is read straight out of guest memory and kept
    // in the byte order the console stores it in.
    void Capture(bool pixel, uint32_t guestAddress, uint32_t sizeDwords);

    void Report();

    // The hash of the most recently uploaded program of each kind, which is the
    // one a draw arriving now will run. The captured file is named after it.
    uint64_t LastHash(bool pixel);

    // The microcode of the program a draw arriving now would run. Returned by
    // value because the command processor can replace it at any time.
    std::vector<uint32_t> Microcode(bool pixel);

    // The microcode of the program a draw arriving now would run. Returned by
    // value because the command processor can replace it at any time.
    std::vector<uint32_t> Microcode(bool pixel);
}
