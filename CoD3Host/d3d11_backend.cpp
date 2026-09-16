#include "d3d11_backend.h"
#include "xenos_hlsl.h"
#include "gpu.h"
#include "kernel.h"
#include "shaders.h"
#include "edram.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // --- the device ----------------------------------------------------------

    std::recursive_mutex g_mutex;
    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;
    ComPtr<IDXGISwapChain1> g_swapChain;
    HWND g_swapChainWindow = nullptr;
    int g_swapChainWidth = 0, g_swapChainHeight = 0;
    bool g_failed = false;
    bool g_started = false;

    typedef HRESULT (WINAPI* CompileFunction)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
        ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    CompileFunction g_compile = nullptr;

    // Counts for the report.
    std::atomic<uint64_t> g_draws{ 0 }, g_drawsSkipped{ 0 }, g_resolves{ 0 }, g_presents{ 0 };
    std::atomic<uint64_t> g_shadersCompiled{ 0 }, g_shadersFailed{ 0 }, g_compileMilliseconds{ 0 };
    std::atomic<uint64_t> g_bufferUploads{ 0 }, g_bufferBytes{ 0 }, g_textureUploads{ 0 };
    std::map<std::string, uint64_t> g_skipReasons;

    void Skip(const char* reason)
    {
        g_drawsSkipped.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_skipReasons[reason]++;
    }

    void ReportOnce(const char* what, uint32_t detail)
    {
        static std::map<std::string, bool> seen;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        char key[128];
        snprintf(key, sizeof(key), "%s %u", what, detail);
        if (seen.count(key)) return;
        seen[key] = true;
        printf("d3d11: %s (%u / 0x%X)\n", what, detail, detail);
        fflush(stdout);
    }

    // --- the registers -------------------------------------------------------

    constexpr uint32_t SnapshotFirst = 0x2000;
    constexpr uint32_t SnapshotCount = 0x3000;
    uint32_t g_registers[SnapshotCount];

    void Snapshot() { Gpu::SnapshotRegisters(SnapshotFirst, SnapshotCount, g_registers); }
    uint32_t Reg(uint32_t index)
    {
        if (index >= SnapshotFirst && index < SnapshotFirst + SnapshotCount)
            return g_registers[index - SnapshotFirst];
        return Gpu::ReadRegister(Gpu::ApertureBase + index * 4);
    }
    float RegFloat(uint32_t index)
    {
        const uint32_t bits = Reg(index);
        float value;
        memcpy(&value, &bits, 4);
        return value;
    }

    void OpenCache();

    bool Start()
    {
        if (g_started) return !g_failed;
        g_started = true;

        static const bool wanted = []() {
            const char* text = getenv("COD3_GPU");
            return text == nullptr || (strcmp(text, "soft") != 0 && strcmp(text, "0") != 0);
        }();
        if (!wanted) { g_failed = true; return false; }

        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        if (compiler != nullptr)
            g_compile = reinterpret_cast<CompileFunction>(GetProcAddress(compiler, "D3DCompile"));
            OpenCache();
        if (g_compile == nullptr)
        {
            printf("d3d11: d3dcompiler_47.dll is not available; the software rasteriser is used\n");
            g_failed = true;
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (getenv("COD3_D3DDEBUG") != nullptr) flags |= D3D11_CREATE_DEVICE_DEBUG;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, 1, D3D11_SDK_VERSION, &g_device, &level, &g_context);
        if (FAILED(hr))
        {
            printf("d3d11: no device (0x%08lX); the software rasteriser is used\n", hr);
            g_failed = true;
            return false;
        }
        printf("d3d11: device created%s\n", (flags & D3D11_CREATE_DEVICE_DEBUG) ? " with the debug layer" : "");
        fflush(stdout);
        return true;
    }

    // With COD3_D3DDEBUG, what the debug layer has to say, printed as it comes.
    void DrainDebugMessages()
    {
        static ComPtr<ID3D11InfoQueue> queue;
        static bool tried = false;
        if (!tried) { tried = true; g_device.As(&queue); }
        if (!queue) return;
        static int printed = 0;
        const UINT64 count = queue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count && printed < 60; i++)
        {
            SIZE_T length = 0;
            queue->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> storage(length);
            D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (SUCCEEDED(queue->GetMessage(i, message, &length)))
            {
                printf("d3d11 debug: %.*s\n", int(message->DescriptionByteLength), message->pDescription);
                printed++;
            }
        }
        queue->ClearStoredMessages();
        fflush(stdout);
    }

    // --- shaders -------------------------------------------------------------

    // A program not in the disk cache compiles on a worker thread while
    // the frames go on without the draws that need it; pending says so.
    struct VertexShader
    {
        ComPtr<ID3D11VertexShader> shader;
        XenosHlsl::Translation translation;
        bool ok = false;
        bool pending = false;
        std::future<std::vector<uint8_t>> compiling;
    };
    struct PixelShader
    {
        ComPtr<ID3D11PixelShader> shader;
        XenosHlsl::Translation translation;
        bool ok = false;
        bool pending = false;
        std::future<std::vector<uint8_t>> compiling;
    };
    std::map<std::vector<uint32_t>, VertexShader> g_vertexShaders;
    std::map<std::vector<uint32_t>, PixelShader> g_pixelShaders;

    // The compiled programs are kept on disk, under %LOCALAPPDATA%\CoD3Recomp\shaders,
    // named by a hash of their source: the host's compiler takes up to a
    // second over one of the level's programs, and a later run has them at
    // once. A changed translation changes the source, so the hash too.
    std::string g_cacheDirectory;
    std::atomic<uint64_t> g_shadersFromDisk{ 0 };

    void OpenCache()
    {
        if (getenv("COD3_NOSHADERCACHE") != nullptr) return;
        const char* local = getenv("LOCALAPPDATA");
        if (local == nullptr) return;
        std::string directory = std::string(local) + "\\CoD3Recomp";
        CreateDirectoryA(directory.c_str(), nullptr);
        directory += "\\shaders";
        CreateDirectoryA(directory.c_str(), nullptr);
        g_cacheDirectory = directory;
    }

    uint64_t Hash(const std::string& text)
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
        return hash;
    }

    std::string CachePath(const std::string& hlsl, const char* profile)
    {
        char name[64];
        snprintf(name, sizeof(name), "\\%016llx.%s.cso", (unsigned long long)Hash(hlsl), profile);
        return g_cacheDirectory + name;
    }

    // The bytecode of a program: from the cache, else compiled and cached.
    // Empty when it did not compile.
    bool ReadCached(const std::string& hlsl, const char* profile, std::vector<uint8_t>& bytes)
    {
        if (g_cacheDirectory.empty()) return false;
        const std::string path = CachePath(hlsl, profile);
        FILE* in = fopen(path.c_str(), "rb");
        if (in == nullptr) return false;
        fseek(in, 0, SEEK_END);
        const long size = ftell(in);
        fseek(in, 0, SEEK_SET);
        if (size > 0)
        {
            bytes.resize(size_t(size));
            if (fread(bytes.data(), 1, bytes.size(), in) != bytes.size()) bytes.clear();
        }
        fclose(in);
        if (bytes.empty()) return false;
        g_shadersFromDisk.fetch_add(1);
        return true;
    }

    std::vector<uint8_t> Compile(const std::string& hlsl, const char* profile, const char* what)
    {
        std::vector<uint8_t> bytes;
        if (ReadCached(hlsl, profile, bytes)) return bytes;
        const std::string path = g_cacheDirectory.empty() ? std::string() : CachePath(hlsl, profile);

        ComPtr<ID3DBlob> code, errors;
        const auto started = std::chrono::steady_clock::now();
        const HRESULT hr = g_compile(hlsl.data(), hlsl.size(), what, nullptr, nullptr, "main", profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        {
            const double ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count() / 1000.0;
            g_compileMilliseconds.fetch_add(uint64_t(ms), std::memory_order_relaxed);
            if (ms > 200.0) printf("d3d11: a %s of %zu bytes took %.0f ms to compile\n", what, hlsl.size(), ms);
        }
        // COD3_DUMPHLSL=directory keeps every translated program.
        static const char* const dumpTo = getenv("COD3_DUMPHLSL");
        if (dumpTo != nullptr)
        {
            static int number = 0;
            char name[512];
            snprintf(name, sizeof(name), "%s/%s-%03d%s.hlsl", dumpTo, profile, number++, FAILED(hr) ? "-failed" : "");
            if (FILE* out = fopen(name, "wb")) { fwrite(hlsl.data(), 1, hlsl.size(), out); fclose(out); }
        }
        if (FAILED(hr))
        {
            g_shadersFailed.fetch_add(1);
            static int announced = 0;
            if (announced++ < 8)
            {
                printf("d3d11: %s did not compile: %s\n", what,
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
                // The source, once, so the failure can be read against it.
                if (announced <= 2) printf("%s\n", hlsl.c_str());
                fflush(stdout);
            }
            return bytes;
        }
        g_shadersCompiled.fetch_add(1);
        bytes.assign(static_cast<const uint8_t*>(code->GetBufferPointer()),
            static_cast<const uint8_t*>(code->GetBufferPointer()) + code->GetBufferSize());
        if (!path.empty())
        {
            // Written whole under another name, then renamed, so a run cut
            // short leaves no half file.
            const std::string partial = path + ".part";
            if (FILE* out = fopen(partial.c_str(), "wb"))
            {
                const bool written = fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
                fclose(out);
                if (written) MoveFileExA(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
                else DeleteFileA(partial.c_str());
            }
        }
        return bytes;
    }

    // The compiling threads: a few, fed in order, started when first needed.
    std::mutex g_compileMutex;
    std::condition_variable g_compileWake;
    std::deque<std::packaged_task<std::vector<uint8_t>()>> g_compileQueue;
    std::vector<std::thread> g_compileThreads;
    std::atomic<uint32_t> g_compilePending{ 0 };

    void CompileWorker()
    {
        for (;;)
        {
            std::packaged_task<std::vector<uint8_t>()> task;
            {
                std::unique_lock<std::mutex> lock(g_compileMutex);
                g_compileWake.wait(lock, [] { return !g_compileQueue.empty(); });
                task = std::move(g_compileQueue.front());
                g_compileQueue.pop_front();
            }
            task();
            g_compilePending.fetch_sub(1);
        }
    }

    std::future<std::vector<uint8_t>> CompileLater(const std::string& hlsl, const char* profile, const char* what)
    {
        std::packaged_task<std::vector<uint8_t>()> task([hlsl, profile, what] { return Compile(hlsl, profile, what); });
        std::future<std::vector<uint8_t>> result = task.get_future();
        std::lock_guard<std::mutex> lock(g_compileMutex);
        if (g_compileThreads.empty())
        {
            unsigned count = std::thread::hardware_concurrency() / 2;
            if (count < 2) count = 2;
            if (count > 6) count = 6;
            for (unsigned i = 0; i < count; i++) g_compileThreads.emplace_back(CompileWorker);
            for (std::thread& thread : g_compileThreads) thread.detach();
        }
        g_compileQueue.push_back(std::move(task));
        g_compilePending.fetch_add(1);
        g_compileWake.notify_one();
        return result;
    }

    // A program's bytecode: from the disk cache at once, else from a worker
    // thread later; empty while it is on its way or when it failed.
    template <typename Entry>
    std::vector<uint8_t> ProgramBytes(Entry& entry, const char* profile, const char* what)
    {
        if (entry.pending)
        {
            if (entry.compiling.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
            entry.pending = false;
            return entry.compiling.get();
        }
        std::vector<uint8_t> bytes;
        if (ReadCached(entry.translation.hlsl, profile, bytes)) return bytes;
        entry.pending = true;
        entry.compiling = CompileLater(entry.translation.hlsl, profile, what);
        return {};
    }

    const VertexShader& GetVertexShader(const std::vector<uint32_t>& words)
    {
        auto found = g_vertexShaders.find(words);
        if (found != g_vertexShaders.end() && !found->second.pending) return found->second;
        VertexShader& entry = g_vertexShaders[words];
        if (found == g_vertexShaders.end())
        {
            entry.translation = XenosHlsl::Translate(words, false);
            if (!entry.translation.ok)
            {
                static int announced = 0;
                if (announced++ < 12)
                    printf("d3d11: a vertex program could not be translated: %s\n", entry.translation.problem.c_str());
                return entry;
            }
        }
        const std::vector<uint8_t> code = ProgramBytes(entry, "vs_5_0", "vertex program");
        if (!code.empty() && SUCCEEDED(g_device->CreateVertexShader(code.data(), code.size(), nullptr, &entry.shader)))
            entry.ok = true;
        return entry;
    }

    const PixelShader& GetPixelShader(const std::vector<uint32_t>& words)
    {
        auto found = g_pixelShaders.find(words);
        if (found != g_pixelShaders.end() && !found->second.pending) return found->second;
        PixelShader& entry = g_pixelShaders[words];
        if (found == g_pixelShaders.end())
        {
            entry.translation = XenosHlsl::Translate(words, true);
            if (!entry.translation.ok)
            {
                static int announced = 0;
                if (announced++ < 12)
                    printf("d3d11: a pixel program could not be translated: %s\n", entry.translation.problem.c_str());
                return entry;
            }
        }
        const std::vector<uint8_t> code = ProgramBytes(entry, "ps_5_0", "pixel program");
        if (!code.empty() && SUCCEEDED(g_device->CreatePixelShader(code.data(), code.size(), nullptr, &entry.shader)))
            entry.ok = true;
        return entry;
    }

    // The geometry shader that turns the console's rectangle list, three
    // corners with the fourth implied, into two triangles.
    ComPtr<ID3D11GeometryShader> g_rectangleShader;
    bool g_rectangleShaderTried = false;

    const char* const RectangleShaderSource =
        "struct V { float4 position : SV_Position; float4 o0 : TEXCOORD0; float4 o1 : TEXCOORD1; float4 o2 : TEXCOORD2; float4 o3 : TEXCOORD3;"
        " float4 o4 : TEXCOORD4; float4 o5 : TEXCOORD5; float4 o6 : TEXCOORD6; float4 o7 : TEXCOORD7; float4 o8 : TEXCOORD8; float4 o9 : TEXCOORD9;"
        " float4 o10 : TEXCOORD10; float4 o11 : TEXCOORD11; float4 o12 : TEXCOORD12; float4 o13 : TEXCOORD13; float4 o14 : TEXCOORD14; float4 o15 : TEXCOORD15; };\n"
        "V fourth(V a, V b, V c) { V d; d.position = a.position + c.position - b.position;"
        " d.o0 = a.o0 + c.o0 - b.o0; d.o1 = a.o1 + c.o1 - b.o1; d.o2 = a.o2 + c.o2 - b.o2; d.o3 = a.o3 + c.o3 - b.o3;"
        " d.o4 = a.o4 + c.o4 - b.o4; d.o5 = a.o5 + c.o5 - b.o5; d.o6 = a.o6 + c.o6 - b.o6; d.o7 = a.o7 + c.o7 - b.o7;"
        " d.o8 = a.o8 + c.o8 - b.o8; d.o9 = a.o9 + c.o9 - b.o9; d.o10 = a.o10 + c.o10 - b.o10; d.o11 = a.o11 + c.o11 - b.o11;"
        " d.o12 = a.o12 + c.o12 - b.o12; d.o13 = a.o13 + c.o13 - b.o13; d.o14 = a.o14 + c.o14 - b.o14; d.o15 = a.o15 + c.o15 - b.o15; return d; }\n"
        "[maxvertexcount(4)]\n"
        "void main(triangle V input[3], inout TriangleStream<V> stream) {\n"
        "    stream.Append(input[1]); stream.Append(input[0]); stream.Append(input[2]); stream.Append(fourth(input[0], input[1], input[2]));\n"
        "    stream.RestartStrip(); }\n";

    ID3D11GeometryShader* RectangleShader()
    {
        if (!g_rectangleShaderTried)
        {
            g_rectangleShaderTried = true;
            const std::vector<uint8_t> code = Compile(RectangleShaderSource, "gs_5_0", "rectangle geometry program");
            if (!code.empty()) g_device->CreateGeometryShader(code.data(), code.size(), nullptr, &g_rectangleShader);
        }
        return g_rectangleShader.Get();
    }

    // The present: a triangle over the window sampling the frame.
    ComPtr<ID3D11VertexShader> g_presentVertexShader;
    ComPtr<ID3D11PixelShader> g_presentPixelShader;
    ComPtr<ID3D11SamplerState> g_presentSampler;
    const char* const PresentSource =
        "Texture2D frame : register(t0); SamplerState linearSampler : register(s0);\n"
        "struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
        "V vsmain(uint id : SV_VertexID) { V v; float2 uv = float2((id << 1) & 2, id & 2);"
        " v.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); v.uv = uv; return v; }\n"
        "float4 psmain(V v) : SV_Target { return float4(frame.Sample(linearSampler, v.uv).rgb, 1.0); }\n";

    // --- constant buffers ------------------------------------------------------

    struct DrawConstants
    {
        float viewportScale[4];
        float viewportOffset[4];
        float targetSize[4];
        uint32_t flags[4];
        float textureSize[32][4];
        uint32_t textureAdjustment[32][4];
    };
    ComPtr<ID3D11Buffer> g_floatConstants[2];   // vertex, pixel
    ComPtr<ID3D11Buffer> g_boolConstants;
    ComPtr<ID3D11Buffer> g_drawConstants[2];    // vertex, pixel

    ID3D11Buffer* ConstantBuffer(ComPtr<ID3D11Buffer>& buffer, uint32_t bytes)
    {
        if (!buffer)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = (bytes + 15) & ~15u;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            g_device->CreateBuffer(&desc, nullptr, &buffer);
        }
        return buffer.Get();
    }

    // --- state objects ---------------------------------------------------------

    std::map<uint64_t, ComPtr<ID3D11BlendState>> g_blendStates;
    std::map<uint64_t, ComPtr<ID3D11DepthStencilState>> g_depthStates;
    std::map<uint32_t, ComPtr<ID3D11RasterizerState>> g_rasterizerStates;
    std::map<uint64_t, ComPtr<ID3D11SamplerState>> g_samplers;

    D3D11_BLEND BlendFactor(uint32_t factor, bool alpha)
    {
        // The console's numbering: zero, one, then the source colour pair at
        // four, source alpha at six, destination colour at eight, destination
        // alpha at ten, the constant colour at twelve and its alpha at
        // fourteen, and source alpha saturate at sixteen.
        switch (factor)
        {
        case 0:  return D3D11_BLEND_ZERO;
        case 1:  return D3D11_BLEND_ONE;
        case 4:  return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
        case 5:  return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
        case 6:  return D3D11_BLEND_SRC_ALPHA;
        case 7:  return D3D11_BLEND_INV_SRC_ALPHA;
        case 8:  return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
        case 9:  return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
        case 10: return D3D11_BLEND_DEST_ALPHA;
        case 11: return D3D11_BLEND_INV_DEST_ALPHA;
        case 12: return D3D11_BLEND_BLEND_FACTOR;
        case 13: return D3D11_BLEND_INV_BLEND_FACTOR;
        case 14: return D3D11_BLEND_BLEND_FACTOR;
        case 15: return D3D11_BLEND_INV_BLEND_FACTOR;
        case 16: return D3D11_BLEND_SRC_ALPHA_SAT;
        default: return D3D11_BLEND_ONE;
        }
    }

    D3D11_BLEND_OP BlendOperation(uint32_t op)
    {
        switch (op)
        {
        case 0: return D3D11_BLEND_OP_ADD;
        case 1: return D3D11_BLEND_OP_SUBTRACT;
        case 2: return D3D11_BLEND_OP_MIN;
        case 3: return D3D11_BLEND_OP_MAX;
        case 4: return D3D11_BLEND_OP_REV_SUBTRACT;
        default: return D3D11_BLEND_OP_ADD;
        }
    }

    ID3D11BlendState* BlendState(const uint32_t control[4], uint32_t colorMask)
    {
        uint64_t key = colorMask;
        for (int i = 0; i < 4; i++) key = key * 1000003u + control[i];
        auto found = g_blendStates.find(key);
        if (found != g_blendStates.end()) return found->second.Get();

        D3D11_BLEND_DESC desc{};
        desc.IndependentBlendEnable = TRUE;
        for (int i = 0; i < 4; i++)
        {
            const uint32_t c = control[i];
            D3D11_RENDER_TARGET_BLEND_DESC& target = desc.RenderTarget[i];
            const uint32_t srcColor = c & 0x1F, colorOp = (c >> 5) & 7, dstColor = (c >> 8) & 0x1F;
            const uint32_t srcAlpha = (c >> 16) & 0x1F, alphaOp = (c >> 21) & 7, dstAlpha = (c >> 24) & 0x1F;
            const bool trivial = srcColor == 1 && dstColor == 0 && colorOp == 0 &&
                                 srcAlpha == 1 && dstAlpha == 0 && alphaOp == 0;
            target.BlendEnable = trivial ? FALSE : TRUE;
            target.SrcBlend = BlendFactor(srcColor, false);
            target.DestBlend = BlendFactor(dstColor, false);
            target.BlendOp = BlendOperation(colorOp);
            target.SrcBlendAlpha = BlendFactor(srcAlpha, true);
            target.DestBlendAlpha = BlendFactor(dstAlpha, true);
            target.BlendOpAlpha = BlendOperation(alphaOp);
            target.RenderTargetWriteMask = UINT8((colorMask >> (i * 4)) & 0xF);
        }
        ComPtr<ID3D11BlendState>& state = g_blendStates[key];
        g_device->CreateBlendState(&desc, &state);
        return state.Get();
    }

    D3D11_STENCIL_OP StencilOperation(uint32_t op)
    {
        switch (op)
        {
        case 0: return D3D11_STENCIL_OP_KEEP;
        case 1: return D3D11_STENCIL_OP_ZERO;
        case 2: return D3D11_STENCIL_OP_REPLACE;
        case 3: return D3D11_STENCIL_OP_INCR_SAT;
        case 4: return D3D11_STENCIL_OP_DECR_SAT;
        case 5: return D3D11_STENCIL_OP_INVERT;
        case 6: return D3D11_STENCIL_OP_INCR;
        case 7: return D3D11_STENCIL_OP_DECR;
        default: return D3D11_STENCIL_OP_KEEP;
        }
    }

    ID3D11DepthStencilState* DepthState(uint32_t depthControl, uint32_t stencilRef)
    {
        const uint64_t key = (uint64_t(depthControl) << 32) | (stencilRef & 0x00FFFF00u);
        auto found = g_depthStates.find(key);
        if (found != g_depthStates.end()) return found->second.Get();

        D3D11_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = (depthControl >> 1) & 1;
        desc.DepthWriteMask = ((depthControl >> 2) & 1) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_FUNC(((depthControl >> 4) & 7) + 1);
        desc.StencilEnable = depthControl & 1;
        desc.StencilReadMask = UINT8((stencilRef >> 8) & 0xFF);
        desc.StencilWriteMask = UINT8((stencilRef >> 16) & 0xFF);
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_FUNC(((depthControl >> 8) & 7) + 1);
        desc.FrontFace.StencilFailOp = StencilOperation((depthControl >> 11) & 7);
        desc.FrontFace.StencilPassOp = StencilOperation((depthControl >> 14) & 7);
        desc.FrontFace.StencilDepthFailOp = StencilOperation((depthControl >> 17) & 7);
        if ((depthControl >> 7) & 1)   // back face state of its own
        {
            desc.BackFace.StencilFunc = D3D11_COMPARISON_FUNC(((depthControl >> 20) & 7) + 1);
            desc.BackFace.StencilFailOp = StencilOperation((depthControl >> 23) & 7);
            desc.BackFace.StencilPassOp = StencilOperation((depthControl >> 26) & 7);
            desc.BackFace.StencilDepthFailOp = StencilOperation((depthControl >> 29) & 7);
        }
        else desc.BackFace = desc.FrontFace;
        ComPtr<ID3D11DepthStencilState>& state = g_depthStates[key];
        g_device->CreateDepthStencilState(&desc, &state);
        return state.Get();
    }

    ID3D11RasterizerState* RasterizerState(uint32_t modeControl, bool scissor)
    {
        const uint32_t key = (modeControl & 7) | (scissor ? 8 : 0);
        auto found = g_rasterizerStates.find(key);
        if (found != g_rasterizerStates.end()) return found->second.Get();

        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        const bool cullFront = (modeControl & 1) != 0;
        const bool cullBack = (modeControl & 2) != 0;
        desc.CullMode = cullFront && cullBack ? D3D11_CULL_BACK   // everything: nothing would show; the nearest sense
                      : cullFront ? D3D11_CULL_FRONT : cullBack ? D3D11_CULL_BACK : D3D11_CULL_NONE;
        // FACE: nought means a counter clockwise triangle is a front face.
        desc.FrontCounterClockwise = ((modeControl >> 2) & 1) == 0 ? TRUE : FALSE;
        desc.DepthClipEnable = FALSE;
        desc.ScissorEnable = scissor ? TRUE : FALSE;
        ComPtr<ID3D11RasterizerState>& state = g_rasterizerStates[key];
        g_device->CreateRasterizerState(&desc, &state);
        return state.Get();
    }

    D3D11_TEXTURE_ADDRESS_MODE AddressMode(uint32_t clamp)
    {
        switch (clamp)
        {
        case 0: return D3D11_TEXTURE_ADDRESS_WRAP;
        case 1: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case 2: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case 3: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        case 4: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case 5: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        default: return D3D11_TEXTURE_ADDRESS_BORDER;
        }
    }

    ID3D11SamplerState* Sampler(const uint32_t fetch[6])
    {
        const uint32_t clampX = (fetch[0] >> 10) & 7, clampY = (fetch[0] >> 13) & 7, clampZ = (fetch[0] >> 16) & 7;
        uint32_t mag = (fetch[3] >> 19) & 3, min = (fetch[3] >> 21) & 3, mip = (fetch[3] >> 23) & 3;
        const uint32_t aniso = (fetch[3] >> 25) & 7;
        const uint32_t border = fetch[5] & 3;
        if (mag == 3) mag = 1;
        if (min == 3) min = 1;
        if (mip == 3) mip = 1;
        const uint64_t key = clampX | (clampY << 3) | (clampZ << 6) | (mag << 9) | (min << 11) | (mip << 13) |
                             (aniso << 15) | (uint64_t(border) << 18);
        auto found = g_samplers.find(key);
        if (found != g_samplers.end()) return found->second.Get();

        D3D11_SAMPLER_DESC desc{};
        const bool linearMag = mag == 1, linearMin = min == 1, linearMip = mip == 1;
        if (aniso >= 1 && aniso <= 4)
        {
            desc.Filter = D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = 1u << aniso;
        }
        else
        {
            desc.Filter = D3D11_FILTER(
                (linearMin ? 0x10 : 0) | (linearMag ? 0x4 : 0) | (linearMip ? 0x1 : 0));
            desc.MaxAnisotropy = 1;
        }
        desc.AddressU = AddressMode(clampX);
        desc.AddressV = AddressMode(clampY);
        desc.AddressW = AddressMode(clampZ);
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = mip == 2 ? 0.0f : D3D11_FLOAT32_MAX;
        const float white = (border == 1) ? 1.0f : 0.0f;
        for (float& f : desc.BorderColor) f = white;
        ComPtr<ID3D11SamplerState>& state = g_samplers[key];
        g_device->CreateSamplerState(&desc, &state);
        return state.Get();
    }

    // --- render targets ----------------------------------------------------------

    // Rows of EDRAM a surface of a pitch can have: ten megabytes of it, four
    // bytes a sample, and no more than the tallest the title draws.
    uint32_t TargetRows(uint32_t pitch, uint32_t bytesPerSample)
    {
        if (pitch == 0) return 0;
        const uint32_t rows = (10u << 20) / (pitch * bytesPerSample);
        return std::min(rows, 1440u);
    }

    DXGI_FORMAT ColorFormat(uint32_t format, uint32_t& bytesPerSample)
    {
        bytesPerSample = 4;
        switch (format)
        {
        case 0: case 1: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case 2: case 10: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case 3: case 12: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 4: return DXGI_FORMAT_R16G16_UNORM;
        case 5: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_UNORM;
        case 6: return DXGI_FORMAT_R16G16_FLOAT;
        case 7: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 14: return DXGI_FORMAT_R32_FLOAT;
        case 15: bytesPerSample = 8; return DXGI_FORMAT_R32G32_FLOAT;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    struct ColorTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11ShaderResourceView> resource;
        uint32_t width = 0, height = 0;   // in host pixels
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
    struct DepthTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11DepthStencilView> view;
        ComPtr<ID3D11ShaderResourceView> resource;
        uint32_t width = 0, height = 0;
    };
    std::map<uint64_t, ColorTarget> g_colorTargets;   // by base tile, pitch, format, scale
    std::map<uint64_t, DepthTarget> g_depthTargets;   // by base tile, pitch, scale

    ColorTarget* GetColorTarget(uint32_t colorInfo, uint32_t pitch, uint32_t scale)
    {
        const uint32_t baseTile = colorInfo & 0xFFF;
        const uint32_t format = (colorInfo >> 16) & 0xF;
        const uint64_t key = baseTile | (uint64_t(pitch) << 12) | (uint64_t(format) << 28) | (uint64_t(scale) << 32);
        auto found = g_colorTargets.find(key);
        if (found != g_colorTargets.end()) return &found->second;

        ColorTarget& target = g_colorTargets[key];
        uint32_t bytesPerSample = 4;
        target.format = ColorFormat(format, bytesPerSample);
        target.width = pitch * scale;
        target.height = TargetRows(pitch, bytesPerSample) * scale;
        if (target.width == 0 || target.height == 0) return nullptr;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = target.width;
        desc.Height = target.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = target.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &target.texture))) return nullptr;
        g_device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.view);
        g_device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.resource);
        printf("d3d11: colour target at tile %u, pitch %u, format %u: %ux%u\n",
            baseTile, pitch, format, target.width, target.height);
        fflush(stdout);
        return &target;
    }

    DepthTarget* GetDepthTarget(uint32_t depthInfo, uint32_t pitch, uint32_t scale)
    {
        const uint32_t baseTile = depthInfo & 0xFFF;
        const uint64_t key = baseTile | (uint64_t(pitch) << 12) | (uint64_t(scale) << 32);
        auto found = g_depthTargets.find(key);
        if (found != g_depthTargets.end()) return &found->second;

        DepthTarget& target = g_depthTargets[key];
        target.width = pitch * scale;
        target.height = TargetRows(pitch, 4) * scale;
        if (target.width == 0 || target.height == 0) return nullptr;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = target.width;
        desc.Height = target.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &target.texture))) return nullptr;
        D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        viewDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        g_device->CreateDepthStencilView(target.texture.Get(), &viewDesc, &target.view);
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
        resourceDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        resourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceDesc.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(target.texture.Get(), &resourceDesc, &target.resource);
        printf("d3d11: depth target at tile %u, pitch %u: %ux%u\n", baseTile, pitch, target.width, target.height);
        fflush(stdout);
        return &target;
    }

    // --- resolved textures: what a resolve made, under its guest address ----------

    struct Resolved
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> resource;
        ComPtr<ID3D11RenderTargetView> view;   // a colour copy is drawn into it
        uint32_t width = 0, height = 0;        // in host pixels
        uint32_t guestWidth = 0, guestHeight = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        bool depth = false;
        uint64_t serial = 0;
    };
    std::map<uint32_t, Resolved> g_resolved;   // by physical destination
    uint64_t g_resolveSerial = 0;

    // A copy from one texture to another through a draw, so the formats and
    // the sizes need not match.
    ComPtr<ID3D11VertexShader> g_copyVertexShader;
    ComPtr<ID3D11PixelShader> g_copyPixelShader;
    ComPtr<ID3D11SamplerState> g_copySampler;
    const char* const CopySource =
        "Texture2D source : register(t0); SamplerState pointSampler : register(s0);\n"
        "cbuffer Rect : register(b0) { float4 uvRect; };\n"
        "struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
        "V vsmain(uint id : SV_VertexID) { V v; float2 t = float2((id << 1) & 2, id & 2);"
        " v.position = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); v.uv = uvRect.xy + t * uvRect.zw; return v; }\n"
        "float4 psmain(V v) : SV_Target { return source.Sample(pointSampler, v.uv); }\n";
    ComPtr<ID3D11Buffer> g_copyRect;

    bool CopyShaders()
    {
        if (g_copyVertexShader) return true;
        ComPtr<ID3DBlob> vs, ps, errors;
        const std::string source = CopySource;
        HRESULT hr = g_compile(source.data(), source.size(), "copy", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &vs, &errors);
        if (SUCCEEDED(hr)) hr = g_compile(source.data(), source.size(), "copy", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &ps, &errors);
        if (FAILED(hr))
        {
            printf("d3d11: the copy program did not compile: %s\n",
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
            return false;
        }
        g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_copyVertexShader);
        g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_copyPixelShader);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        g_device->CreateSamplerState(&sampler, &g_copySampler);
        return g_copyVertexShader && g_copyPixelShader;
    }

    // Draws `source`'s rectangle (in texels of the source) over the whole of
    // the target view of the given size.
    void CopyRectangle(ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight,
                       uint32_t x0, uint32_t y0, uint32_t width, uint32_t height,
                       ID3D11RenderTargetView* target, uint32_t targetWidth, uint32_t targetHeight)
    {
        if (!CopyShaders()) return;
        const float rect[4] = { float(x0) / sourceWidth, float(y0) / sourceHeight,
                                float(width) / sourceWidth, float(height) / sourceHeight };
        {
            static int announced = 0;
            if (announced++ < 4)
                printf("d3d11: copy %u,%u %ux%u of %ux%u into %ux%u\n", x0, y0, width, height,
                    sourceWidth, sourceHeight, targetWidth, targetHeight);
        }
        g_context->UpdateSubresource(ConstantBuffer(g_copyRect, 16), 0, nullptr, rect, 0, 0);
        ID3D11RenderTargetView* targets[1] = { target };
        g_context->OMSetRenderTargets(1, targets, nullptr);
        D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(targetWidth), float(targetHeight), 0.0f, 1.0f };
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(RasterizerState(0, false));
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(nullptr, 0);
        g_context->VSSetShader(g_copyVertexShader.Get(), nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_copyPixelShader.Get(), nullptr, 0);
        ID3D11Buffer* buffers[1] = { g_copyRect.Get() };
        g_context->VSSetConstantBuffers(0, 1, buffers);
        ID3D11ShaderResourceView* sources[1] = { source };
        g_context->PSSetShaderResources(0, 1, sources);
        ID3D11SamplerState* samplers[1] = { g_copySampler.Get() };
        g_context->PSSetSamplers(0, 1, samplers);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(nullptr);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* none[1] = { nullptr };
        g_context->PSSetShaderResources(0, 1, none);
        ID3D11RenderTargetView* noTargets[1] = { nullptr };
        g_context->OMSetRenderTargets(1, noTargets, nullptr);
    }

    // --- guest memory as buffers and textures --------------------------------------

    uint64_t Fingerprint(const uint8_t* data, size_t bytes)
    {
        // Sixty four samples across the range plus the ends: a change
        // anywhere large shows, a change in one texel may not, and a texture
        // that changes by one texel is one this runtime redraws slightly late.
        uint64_t hash = 1469598103934665603ull ^ bytes;
        const size_t step = std::max<size_t>(bytes / 64, 64);
        for (size_t at = 0; at + 8 <= bytes; at += step)
        {
            uint64_t word;
            memcpy(&word, data + at, 8);
            hash = (hash ^ word) * 1099511628211ull;
        }
        if (bytes >= 8)
        {
            uint64_t word;
            memcpy(&word, data + bytes - 8, 8);
            hash = (hash ^ word) * 1099511628211ull;
        }
        return hash;
    }

    struct VertexBuffer
    {
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> resource;
        uint64_t fingerprint = 0;
        uint32_t bytes = 0;
    };
    std::map<uint64_t, VertexBuffer> g_vertexBuffers;   // by physical address, endian

    void SwapWords(uint32_t* words, size_t count, uint32_t endian)
    {
        switch (endian)
        {
        case 1: for (size_t i = 0; i < count; i++) words[i] = ((words[i] & 0xFF00FF00u) >> 8) | ((words[i] & 0x00FF00FFu) << 8); break;
        case 2: for (size_t i = 0; i < count; i++) words[i] = _byteswap_ulong(words[i]); break;
        case 3: for (size_t i = 0; i < count; i++) words[i] = (words[i] >> 16) | (words[i] << 16); break;
        default: break;
        }
    }

    ID3D11ShaderResourceView* GetVertexBuffer(uint32_t physical, uint32_t bytes, uint32_t endian)
    {
        if (bytes == 0 || bytes > (128u << 20)) return nullptr;
        const uint64_t key = physical | (uint64_t(endian) << 32);
        VertexBuffer& entry = g_vertexBuffers[key];
        const uint8_t* data = Guest::Base + Guest::PhysicalAlias(physical);
        const uint64_t fingerprint = Fingerprint(data, bytes);
        if (entry.buffer && entry.bytes >= bytes && entry.fingerprint == fingerprint)
            return entry.resource.Get();

        std::vector<uint32_t> words((bytes + 3) / 4);
        memcpy(words.data(), data, bytes);
        SwapWords(words.data(), words.size(), endian);

        if (!entry.buffer || entry.bytes < bytes)
        {
            entry.buffer.Reset();
            entry.resource.Reset();
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = UINT(words.size() * 4);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            D3D11_SUBRESOURCE_DATA initial{ words.data(), 0, 0 };
            if (FAILED(g_device->CreateBuffer(&desc, &initial, &entry.buffer))) return nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_R32_TYPELESS;
            view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            view.BufferEx.FirstElement = 0;
            view.BufferEx.NumElements = UINT(words.size());
            view.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            g_device->CreateShaderResourceView(entry.buffer.Get(), &view, &entry.resource);
            entry.bytes = uint32_t(words.size() * 4);
        }
        else
        {
            D3D11_BOX box{ 0, 0, 0, UINT(words.size() * 4), 1, 1 };
            g_context->UpdateSubresource(entry.buffer.Get(), 0, &box, words.data(), 0, 0);
        }
        entry.fingerprint = fingerprint;
        g_bufferUploads.fetch_add(1);
        g_bufferBytes.fetch_add(bytes);
        {
            static std::map<uint32_t, uint64_t> bySize;
            bySize[bytes >> 16]++;
            static int reported = 0;
            if (g_bufferUploads.load() % 2000 == 0 && reported++ < 6)
            {
                printf("d3d11: buffer uploads by size (64K units):");
                for (const auto& entry : bySize) printf(" %u:%llu", entry.first, (unsigned long long)entry.second);
                printf("\n");
            }
        }
        return entry.resource.Get();
    }

    struct Texture
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> resource;
        uint64_t fingerprint = 0;
        uint32_t width = 0, height = 0;
        uint32_t format = 0;
        bool resolvedCopy = false;
        uint64_t resolvedSerial = 0;
    };
    std::map<uint64_t, Texture> g_textures;   // by physical address, format, size
    ComPtr<ID3D11ShaderResourceView> g_whiteTexture;

    ID3D11ShaderResourceView* WhiteTexture()
    {
        if (g_whiteTexture) return g_whiteTexture.Get();
        const uint32_t white = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 1;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ &white, 4, 4 };
        ComPtr<ID3D11Texture2D> texture;
        g_device->CreateTexture2D(&desc, &initial, &texture);
        if (texture) g_device->CreateShaderResourceView(texture.Get(), nullptr, &g_whiteTexture);
        return g_whiteTexture.Get();
    }

    // The bytes of a block compressed texture, untiled and with each pair of
    // bytes put the right way round, as the host format wants them.
    bool LinearBlocks(const uint8_t* data, uint32_t width, uint32_t height, uint32_t pitch,
                      uint32_t blockBytes, bool tiled, std::vector<uint8_t>& out, uint32_t& rowBytes)
    {
        const uint32_t blockShift = blockBytes == 8 ? 3 : 4;
        const uint32_t blocksAcross = std::max(pitch / 4, 1u);
        const uint32_t blocksWide = (width + 3) / 4;
        const uint32_t blocksHigh = (height + 3) / 4;
        rowBytes = blocksWide * blockBytes;
        out.resize(size_t(rowBytes) * blocksHigh);
        for (uint32_t by = 0; by < blocksHigh; by++)
        {
            for (uint32_t bx = 0; bx < blocksWide; bx++)
            {
                const uint32_t offset = tiled ? Edram::TiledOffset(bx, by, blocksAcross, blockShift)
                                              : (by * blocksAcross + bx) * blockBytes;
                uint8_t* to = out.data() + size_t(by) * rowBytes + size_t(bx) * blockBytes;
                for (uint32_t i = 0; i < blockBytes; i++) to[i] = data[offset + (i ^ 1)];
            }
        }
        return true;
    }

    ID3D11ShaderResourceView* GetTexture(const uint32_t fetch[6], uint32_t& outWidth, uint32_t& outHeight)
    {
        const uint32_t format = fetch[1] & 0x3F;
        const uint32_t endian = (fetch[1] >> 6) & 3;
        const uint32_t base = fetch[1] & 0xFFFFF000u;
        const uint32_t pitch = ((fetch[0] >> 22) & 0x1FF) * 32;
        const bool tiled = ((fetch[0] >> 31) & 1) != 0;
        const uint32_t width = (fetch[2] & 0x1FFF) + 1;
        const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
        outWidth = width;
        outHeight = height;
        if ((fetch[0] & 3) != 2 || base == 0 || width > 8192 || height > 8192) return WhiteTexture();

        // A surface a resolve made, at the size the frame was drawn.
        auto resolved = g_resolved.find(base & 0x1FFFFFFFu);
        if (resolved != g_resolved.end() && resolved->second.resource)
        {
            outWidth = resolved->second.guestWidth;
            outHeight = resolved->second.guestHeight;
            return resolved->second.resource.Get();
        }

        const uint64_t key = (base & 0x1FFFFFFFu) | (uint64_t(format) << 32) | (uint64_t(width & 0xFFF) << 40) | (uint64_t(height & 0xFFF) << 52);
        Texture& entry = g_textures[key];
        const uint8_t* data = Guest::Base + Guest::PhysicalAlias(base);

        DXGI_FORMAT hostFormat = DXGI_FORMAT_UNKNOWN;
        std::vector<uint8_t> linear;
        uint32_t rowBytes = 0;
        size_t sourceBytes = 0;
        switch (format)
        {
        case 6:   // 8_8_8_8
        {
            sourceBytes = size_t(pitch) * height * 4;
            const uint64_t fingerprint = Fingerprint(data, std::min(sourceBytes, size_t(64u << 20)));
            if (entry.resource && entry.fingerprint == fingerprint) return entry.resource.Get();
            entry.fingerprint = fingerprint;
            hostFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            rowBytes = width * 4;
            linear.resize(size_t(rowBytes) * height);
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                {
                    const uint32_t offset = tiled ? Edram::TiledOffset(x, y, pitch, 2) : (y * pitch + x) * 4;
                    uint32_t raw;
                    memcpy(&raw, data + offset, 4);
                    uint32_t word = raw;
                    SwapWords(&word, 1, endian);
                    // The word is A R G B from the top; the host wants R G B A from the bottom.
                    const uint32_t rgba = ((word >> 16) & 0xFF) | (((word >> 8) & 0xFF) << 8) |
                                          ((word & 0xFF) << 16) | ((word >> 24) << 24);
                    memcpy(linear.data() + size_t(y) * rowBytes + size_t(x) * 4, &rgba, 4);
                }
            break;
        }
        case 2:   // 8
        {
            sourceBytes = size_t(pitch) * height;
            const uint64_t fingerprint = Fingerprint(data, sourceBytes);
            if (entry.resource && entry.fingerprint == fingerprint) return entry.resource.Get();
            entry.fingerprint = fingerprint;
            hostFormat = DXGI_FORMAT_R8_UNORM;
            rowBytes = width;
            linear.resize(size_t(rowBytes) * height);
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                {
                    const uint32_t offset = tiled ? Edram::TiledOffset(x, y, pitch, 0) : (y * pitch + x);
                    linear[size_t(y) * rowBytes + x] = data[offset];
                }
            break;
        }
        case 18: case 19: case 20:   // DXT1, DXT3, DXT5
        {
            const uint32_t blockBytes = format == 18 ? 8 : 16;
            sourceBytes = size_t(std::max(pitch / 4, 1u)) * ((height + 3) / 4) * blockBytes;
            const uint64_t fingerprint = Fingerprint(data, sourceBytes);
            if (entry.resource && entry.fingerprint == fingerprint) return entry.resource.Get();
            entry.fingerprint = fingerprint;
            hostFormat = format == 18 ? DXGI_FORMAT_BC1_UNORM : format == 19 ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM;
            LinearBlocks(data, width, height, pitch, blockBytes, tiled, linear, rowBytes);
            break;
        }
        default:
            ReportOnce("texture format not uploaded", format);
            return WhiteTexture();
        }

        entry.texture.Reset();
        entry.resource.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = (hostFormat == DXGI_FORMAT_BC1_UNORM || hostFormat == DXGI_FORMAT_BC2_UNORM || hostFormat == DXGI_FORMAT_BC3_UNORM)
                   ? ((width + 3) & ~3u) : width;
        desc.Height = (hostFormat == DXGI_FORMAT_BC1_UNORM || hostFormat == DXGI_FORMAT_BC2_UNORM || hostFormat == DXGI_FORMAT_BC3_UNORM)
                    ? ((height + 3) & ~3u) : height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = hostFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ linear.data(), rowBytes, 0 };
        if (FAILED(g_device->CreateTexture2D(&desc, &initial, &entry.texture))) return WhiteTexture();
        g_device->CreateShaderResourceView(entry.texture.Get(), nullptr, &entry.resource);
        entry.width = width;
        entry.height = height;
        entry.format = format;
        g_textureUploads.fetch_add(1);
        return entry.resource.Get();
    }

    // COD3_D3DPROBE=1: reads a texture back and says how much of it is not
    // black, for finding where a black frame goes black.
    void Probe(const char* what, ID3D11Texture2D* texture)
    {
        static const bool enabled = getenv("COD3_D3DPROBE") != nullptr;
        if (!enabled || texture == nullptr) return;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) { printf("d3d11 probe: %s: no staging\n", what); return; }
        g_context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { printf("d3d11 probe: %s: no map\n", what); return; }
        uint64_t nonZero = 0, total = 0;
        uint32_t sample = 0;
        for (uint32_t y = 0; y < desc.Height; y += 8)
        {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch);
            for (uint32_t x = 0; x < desc.Width; x += 8)
            {
                total++;
                if ((row[x] & 0x00FFFFFFu) != 0) { nonZero++; sample = row[x]; }
            }
        }
        g_context->Unmap(staging.Get(), 0);
        printf("d3d11 probe: %s %ux%u format %u: %llu of %llu samples not black, e.g. %08X\n",
            what, desc.Width, desc.Height, desc.Format, (unsigned long long)nonZero, (unsigned long long)total, sample);
        fflush(stdout);
    }

    // --- the draw ------------------------------------------------------------------

    ComPtr<ID3D11Buffer> g_indexBuffer;
    uint32_t g_indexBufferBytes = 0;

    ID3D11Buffer* IndexBuffer(const void* data, uint32_t bytes)
    {
        if (!g_indexBuffer || g_indexBufferBytes < bytes)
        {
            g_indexBuffer.Reset();
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = std::max(bytes, 1u << 20);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(g_device->CreateBuffer(&desc, nullptr, &g_indexBuffer))) return nullptr;
            g_indexBufferBytes = desc.ByteWidth;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(g_indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
        memcpy(mapped.pData, data, bytes);
        g_context->Unmap(g_indexBuffer.Get(), 0);
        return g_indexBuffer.Get();
    }

    uint32_t g_frameCounter = 0;

    void DrawLocked(uint32_t initiator, uint32_t indexBase, uint32_t indexWord)
    {
        Snapshot();
        const uint32_t primitive = initiator & 0x3F;
        const uint32_t sourceSelect = (initiator >> 6) & 3;
        const uint32_t indexCount = initiator >> 16;
        const bool wideIndices = ((initiator >> 11) & 1) != 0;
        if (indexCount == 0) return;

        const uint32_t modeControl = Reg(0x2208) & 7;
        if (modeControl == 0) { Skip("render backend off"); return; }
        const bool depthOnly = modeControl == 5;

        // Points and the primitives this does not draw yet.
        D3D11_PRIMITIVE_TOPOLOGY topology;
        bool rectangles = false;
        bool convertQuads = false, convertFan = false;
        switch (primitive)
        {
        case 2: topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case 3: topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case 4: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case 5: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; convertFan = true; break;
        case 6: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        case 8: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; rectangles = true; break;
        case 13: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; convertQuads = true; break;
        default: Skip(primitive == 1 ? "point list" : "primitive type"); return;
        }

        // The programs.
        const VertexShader& vertexShader = GetVertexShader(Shaders::Microcode(false));
        if (!vertexShader.ok) { Skip(vertexShader.pending ? "vertex program compiling" : "vertex program"); return; }
        const PixelShader* pixelShader = nullptr;
        if (!depthOnly)
        {
            pixelShader = &GetPixelShader(Shaders::Microcode(true));
            if (!pixelShader->ok) { Skip(pixelShader->pending ? "pixel program compiling" : "pixel program"); return; }
        }

        const uint32_t scale = Edram::Scale();
        const uint32_t surfaceInfo = Reg(0x2000);
        const uint32_t pitch = surfaceInfo & 0x3FFF;
        if (pitch == 0) { Skip("no surface pitch"); return; }

        // Targets.
        ID3D11RenderTargetView* views[4] = {};
        uint32_t targetCount = 0;
        const uint32_t colorMask = Reg(0x2104);
        if (!depthOnly)
        {
            // Only the targets the pixel program writes, each once: the
            // title leaves the other target registers pointing at the same
            // tiles, and the host refuses one surface bound twice.
            for (uint32_t i = 0; i < 4; i++)
            {
                if (((pixelShader->translation.colourTargets >> i) & 1) == 0) continue;
                if (((colorMask >> (i * 4)) & 0xF) == 0) continue;
                const uint32_t info = Reg(i == 0 ? 0x2001 : 0x2003 + (i - 1));
                ColorTarget* target = GetColorTarget(info, pitch, scale);
                if (target == nullptr) continue;
                bool duplicate = false;
                for (uint32_t j = 0; j < i; j++) if (views[j] == target->view.Get()) duplicate = true;
                if (duplicate) continue;
                views[i] = target->view.Get();
                targetCount = i + 1;
            }
        }
        const uint32_t depthControl = Reg(0x2200);
        ID3D11DepthStencilView* depthView = nullptr;
        if ((depthControl & 3) != 0 || depthOnly)
        {
            DepthTarget* depth = GetDepthTarget(Reg(0x2002), pitch, scale);
            if (depth != nullptr) depthView = depth->view.Get();
        }
        if (targetCount == 0 && depthView == nullptr) { Skip("no target"); return; }

        // Unbind anything about to be a target from the samplers.
        ID3D11ShaderResourceView* nothing[32] = {};
        g_context->PSSetShaderResources(0, 32, nothing);
        g_context->VSSetShaderResources(0, 32, nothing);
        g_context->OMSetRenderTargets(4, views, depthView);

        // Viewport: the whole target; the console's viewport is folded into
        // the vertex program. Scissor from the window scissor registers.
        const uint32_t targetWidth = pitch * scale;
        const uint32_t targetHeight = TargetRows(pitch, 4) * scale;
        D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(targetWidth), float(targetHeight), 0.0f, 1.0f };
        g_context->RSSetViewports(1, &viewport);
        {
            const uint32_t tl = Reg(0x2081), br = Reg(0x2082);
            const uint32_t offset = Reg(0x2080);
            // Bit 31 of the scissor says the window offset does not apply.
            const int32_t offsetX = (tl & 0x80000000u) ? 0 : (int32_t(offset << 17) >> 17);
            const int32_t offsetY = (tl & 0x80000000u) ? 0 : (int32_t(offset << 1) >> 17);
            D3D11_RECT rect;
            rect.left = std::max(0, int((tl & 0x7FFF) + offsetX) * int(scale));
            rect.top = std::max(0, int(((tl >> 16) & 0x7FFF) + offsetY) * int(scale));
            rect.right = std::min(int(targetWidth), int((br & 0x7FFF) + offsetX) * int(scale));
            rect.bottom = std::min(int(targetHeight), int(((br >> 16) & 0x7FFF) + offsetY) * int(scale));
            if (rect.right <= rect.left || rect.bottom <= rect.top) { Skip("empty scissor"); return; }
            g_context->RSSetScissorRects(1, &rect);
        }

        // State.
        const uint32_t modeCntl = Reg(0x2205);
        g_context->RSSetState(RasterizerState(rectangles ? 0 : modeCntl, true));
        {
            const uint32_t blendControl[4] = { Reg(0x2201), Reg(0x2209), Reg(0x220A), Reg(0x220B) };
            const float factor[4] = { RegFloat(0x2105), RegFloat(0x2106), RegFloat(0x2107), RegFloat(0x2108) };
            g_context->OMSetBlendState(BlendState(blendControl, depthOnly ? 0 : colorMask), factor, 0xFFFFFFFF);
        }
        {
            const uint32_t stencilRef = Reg(0x210D);
            g_context->OMSetDepthStencilState(DepthState(depthView ? depthControl : 0, stencilRef), stencilRef & 0xFF);
        }

        // Constants: the float file, the booleans, and this draw's own.
        {
            const uint32_t* floats = &g_registers[0x4000 - SnapshotFirst];
            g_context->UpdateSubresource(ConstantBuffer(g_floatConstants[0], 4096), 0, nullptr, floats, 0, 0);
            g_context->UpdateSubresource(ConstantBuffer(g_floatConstants[1], 4096), 0, nullptr, floats + 1024, 0, 0);
            // The booleans and the loop constants together: 8 words and 32.
            g_context->UpdateSubresource(ConstantBuffer(g_boolConstants, 160), 0, nullptr, &g_registers[0x4900 - SnapshotFirst], 0, 0);
        }

        DrawConstants constants{};
        constants.viewportScale[0] = RegFloat(0x210F);
        constants.viewportOffset[0] = RegFloat(0x2110);
        constants.viewportScale[1] = RegFloat(0x2111);
        constants.viewportOffset[1] = RegFloat(0x2112);
        constants.viewportScale[2] = RegFloat(0x2113);
        constants.viewportOffset[2] = RegFloat(0x2114);
        constants.targetSize[0] = float(pitch);
        constants.targetSize[1] = float(targetHeight / scale);
        constants.targetSize[2] = 1.0f / float(pitch);
        constants.targetSize[3] = float(scale) / float(targetHeight);
        constants.flags[0] = Reg(0x2206);
        {
            const uint32_t colorControl = Reg(0x2202);
            const bool alphaTest = ((colorControl >> 3) & 1) != 0;
            constants.flags[1] = alphaTest ? (colorControl & 7) : 7;
            constants.flags[2] = Reg(0x210E);
        }

        // Textures and samplers the pixel program samples.
        if (pixelShader != nullptr)
        {
            for (const XenosHlsl::TextureFetch& fetch : pixelShader->translation.textureFetches)
            {
                uint32_t words[6];
                for (uint32_t i = 0; i < 6; i++) words[i] = Reg(0x4800 + fetch.slot * 6 + i);
                uint32_t width = 1, height = 1;
                ID3D11ShaderResourceView* resource = GetTexture(words, width, height);
                g_context->PSSetShaderResources(fetch.slot, 1, &resource);
                ID3D11SamplerState* sampler = Sampler(words);
                g_context->PSSetSamplers(fetch.sampler, 1, &sampler);
                constants.textureSize[fetch.slot][0] = float(width);
                constants.textureSize[fetch.slot][1] = float(height);
                constants.textureSize[fetch.slot][2] = 1.0f / float(width);
                constants.textureSize[fetch.slot][3] = 1.0f / float(height);
                constants.textureAdjustment[fetch.slot][0] = (words[3] >> 1) & 0xFFF;
                constants.textureAdjustment[fetch.slot][1] = (words[0] >> 2) & 0xFF;
            }
        }
        // And the vertex buffers the vertex program fetches.
        for (const XenosHlsl::VertexFetch& fetch : vertexShader.translation.vertexFetches)
        {
            const uint32_t word0 = Reg(0x4800 + fetch.slot * 2);
            const uint32_t word1 = Reg(0x4800 + fetch.slot * 2 + 1);
            const uint32_t sizeBytes = ((word1 >> 2) & 0xFFFFFF) * 4;
            ID3D11ShaderResourceView* resource = GetVertexBuffer(word0 & ~3u, sizeBytes, word1 & 3);
            if (resource == nullptr) { Skip("vertex buffer"); return; }
            g_context->VSSetShaderResources(32 + fetch.slot, 1, &resource);
        }

        g_context->UpdateSubresource(ConstantBuffer(g_drawConstants[0], sizeof(constants)), 0, nullptr, &constants, 0, 0);
        {
            ID3D11Buffer* buffers[3] = { g_floatConstants[0].Get(), g_boolConstants.Get(), g_drawConstants[0].Get() };
            g_context->VSSetConstantBuffers(0, 3, buffers);
            ID3D11Buffer* pixelBuffers[3] = { g_floatConstants[1].Get(), g_boolConstants.Get(), g_drawConstants[0].Get() };
            g_context->PSSetConstantBuffers(0, 3, pixelBuffers);
        }

        g_context->VSSetShader(vertexShader.shader.Get(), nullptr, 0);
        g_context->GSSetShader(rectangles ? RectangleShader() : nullptr, nullptr, 0);
        g_context->PSSetShader(pixelShader ? pixelShader->shader.Get() : nullptr, nullptr, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(topology);

        // Indices: the title's, byte swapped, or none. Quads and fans become
        // triangle lists here.
        std::vector<uint32_t> indices;
        if (sourceSelect == 0)
        {
            const uint32_t address = Guest::PhysicalAlias(indexBase);
            indices.resize(indexCount);
            for (uint32_t i = 0; i < indexCount; i++)
                indices[i] = wideIndices ? Guest::Read32(Guest::Base, address + i * 4)
                                         : Guest::Read16(Guest::Base, address + i * 2);
        }
        else if (convertQuads || convertFan)
        {
            indices.resize(indexCount);
            for (uint32_t i = 0; i < indexCount; i++) indices[i] = i;
        }
        if (convertQuads && !indices.empty())
        {
            std::vector<uint32_t> list;
            for (uint32_t i = 0; i + 3 < indices.size(); i += 4)
            {
                const uint32_t q[6] = { indices[i], indices[i + 1], indices[i + 2], indices[i], indices[i + 2], indices[i + 3] };
                list.insert(list.end(), q, q + 6);
            }
            indices.swap(list);
        }
        if (convertFan && !indices.empty())
        {
            std::vector<uint32_t> list;
            for (uint32_t i = 1; i + 1 < indices.size(); i++)
            {
                const uint32_t t[3] = { indices[0], indices[i], indices[i + 1] };
                list.insert(list.end(), t, t + 3);
            }
            indices.swap(list);
        }

        g_draws.fetch_add(1, std::memory_order_relaxed);
        {
            static int announced = 0;
            if (announced++ < 24)
            {
                printf("d3d11: draw %u of %u indices (%s), vte %03X viewport %.0f %.0f %.0f %.0f, target %u %ux%u, "
                       "scissor %u,%u-%u,%u, depth %08X blend %08X mask %X, vs %zu ps %zu\n",
                    primitive, indexCount, sourceSelect == 0 ? "indexed" : "plain", Reg(0x2206),
                    RegFloat(0x210F), RegFloat(0x2110), RegFloat(0x2111), RegFloat(0x2112),
                    targetCount, targetWidth, targetHeight,
                    Reg(0x2081) & 0x7FFF, (Reg(0x2081) >> 16) & 0x7FFF, Reg(0x2082) & 0x7FFF, (Reg(0x2082) >> 16) & 0x7FFF,
                    depthControl, Reg(0x2201), colorMask,
                    vertexShader.translation.hlsl.size(), pixelShader ? pixelShader->translation.hlsl.size() : 0u);
                fflush(stdout);
            }
        }
        if (!indices.empty())
        {
            ID3D11Buffer* buffer = IndexBuffer(indices.data(), uint32_t(indices.size() * 4));
            if (buffer == nullptr) { Skip("index buffer"); return; }
            g_context->IASetIndexBuffer(buffer, DXGI_FORMAT_R32_UINT, 0);
            g_context->DrawIndexed(UINT(indices.size()), 0, 0);
        }
        else
        {
            g_context->Draw(indexCount, 0);
        }
        {
            static int probes = 0;
            static const int firstProbe = []() {
                const char* text = getenv("COD3_D3DPROBE");
                return text != nullptr ? int(strtol(text, nullptr, 10)) : 0;
            }();
            const int number = probes++;
            if (number >= firstProbe && number < firstProbe + 8 && views[0] != nullptr)
            {
                ColorTarget* color = GetColorTarget(Reg(0x2001), pitch, scale);
                if (color) Probe("target after draw", color->texture.Get());
                if (pixelShader)
                    for (const XenosHlsl::TextureFetch& fetch : pixelShader->translation.textureFetches)
                    {
                        uint32_t w[6];
                        for (uint32_t i = 0; i < 6; i++) w[i] = Reg(0x4800 + fetch.slot * 6 + i);
                        printf("d3d11:   texture slot %u: %08X %08X %08X %08X %08X %08X (format %u, %ux%u)\n", fetch.slot,
                            w[0], w[1], w[2], w[3], w[4], w[5], w[1] & 0x3F, (w[2] & 0x1FFF) + 1, ((w[2] >> 13) & 0x1FFF) + 1);
                    }
                for (const XenosHlsl::VertexFetch& fetch : vertexShader.translation.vertexFetches)
                {
                    const uint32_t word0 = Reg(0x4800 + fetch.slot * 2), word1 = Reg(0x4800 + fetch.slot * 2 + 1);
                    const uint8_t* data = Guest::Base + Guest::PhysicalAlias(word0 & ~3u);
                    printf("d3d11:   vertex slot %u: %08X %08X, first words", fetch.slot, word0, word1);
                    for (uint32_t i = 0; i < 8; i++) { uint32_t v; memcpy(&v, data + i * 4, 4); printf(" %08X", _byteswap_ulong(v)); }
                    printf("\n");
                }
                fflush(stdout);
            }
        }
    }

    // --- the resolve ---------------------------------------------------------------

    void ResolveLocked()
    {
        Snapshot();
        const uint32_t control = Reg(0x2318);
        const uint32_t destBase = Reg(0x2319);
        const uint32_t destPitchWord = Reg(0x231A);
        const uint32_t destInfo = Reg(0x231B);
        const uint32_t surfaceInfo = Reg(0x2000);
        const uint32_t tl = Reg(0x2081), br = Reg(0x2082);
        const uint32_t scale = Edram::Scale();
        const uint32_t pitch = surfaceInfo & 0x3FFF;
        if (pitch == 0) return;

        const uint32_t x0 = tl & 0x7FFF, y0 = (tl >> 16) & 0x7FFF;
        const uint32_t x1 = br & 0x7FFF, y1 = (br >> 16) & 0x7FFF;
        if (x1 <= x0 || y1 <= y0 || x1 - x0 > 4096 || y1 - y0 > 4096) return;
        const uint32_t width = x1 - x0, height = y1 - y0;

        const uint32_t sourceSelect = control & 7;
        const uint32_t command = (control >> 20) & 3;
        const bool colorClear = ((control >> 8) & 1) != 0;
        const bool depthClear = ((control >> 9) & 1) != 0;
        (void)destInfo;

        g_resolves.fetch_add(1, std::memory_order_relaxed);
        {
            static int announced = 0;
            if (announced++ < 12)
            {
                printf("d3d11: resolve control %08X to %08X, %u,%u-%u,%u, pitch %u, clears %d%d\n",
                    control, destBase, x0, y0, x1, y1, pitch, colorClear ? 1 : 0, depthClear ? 1 : 0);
                fflush(stdout);
            }
        }
        if (command != 3 && destBase != 0)
        {
            Resolved& out = g_resolved[destBase & 0x1FFFFFFFu];
            const uint32_t hostWidth = width * scale, hostHeight = height * scale;
            const bool fromDepth = sourceSelect == 4;
            ID3D11ShaderResourceView* source = nullptr;
            uint32_t sourceWidth = 0, sourceHeight = 0;
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (fromDepth)
            {
                DepthTarget* depth = GetDepthTarget(Reg(0x2002), pitch, scale);
                if (depth == nullptr) return;
                source = depth->resource.Get();
                sourceWidth = depth->width;
                sourceHeight = depth->height;
                format = DXGI_FORMAT_R32_FLOAT;
            }
            else
            {
                ColorTarget* color = GetColorTarget(Reg(sourceSelect == 0 ? 0x2001 : 0x2003 + (sourceSelect - 1)), pitch, scale);
                if (color == nullptr) return;
                source = color->resource.Get();
                sourceWidth = color->width;
                sourceHeight = color->height;
                format = color->format;
            }
            if (!out.texture || out.width != hostWidth || out.height != hostHeight || out.format != format)
            {
                out.texture.Reset(); out.resource.Reset(); out.view.Reset();
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = hostWidth;
                desc.Height = hostHeight;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = format;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &out.texture))) return;
                g_device->CreateShaderResourceView(out.texture.Get(), nullptr, &out.resource);
                g_device->CreateRenderTargetView(out.texture.Get(), nullptr, &out.view);
                out.width = hostWidth;
                out.height = hostHeight;
                out.format = format;
                out.depth = fromDepth;
            }
            out.guestWidth = width;
            out.guestHeight = height;
            out.serial = ++g_resolveSerial;
            static int resolveProbes = 0;
            const bool probing = resolveProbes++ < 3;
            if (probing && !fromDepth)
            {
                ColorTarget* color = GetColorTarget(Reg(0x2001), pitch, scale);
                if (color) Probe("render target before resolve", color->texture.Get());
            }
            CopyRectangle(source, sourceWidth, sourceHeight, x0 * scale, y0 * scale, hostWidth, hostHeight,
                          out.view.Get(), hostWidth, hostHeight);
            if (probing) Probe("resolved", out.texture.Get());
        }

        if (colorClear)
        {
            ColorTarget* color = GetColorTarget(Reg(0x2001), pitch, scale);
            if (color != nullptr)
            {
                const uint32_t value = Reg(0x231E);
                const float rgba[4] = { ((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f,
                                        (value & 0xFF) / 255.0f, (value >> 24) / 255.0f };
                ComPtr<ID3D11DeviceContext1> context1;
                if (SUCCEEDED(g_context.As(&context1)))
                {
                    D3D11_RECT rect{ LONG(x0 * scale), LONG(y0 * scale), LONG(x1 * scale), LONG(y1 * scale) };
                    context1->ClearView(color->view.Get(), rgba, &rect, 1);
                }
                else g_context->ClearRenderTargetView(color->view.Get(), rgba);
            }
        }
        if (depthClear)
        {
            DepthTarget* depth = GetDepthTarget(Reg(0x2002), pitch, scale);
            if (depth != nullptr)
            {
                const uint32_t value = Reg(0x231D);
                const float z = float(value >> 8) / 16777215.0f;
                g_context->ClearDepthStencilView(depth->view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, z, UINT8(value & 0xFF));
            }
        }
    }

    // --- the present ---------------------------------------------------------------

    std::atomic<uint32_t> g_frontBuffer{ 0 };
    std::atomic<uint32_t> g_frontWidth{ 0 }, g_frontHeight{ 0 };

    bool PresentShaders()
    {
        if (g_presentVertexShader) return true;
        ComPtr<ID3DBlob> vs, ps, errors;
        const std::string source = PresentSource;
        HRESULT hr = g_compile(source.data(), source.size(), "present", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &vs, &errors);
        if (SUCCEEDED(hr)) hr = g_compile(source.data(), source.size(), "present", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &ps, &errors);
        if (FAILED(hr))
        {
            printf("d3d11: the present program did not compile: %s\n",
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
            return false;
        }
        g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_presentVertexShader);
        g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_presentPixelShader);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        g_device->CreateSamplerState(&sampler, &g_presentSampler);
        return g_presentVertexShader && g_presentPixelShader;
    }

    // COD3_FRAMEDUMP=path with the host GPU: the window's picture, as a
    // bitmap, every COD3_FRAMEDUMP_EVERY presents, cycling through forty.
    void DumpFrame(ID3D11Texture2D* back, int width, int height)
    {
        static const char* const path = getenv("COD3_FRAMEDUMP");
        if (path == nullptr) return;
        static const int every = []() {
            const char* text = getenv("COD3_FRAMEDUMP_EVERY");
            const int value = text != nullptr ? int(strtol(text, nullptr, 10)) : 120;
            return value > 0 ? value : 120;
        }();
        static int counter = 0;
        if ((counter++ % every) != 0) return;

        D3D11_TEXTURE2D_DESC desc{};
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return;
        g_context->CopyResource(staging.Get(), back);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

        char name[512];
        snprintf(name, sizeof(name), "%s-%02d.bmp", path, (counter / every) % 40);
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        info.biSizeImage = DWORD(width) * DWORD(height) * 4;
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + info.biSizeImage;
        FILE* out = fopen(name, "wb");
        if (out != nullptr)
        {
            fwrite(&file, sizeof(file), 1, out);
            fwrite(&info, sizeof(info), 1, out);
            for (int y = 0; y < height; y++)
                fwrite(static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, 1, size_t(width) * 4, out);
            fclose(out);
        }
        g_context->Unmap(staging.Get(), 0);
    }

    bool PresentLocked(HWND hwnd, int clientWidth, int clientHeight)
    {
        if (clientWidth <= 0 || clientHeight <= 0) return false;
        if (!g_swapChain || g_swapChainWindow != hwnd)
        {
            g_swapChain.Reset();
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            if (FAILED(g_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
                FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = clientWidth;
            desc.Height = clientHeight;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (FAILED(factory->CreateSwapChainForHwnd(g_device.Get(), hwnd, &desc, nullptr, nullptr, &g_swapChain))) return false;
            g_swapChainWindow = hwnd;
            g_swapChainWidth = clientWidth;
            g_swapChainHeight = clientHeight;
        }
        else if (g_swapChainWidth != clientWidth || g_swapChainHeight != clientHeight)
        {
            g_context->OMSetRenderTargets(0, nullptr, nullptr);
            g_swapChain->ResizeBuffers(0, clientWidth, clientHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_swapChainWidth = clientWidth;
            g_swapChainHeight = clientHeight;
        }
        if (!PresentShaders()) return false;

        ComPtr<ID3D11Texture2D> back;
        if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
        ComPtr<ID3D11RenderTargetView> view;
        g_device->CreateRenderTargetView(back.Get(), nullptr, &view);
        const float black[4] = { 0, 0, 0, 1 };
        g_context->ClearRenderTargetView(view.Get(), black);

        auto found = g_resolved.find(g_frontBuffer.load() & 0x1FFFFFFFu);
        if (found != g_resolved.end() && found->second.resource)
        {
            // Sixteen by nine, as the console sends it, centred.
            const float aspect = 16.0f / 9.0f;
            float width = float(clientWidth), height = width / aspect;
            if (height > clientHeight) { height = float(clientHeight); width = height * aspect; }
            D3D11_VIEWPORT viewport{ (clientWidth - width) * 0.5f, (clientHeight - height) * 0.5f, width, height, 0.0f, 1.0f };
            ID3D11RenderTargetView* targets[1] = { view.Get() };
            g_context->OMSetRenderTargets(1, targets, nullptr);
            g_context->RSSetViewports(1, &viewport);
            g_context->RSSetState(RasterizerState(0, false));
            g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
            g_context->OMSetDepthStencilState(nullptr, 0);
            g_context->VSSetShader(g_presentVertexShader.Get(), nullptr, 0);
            g_context->GSSetShader(nullptr, nullptr, 0);
            g_context->PSSetShader(g_presentPixelShader.Get(), nullptr, 0);
            ID3D11ShaderResourceView* sources[1] = { found->second.resource.Get() };
            g_context->PSSetShaderResources(0, 1, sources);
            ID3D11SamplerState* samplers[1] = { g_presentSampler.Get() };
            g_context->PSSetSamplers(0, 1, samplers);
            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_context->IASetInputLayout(nullptr);
            g_context->Draw(3, 0);
            ID3D11ShaderResourceView* none[1] = { nullptr };
            g_context->PSSetShaderResources(0, 1, none);
        }
        ID3D11RenderTargetView* noTargets[1] = { nullptr };
        g_context->OMSetRenderTargets(1, noTargets, nullptr);
        DumpFrame(back.Get(), clientWidth, clientHeight);
        g_swapChain->Present(0, 0);
        DrainDebugMessages();
        g_presents.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
}

bool D3D11Backend::Enabled()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return Start();
}

namespace
{
    std::atomic<uint64_t> g_drawNanoseconds{ 0 }, g_resolveNanoseconds{ 0 }, g_presentNanoseconds{ 0 };
    struct Timed
    {
        std::atomic<uint64_t>& into;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        explicit Timed(std::atomic<uint64_t>& i) : into(i) {}
        ~Timed()
        {
            into.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count()), std::memory_order_relaxed);
        }
    };
}

