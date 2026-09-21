#pragma once

// What the renderer did, counted per frame and averaged over the last
// second: for the COD3_RENDER_STATS=1 line, the overlay's panel and the
// comparison of one renderer against another.
//
// Counting is a relaxed add on the command thread. The GPU's time comes
// from timestamp queries around the frame, with COD3_GPU_PROFILE=1, read
// back a few frames later so nothing waits on the GPU.

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace RenderStats
{
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Per frame counters, added to as the frame is drawn.
    struct Counters
    {
        uint32_t draws = 0, skipped = 0, triangles = 0, resolves = 0;
        uint32_t shaderSwitches = 0, textureSwitches = 0, pipelineSwitches = 0, targetSwitches = 0;
        uint32_t textureUploads = 0, bufferUploads = 0;
        uint64_t uploadBytes = 0;          // textures and vertex buffers
        uint64_t streamBytes = 0;          // indices and constants through the rings
        uint64_t drawNanoseconds = 0;      // CPU time inside the draw path
        uint64_t resolveNanoseconds = 0;
        uint64_t presentNanoseconds = 0;
    };
    Counters& Frame();

    // The frame's edges: the swap ends one and starts the next. Begin
    // issues the GPU timestamp, End the closing one and folds the frame's
    // counters into the running averages.
    void BeginFrame();
    void EndGpuFrame();   // before the present: the GPU's work for the frame ends here
    void EndFrame();

    // The averages of the last second, for showing.
    struct Snapshot
    {
        float fps = 0;
        float frameMilliseconds = 0;       // between swaps
        float cpuMilliseconds = 0;         // on the command thread, drawing
        float gpuMilliseconds = 0;         // between the frame's timestamps, when profiled
        float draws = 0, triangles = 0, skipped = 0, resolves = 0;
        float shaderSwitches = 0, textureSwitches = 0, pipelineSwitches = 0, targetSwitches = 0;
        float textureUploads = 0, bufferUploads = 0;
        float uploadKilobytes = 0, streamKilobytes = 0;
        uint64_t frames = 0;
        bool gpuProfiled = false;
    };
    Snapshot Current();

    bool Wanted();        // COD3_RENDER_STATS=1: a line a second on stdout
    bool GpuProfiled();   // COD3_GPU_PROFILE=1
    bool Debug();         // COD3_RENDER_DEBUG=1: the commands of a frame, once

    // The one second line, when wanted; called at the swap.
    void Tick();
}
