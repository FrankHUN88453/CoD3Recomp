#include "render_stats.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <d3d11_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;

    RenderStats::Counters g_frame;
    RenderStats::Counters g_second;      // summed over the second so far
    uint32_t g_secondFrames = 0;
    double g_secondGpuMilliseconds = 0;
    uint32_t g_secondGpuFrames = 0;
    std::chrono::steady_clock::time_point g_secondStarted = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point g_frameStarted = std::chrono::steady_clock::now();
    double g_secondFrameMilliseconds = 0;
    uint64_t g_frames = 0;

    std::mutex g_snapshotMutex;
    RenderStats::Snapshot g_snapshot;

    // The GPU timestamps: a disjoint query and two timestamps a frame, in
    // a ring of four so the read back is of a frame the GPU has finished.
    struct GpuFrame
    {
        ComPtr<ID3D11Query> disjoint, begin, end;
        bool issued = false;
        bool ended = false;
    };
    GpuFrame g_gpu[4];
    uint32_t g_gpuFrame = 0;
    bool g_gpuProfile = false;

    void ReadGpu(GpuFrame& frame)
    {
        if (!frame.issued) return;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 begin = 0, end = 0;
        // Flagged not to flush: a frame not done yet is simply not counted.
        if (g_context->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
        if (g_context->GetData(frame.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
        if (g_context->GetData(frame.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
        frame.issued = false;
        if (disjoint.Disjoint || disjoint.Frequency == 0 || end < begin) return;
        g_secondGpuMilliseconds += double(end - begin) * 1000.0 / double(disjoint.Frequency);
        g_secondGpuFrames++;
    }
}

void RenderStats::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    g_device = device;
    g_context = context;
    g_gpuProfile = getenv("COD3_GPU_PROFILE") != nullptr;
    if (!g_gpuProfile) return;
    for (GpuFrame& frame : g_gpu)
    {
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        device->CreateQuery(&desc, &frame.disjoint);
        desc.Query = D3D11_QUERY_TIMESTAMP;
        device->CreateQuery(&desc, &frame.begin);
        device->CreateQuery(&desc, &frame.end);
        if (!frame.disjoint || !frame.begin || !frame.end) { g_gpuProfile = false; break; }
    }
}

RenderStats::Counters& RenderStats::Frame() { return g_frame; }

void RenderStats::BeginFrame()
{
    g_frameStarted = std::chrono::steady_clock::now();
    if (!g_gpuProfile) return;
    GpuFrame& frame = g_gpu[g_gpuFrame % 4];
    ReadGpu(frame);   // the one from four frames ago, done by now
    if (frame.issued) return;   // still not: skip this frame's query rather than wait
    g_context->Begin(frame.disjoint.Get());
    g_context->End(frame.begin.Get());
    frame.issued = true;
    frame.ended = false;
}

void RenderStats::EndGpuFrame()
{
    if (!g_gpuProfile) return;
    GpuFrame& frame = g_gpu[g_gpuFrame % 4];
    if (!frame.issued || frame.ended) return;
    g_context->End(frame.end.Get());
    g_context->End(frame.disjoint.Get());
    frame.ended = true;
}

void RenderStats::EndFrame()
{
    const auto now = std::chrono::steady_clock::now();
    if (g_gpuProfile)
    {
        EndGpuFrame();
        g_gpuFrame++;
    }
    g_frames++;
    g_secondFrames++;
    g_secondFrameMilliseconds += std::chrono::duration<double, std::milli>(now - g_frameStarted).count();
    g_second.draws += g_frame.draws; g_second.skipped += g_frame.skipped; g_second.triangles += g_frame.triangles; g_second.resolves += g_frame.resolves;
    g_second.shaderSwitches += g_frame.shaderSwitches; g_second.textureSwitches += g_frame.textureSwitches;
    g_second.pipelineSwitches += g_frame.pipelineSwitches; g_second.targetSwitches += g_frame.targetSwitches;
    g_second.textureUploads += g_frame.textureUploads; g_second.bufferUploads += g_frame.bufferUploads;
    g_second.uploadBytes += g_frame.uploadBytes; g_second.streamBytes += g_frame.streamBytes;
    g_second.drawNanoseconds += g_frame.drawNanoseconds; g_second.resolveNanoseconds += g_frame.resolveNanoseconds; g_second.presentNanoseconds += g_frame.presentNanoseconds;
    g_frame = Counters();

    const double elapsed = std::chrono::duration<double>(now - g_secondStarted).count();
    if (elapsed < 1.0) return;
    Snapshot s;
    const float frames = float(g_secondFrames);
    s.fps = float(g_secondFrames / elapsed);
    s.frameMilliseconds = float(g_secondFrameMilliseconds / frames);
    s.cpuMilliseconds = float((g_second.drawNanoseconds + g_second.resolveNanoseconds + g_second.presentNanoseconds) / 1e6 / frames);
    s.gpuMilliseconds = g_secondGpuFrames != 0 ? float(g_secondGpuMilliseconds / g_secondGpuFrames) : 0.0f;
    s.gpuProfiled = g_gpuProfile;
    s.draws = g_second.draws / frames; s.triangles = g_second.triangles / frames; s.skipped = g_second.skipped / frames; s.resolves = g_second.resolves / frames;
    s.shaderSwitches = g_second.shaderSwitches / frames; s.textureSwitches = g_second.textureSwitches / frames;
    s.pipelineSwitches = g_second.pipelineSwitches / frames; s.targetSwitches = g_second.targetSwitches / frames;
    s.textureUploads = g_second.textureUploads / frames; s.bufferUploads = g_second.bufferUploads / frames;
    s.uploadKilobytes = float(g_second.uploadBytes / 1024.0 / frames); s.streamKilobytes = float(g_second.streamBytes / 1024.0 / frames);
    s.frames = g_frames;
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        g_snapshot = s;
    }
    g_second = Counters();
    g_secondFrames = 0;
    g_secondFrameMilliseconds = 0;
    g_secondGpuMilliseconds = 0;
    g_secondGpuFrames = 0;
    g_secondStarted = now;
    Tick();
}

RenderStats::Snapshot RenderStats::Current()
{
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    return g_snapshot;
}

bool RenderStats::Wanted()
{
    static const bool wanted = getenv("COD3_RENDER_STATS") != nullptr;
    return wanted;
}

bool RenderStats::GpuProfiled() { return g_gpuProfile; }

bool RenderStats::Debug()
{
    static const bool wanted = getenv("COD3_RENDER_DEBUG") != nullptr;
    return wanted;
}

void RenderStats::Tick()
{
    if (!Wanted()) return;
    const Snapshot s = Current();
    printf("stats: %.1f fps, frame %.2f ms, cpu %.2f ms%s%.2f ms, %.0f draws (%.0f skipped), %.0f k triangles, %.0f resolves, "
           "switches: %.0f shader %.0f texture %.0f pipeline %.0f target; uploads %.0f tex %.0f buf (%.0f KB), streamed %.0f KB\n",
        s.fps, s.frameMilliseconds, s.cpuMilliseconds, s.gpuProfiled ? ", gpu " : ", gpu n/a ", s.gpuMilliseconds,
        s.draws, s.skipped, s.triangles / 1000.0f, s.resolves,
        s.shaderSwitches, s.textureSwitches, s.pipelineSwitches, s.targetSwitches,
        s.textureUploads, s.bufferUploads, s.uploadKilobytes, s.streamKilobytes);
    fflush(stdout);
}
