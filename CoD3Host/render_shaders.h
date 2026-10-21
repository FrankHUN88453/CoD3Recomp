#pragma once

// The title's programs, as Direct3D shaders: translated from the microcode
// once, compiled once, kept for the run and on disk for the next.
//
// A program is known by the hash of its microcode. The stream's upload of
// a program (IM_LOAD) hashes the words where they lie and looks the hash
// up; a draw then only reads the stage's current entry. A new program is
// translated to HLSL and compiled on a worker thread while the frames go
// on without the draws that need it, unless the compiled bytecode is in
// the disk cache, in which case neither happens: the cache file carries
// the translation's metadata beside the bytecode, keyed by the microcode
// hash and the translator's version.
//
// The microcode of every program seen is also written out once, under
// the working directory's shaders folder, so the next run can compile
// what is not cached before the level needs it (Precompile).

#include "render_state.h"
#include "xenos_hlsl.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11DeviceChild;

namespace RenderShaders
{
    using RenderState::Handle;

    void Initialize(ID3D11Device* device);

    // A program uploaded by the stream: its words in guest memory. Returns
    // the program's handle, which becomes the stage's current one.
    Handle Loaded(bool pixel, const uint8_t* words, uint32_t sizeDwords);
    // The same, with the microcode's hash (HashMicrocode) already known.
    Handle Loaded(bool pixel, const uint8_t* words, uint32_t sizeDwords, uint64_t hash);
    Handle Current(bool pixel);

    enum class State : uint8_t { New, Translating, Compiling, Ready, Failed };

    struct Program
    {
        uint64_t hash = 0;
        bool pixel = false;
        bool skipped = false;              // COD3_D3DSKIPVS names it
        bool captured = false;             // from the shaders folder, not the stream (yet)
        bool announced = false;            // the draw path has said it cannot be drawn with
        std::atomic<State> state{ State::New };
        std::string problem;               // why it failed, set before the state says so
        ID3D11DeviceChild* shader = nullptr;   // a vertex or pixel shader, once ready
        std::vector<XenosHlsl::VertexFetch> vertexFetches;
        std::vector<XenosHlsl::TextureFetch> textureFetches;
        uint32_t colourTargets = 1;
        std::vector<uint16_t> constantMap; // the file's constants the program reads, packed; empty means all of them
        bool writesDepth = false;
        std::vector<uint8_t> bytecode;     // kept for the stream out probe
        uint32_t hlslBytes = 0;
        // A pixel program once more without the depth it puts out (the
        // console's precision, xenos_hlsl.cpp), for the draws whose depth
        // test cannot see it: built when a draw first wants it.
        std::atomic<uint8_t> noDepthState{ 0 };   // 0 not asked, 1 building, 2 ready, 3 failed
        ID3D11PixelShader* noDepth = nullptr;
    };

    const Program* Get(Handle handle);
    // A pixel program's variant without its depth output: the shader once
    // built, nothing until then (the first ask queues the build).
    ID3D11PixelShader* NoDepthVariant(Handle handle);
    uint64_t HashOf(Handle handle);
    // The hash a program of that microcode is known by.
    uint64_t HashMicrocode(const uint8_t* words, uint32_t sizeDwords);

    // Compiles, in the background, every captured program not in the disk
    // cache: the shaders folder's files, left by earlier runs.
    void Precompile();

    struct Statistics
    {
        uint64_t loads = 0;           // uploads seen
        uint64_t programs = 0;        // distinct
        uint64_t fromDisk = 0;
        uint64_t compiled = 0;
        uint64_t failed = 0;
        uint64_t compileMilliseconds = 0;
        uint32_t pending = 0;
    };
    Statistics Stats();
    void Report();
}
