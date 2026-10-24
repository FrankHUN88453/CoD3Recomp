#pragma once

// The optimizations the F11 menu can turn off one at a time (Graphics,
// "Optimizations"), for finding which one is behind something that looks
// wrong without a rebuild or an environment variable. On by default; the
// renderer copies the settings in here at every swap, and the code that
// does each one reads its switch where it decides. The environment
// variables that turned them off before still do (COD3_SYNCDEPTHWRITEBACK
// and the rest), whatever the menu says.

#include <atomic>

namespace Tuning
{
    inline std::atomic<bool> depthThread{ false };   // 0.5.341: off by default (see settings.h)       // depth copies converted on a thread of their own (render_d3d11.cpp)
    inline std::atomic<bool> fastConstants{ true };     // constant packets stored as runs (gpu.cpp)
    inline std::atomic<bool> shaderLoadCache{ true };   // programs seen before matched without the byte-wise hash (render_shaders.cpp)
    inline std::atomic<bool> strongChangeCheck{ true }; // the eight lane fingerprint (render_resources.cpp)
    inline std::atomic<bool> rangeLooks{ true };        // a changing big buffer's vertices looked at again by each draw (render_resources.cpp)
    inline std::atomic<bool> opaqueShaders{ true };     // pixel programs without the alpha test for draws that do not test (render_commands.cpp)
    inline std::atomic<bool> stagingUploads{ true };    // textures sent through staging copies (render_resources.cpp)
    inline std::atomic<bool> texturePool{ true };       // textures of a shape kept and used again (render_resources.cpp)

    inline bool On(const std::atomic<bool>& which) { return which.load(std::memory_order_relaxed); }
}
