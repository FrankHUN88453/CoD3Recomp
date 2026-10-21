// The HDR mode's shader files, built into the executable at configure time
// (hdr/embed_shaders.cmake writes hdr_shader_files.cpp in the build folder):
// hdr/cod3_hdr.hlsl, RenoDX's shader library under "renodx/" and the Prism
// tone mapper under "prism/". render_d3d11.cpp compiles them at run time,
// its include handler finding each #include among these by name.
#pragma once

#include <cstddef>

namespace HdrShaders
{
    struct File { const char* name; const unsigned char* data; size_t size; };
    extern const File Files[];
    extern const size_t FileCount;
}