void D3D11Backend::Draw(uint32_t initiator, uint32_t indexBase, uint32_t indexWord)
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    Timed timed(g_drawNanoseconds);
    DrawLocked(initiator, indexBase, indexWord);
}

void D3D11Backend::Resolve()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    Timed timed(g_resolveNanoseconds);
    ResolveLocked();
}

void D3D11Backend::Swap(uint32_t frontBufferPhysical, uint32_t width, uint32_t height)
{
    g_frontBuffer.store(frontBufferPhysical);
    g_frontWidth.store(width);
    g_frontHeight.store(height);
}

bool D3D11Backend::Present(void* hwnd, int clientWidth, int clientHeight)
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return false;
    Timed timed(g_presentNanoseconds);
    return PresentLocked(static_cast<HWND>(hwnd), clientWidth, clientHeight);
}

void D3D11Backend::Report()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_started || g_failed) return;
    printf("d3d11: %llu draws, %llu skipped, %llu resolves, %llu presents, %llu programs compiled (%llu failed), "
           "%llu buffer uploads (%.1f MB), %llu texture uploads\n",
        (unsigned long long)g_draws.load(), (unsigned long long)g_drawsSkipped.load(),
        (unsigned long long)g_resolves.load(), (unsigned long long)g_presents.load(),
        (unsigned long long)g_shadersCompiled.load(), (unsigned long long)g_shadersFailed.load(),
        (unsigned long long)g_bufferUploads.load(), g_bufferBytes.load() / 1048576.0,
        (unsigned long long)g_textureUploads.load());
    printf("        %.2f s compiling programs, %llu programs from the disk cache, %u compiling now\n", g_compileMilliseconds.load() / 1000.0,
        (unsigned long long)g_shadersFromDisk.load(), g_compilePending.load());
    printf("        %.2f s in draws, %.2f s in resolves, %.2f s in presents\n",
        g_drawNanoseconds.load() / 1e9, g_resolveNanoseconds.load() / 1e9, g_presentNanoseconds.load() / 1e9);
    if (!g_skipReasons.empty())
    {
        printf("        skipped:");
        for (const auto& entry : g_skipReasons)
            printf(" %s x%llu;", entry.first.c_str(), (unsigned long long)entry.second);
        printf("\n");
    }
    fflush(stdout);
}
