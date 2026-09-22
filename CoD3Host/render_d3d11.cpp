// The Direct3D 11 renderer: the device, the draw, the resolve and the
// present. See render.h for where it sits.
//
// A draw is: the registers read into a Snapshot, the state turned into
// handles from the caches, and the handles compared with what the context
// already has, so that a run of draws that share their programs, targets
// and textures costs a few compares and a DrawIndexed each. Nothing here
// allocates, locks or creates a Direct3D object on a draw that has been
// seen before.

#include "render.h"
#include "render_state.h"
#include "render_pipeline.h"
#include "render_shaders.h"
#include "render_resources.h"
#include "render_stats.h"
#include "overlay.h"
#include "settings.h"
#include "gpu.h"
#include "kernel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using RenderState::Handle;

namespace
{
    // --- the device ---------------------------------------------------------------------------

    // The command thread draws and presents; the window thread only reports
    // its size and, when the title is idle, asks for the last frame again.
    std::recursive_mutex g_mutex;
    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;
    ComPtr<ID3D11DeviceContext1> g_context1;
    ComPtr<IDXGISwapChain1> g_swapChain;
    ComPtr<ID3D11RenderTargetView> g_backView;
    ComPtr<ID3D11Texture2D> g_backTexture;
    HWND g_swapChainWindow = nullptr;
    int g_swapChainWidth = 0, g_swapChainHeight = 0;
    bool g_tearing = false;
    bool g_failed = false;
    bool g_started = false;

    std::atomic<HWND> g_window{ nullptr };
    std::atomic<int> g_clientWidth{ 0 }, g_clientHeight{ 0 };
    std::atomic<uint64_t> g_lastPresent{ 0 };   // steady clock milliseconds

    typedef HRESULT (WINAPI* CompileFunction)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
        ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    CompileFunction g_compile = nullptr;

    uint64_t g_frame = 1;          // the title's frames, counted at the swap
    std::atomic<uint64_t> g_swaps{ 0 };
    uint32_t g_frontBuffer = 0;

    // --- what the context has bound ------------------------------------------------------------

    struct Bound
    {
        Handle vertexShader = 0, pixelShader = 0;
        bool rectangles = false;
        bool flat = false;
        Handle blend = 0, depth = 0, rasterizer = 0;
        float blendFactor[4] = { -1, -1, -1, -1 };
        uint32_t stencilReference = 0xFFFFFFFFu;
        ID3D11RenderTargetView* targets[4] = {};
        ID3D11DepthStencilView* depthView = nullptr;
        uint32_t targetCount = 0;
        D3D11_RECT scissor{ -1, -1, -1, -1 };
        uint32_t viewportWidth = 0, viewportHeight = 0;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11ShaderResourceView* textures[2][32] = {};    // pixel, vertex
        ID3D11SamplerState* samplers[2][32] = {};
        ID3D11ShaderResourceView* vertexBuffers[96] = {};
        bool indices32 = false;
        bool indexBufferSet = false;
        uint32_t floatAt[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };   // the float files' ring offsets
        uint32_t floatCount[2] = { 0, 0 };                     // and how many float4s went up there
        Handle floatProgram[2] = { 0, 0 };                     // packed for this program
        uint32_t drawAt = 0xFFFFFFFFu;
        uint32_t boundFloatAt[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }, boundFloatCount[2] = { 0, 0 }, boundDrawAt = 0xFFFFFFFFu;
        bool constantsBound = false;
    };
    Bound g_bound;

    void ForgetBindings()
    {
        g_bound = Bound();
    }

    // The programs' constants: the same layout the translated HLSL declares.
    struct DrawConstants
    {
        float viewportScale[4];
        float viewportOffset[4];
        float targetSize[4];
        uint32_t flags[4];
        float textureSize[32][4];
        uint32_t textureAdjustment[32][4];
    };
    DrawConstants g_constants{};
    DrawConstants g_lastConstants{};
    ComPtr<ID3D11Buffer> g_boolConstants;
    ComPtr<ID3D11Buffer> g_floatConstants[2];   // when the ring is not available
    ComPtr<ID3D11Buffer> g_drawConstants;

    int AntiAliasing(const Settings::Values& settings);

    // --- the built in programs -------------------------------------------------------------------

    ComPtr<ID3D11GeometryShader> g_rectangleShader;
    ComPtr<ID3D11VertexShader> g_quadVertexShader;   // a triangle over the target, from the vertex id
    ComPtr<ID3D11PixelShader> g_copyPixelShader;     // a rectangle of a texture, point sampled
    ComPtr<ID3D11PixelShader> g_copyColourMS[4];     // the same of a multisampled colour target, the samples averaged: 2, 4, 8
    ComPtr<ID3D11PixelShader> g_copyDepthMS[4];      // and of a depth target, its first sample
    ComPtr<ID3D11PixelShader> g_presentPixelShader;  // the frame, linear sampled
    ComPtr<ID3D11PixelShader> g_fxaaPixelShader;
    ComPtr<ID3D11PixelShader> g_flatPixelShader;
    ComPtr<ID3D11SamplerState> g_pointSampler, g_linearSampler;
    ComPtr<ID3D11Buffer> g_quadConstants;

    // The console's rectangle list, three corners with the fourth implied,
    // as two triangles. The three can come in any order: the diagonal is
    // the longest edge on the screen, the corner off it is mirrored across
    // the diagonal for the fourth, and the strip starts from that corner so
    // the first triangle keeps the winding it came with.
    const char* const RectangleSource =
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
        "    float2 p0 = input[0].position.xy / input[0].position.w, p1 = input[1].position.xy / input[1].position.w, p2 = input[2].position.xy / input[2].position.w;\n"
        "    float d01 = dot(p1 - p0, p1 - p0), d12 = dot(p2 - p1, p2 - p1), d02 = dot(p2 - p0, p2 - p0);\n"
        "    uint c = (d02 >= d01 && d02 >= d12) ? 1 : (d01 >= d12) ? 2 : 0;\n"
        "    uint a = (c + 1) % 3, b = (c + 2) % 3;\n"
        "    stream.Append(input[c]); stream.Append(input[a]); stream.Append(input[b]); stream.Append(fourth(input[a], input[c], input[b]));\n"
        "    stream.RestartStrip(); }\n";

    // One triangle over the target; the rectangle of the source it shows
    // and the source's texel size come in the constants.
    const char* const QuadSource =
        "Texture2D source : register(t0); SamplerState smp : register(s0);\n"
        "cbuffer Quad : register(b0) { float4 uvRect; float4 texel; };\n"
        "struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
        "V vsmain(uint id : SV_VertexID) { V v; float2 t = float2((id << 1) & 2, id & 2);"
        " v.position = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); v.uv = uvRect.xy + t * uvRect.zw; return v; }\n"
        "float4 copy(V v) : SV_Target { return source.Sample(smp, v.uv); }\n"
        "float4 present(V v) : SV_Target { return float4(source.Sample(smp, v.uv).rgb, 1.0); }\n"
        // FXAA 3.11, the quality path, on the presented picture: the edges
        // the title's own picture has, softened by the luma along them.
        "float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
        "float4 fxaa(V v) : SV_Target {\n"
        "    float2 uv = v.uv; float2 px = texel.xy;\n"
        "    float3 rgbM = source.Sample(smp, uv).rgb; float lM = luma(rgbM);\n"
        "    float lN = luma(source.Sample(smp, uv + float2(0, -px.y)).rgb), lS = luma(source.Sample(smp, uv + float2(0, px.y)).rgb);\n"
        "    float lW = luma(source.Sample(smp, uv + float2(-px.x, 0)).rgb), lE = luma(source.Sample(smp, uv + float2(px.x, 0)).rgb);\n"
        "    float lMin = min(lM, min(min(lN, lS), min(lW, lE))), lMax = max(lM, max(max(lN, lS), max(lW, lE)));\n"
        "    float range = lMax - lMin;\n"
        "    if (range < max(0.0312, lMax * 0.125)) return float4(rgbM, 1.0);\n"
        "    float lNW = luma(source.Sample(smp, uv + float2(-px.x, -px.y)).rgb), lNE = luma(source.Sample(smp, uv + float2(px.x, -px.y)).rgb);\n"
        "    float lSW = luma(source.Sample(smp, uv + float2(-px.x, px.y)).rgb), lSE = luma(source.Sample(smp, uv + float2(px.x, px.y)).rgb);\n"
        "    float lNS = lN + lS, lWE = lW + lE, lWc = lNW + lSW, lEc = lNE + lSE, lNc = lNW + lNE, lSc = lSW + lSE;\n"
        "    float edgeH = abs(-2.0 * lW + lWc) + abs(-2.0 * lM + lNS) * 2.0 + abs(-2.0 * lE + lEc);\n"
        "    float edgeV = abs(-2.0 * lN + lNc) + abs(-2.0 * lM + lWE) * 2.0 + abs(-2.0 * lS + lSc);\n"
        "    bool horizontal = edgeH >= edgeV;\n"
        "    float l1 = horizontal ? lS : lE, l2 = horizontal ? lN : lW;\n"
        "    float g1 = l1 - lM, g2 = l2 - lM; bool steep1 = abs(g1) >= abs(g2);\n"
        "    float gradScaled = 0.25 * max(abs(g1), abs(g2));\n"
        "    float step = horizontal ? px.y : px.x; float lAvg = 0.0;\n"
        "    if (steep1) { lAvg = 0.5 * (l1 + lM); } else { step = -step; lAvg = 0.5 * (l2 + lM); }\n"
        "    float2 cur = uv; if (horizontal) cur.y += step * 0.5; else cur.x += step * 0.5;\n"
        "    float2 off = horizontal ? float2(px.x, 0) : float2(0, px.y);\n"
        "    float2 uv1 = cur - off, uv2 = cur + off;\n"
        "    float e1 = luma(source.Sample(smp, uv1).rgb) - lAvg, e2 = luma(source.Sample(smp, uv2).rgb) - lAvg;\n"
        "    bool r1 = abs(e1) >= gradScaled, r2 = abs(e2) >= gradScaled; bool done = r1 && r2;\n"
        "    if (!r1) uv1 -= off; if (!r2) uv2 += off;\n"
        "    if (!done) { [loop] for (int i = 2; i < 12; i++) {\n"
        "        if (!r1) e1 = luma(source.Sample(smp, uv1).rgb) - lAvg; if (!r2) e2 = luma(source.Sample(smp, uv2).rgb) - lAvg;\n"
        "        r1 = abs(e1) >= gradScaled; r2 = abs(e2) >= gradScaled; done = r1 && r2;\n"
        "        float q = i < 5 ? 1.5 : i < 10 ? 2.0 : 4.0;\n"
        "        if (!r1) uv1 -= off * q; if (!r2) uv2 += off * q; if (done) break; } }\n"
        "    float d1 = horizontal ? (uv.x - uv1.x) : (uv.y - uv1.y), d2 = horizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);\n"
        "    bool dir1 = d1 < d2; float dist = min(d1, d2), edgeLen = d1 + d2;\n"
        "    float pixelOffset = -dist / edgeLen + 0.5;\n"
        "    bool lessThanAvg = lM < lAvg;\n"
        "    bool correct = ((dir1 ? e1 : e2) < 0.0) != lessThanAvg;\n"
        "    float finalOffset = correct ? pixelOffset : 0.0;\n"
        "    float lAvg12 = (1.0 / 12.0) * (2.0 * (lNS + lWE) + lWc + lEc);\n"
        "    float sub = clamp(abs(lAvg12 - lM) / range, 0.0, 1.0);\n"
        "    float subFinal = (-2.0 * sub + 3.0) * sub * sub; subFinal = subFinal * subFinal * 0.75;\n"
        "    finalOffset = max(finalOffset, subFinal);\n"
        "    float2 finalUv = uv; if (horizontal) finalUv.y += finalOffset * step; else finalUv.x += finalOffset * step;\n"
        "    return float4(source.Sample(smp, finalUv).rgb, 1.0); }\n";

    // The copy of a multisampled target, which has no sampler: the texel
    // by position, a colour as the average of its samples, a depth as its
    // first, as the console's own resolve took one. SAMPLES is defined
    // when this is compiled, once for each count.
    const char* const CopyMultisampledSource =
        "cbuffer Quad : register(b0) { float4 uvRect; float4 texel; };\n"
        "struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
        "Texture2DMS<float4, SAMPLES> sourceMS : register(t0);\n"
        "float4 copyColourMS(V v) : SV_Target { int2 p = int2(v.uv / texel.xy); float4 sum = 0; [unroll] for (int i = 0; i < SAMPLES; i++) sum += sourceMS.Load(p, i); return sum / SAMPLES; }\n"
        "float4 copyDepthMS(V v) : SV_Target { int2 p = int2(v.uv / texel.xy); return sourceMS.Load(p, 0); }\n";

    // COD3_D3DFLAT=1: every opaque draw in a flat colour made from its pixel
    // program's hash, so a frame shows which program painted what.
    const char* const FlatSource =
        "cbuffer DrawConstants : register(b2) { float4 viewportScale; float4 viewportOffset; float4 targetSize; uint4 flags; };\n"
        "float4 main() : SV_Target { uint h = flags.w; return float4(float(h & 255u) / 255.0, float((h >> 8) & 255u) / 255.0, float((h >> 16) & 255u) / 255.0, 1.0); }\n";

    ComPtr<ID3DBlob> CompileBuiltIn(const char* source, const char* entry, const char* profile, const char* what, const D3D_SHADER_MACRO* defines = nullptr)
    {
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = g_compile(source, strlen(source), what, defines, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr))
        {
            printf("render: the %s did not compile: %s\n", what, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
            return nullptr;
        }
        return code;
    }

    bool BuiltInPrograms()
    {
        if (g_quadVertexShader) return true;
        if (ComPtr<ID3DBlob> code = CompileBuiltIn(RectangleSource, "main", "gs_5_0", "rectangle list program"))
            g_device->CreateGeometryShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_rectangleShader);
        if (ComPtr<ID3DBlob> code = CompileBuiltIn(QuadSource, "vsmain", "vs_5_0", "quad program"))
            g_device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_quadVertexShader);
        if (ComPtr<ID3DBlob> code = CompileBuiltIn(QuadSource, "copy", "ps_5_0", "copy program"))
            g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_copyPixelShader);
        for (int i = 1; i <= 3; i++)
        {
            const char* const counts[4] = { "1", "2", "4", "8" };
            const D3D_SHADER_MACRO defines[2] = { { "SAMPLES", counts[i] }, { nullptr, nullptr } };
            if (ComPtr<ID3DBlob> code = CompileBuiltIn(CopyMultisampledSource, "copyColourMS", "ps_5_0", "multisampled copy program", defines))
                g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_copyColourMS[i]);
            if (ComPtr<ID3DBlob> code = CompileBuiltIn(CopyMultisampledSource, "copyDepthMS", "ps_5_0", "multisampled depth copy program", defines))
                g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_copyDepthMS[i]);
        }
        if (ComPtr<ID3DBlob> code = CompileBuiltIn(QuadSource, "present", "ps_5_0", "present program"))
            g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_presentPixelShader);
        if (ComPtr<ID3DBlob> code = CompileBuiltIn(QuadSource, "fxaa", "ps_5_0", "anti aliasing program"))
            g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_fxaaPixelShader);
        if (getenv("COD3_D3DFLAT") != nullptr)
            if (ComPtr<ID3DBlob> code = CompileBuiltIn(FlatSource, "main", "ps_5_0", "flat program"))
                g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_flatPixelShader);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        g_device->CreateSamplerState(&sampler, &g_pointSampler);
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        g_device->CreateSamplerState(&sampler, &g_linearSampler);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 32;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        g_device->CreateBuffer(&desc, nullptr, &g_quadConstants);
        return g_quadVertexShader && g_copyPixelShader && g_presentPixelShader;
    }

    // Draws `source`'s rectangle (in texels of the source) over the whole
    // of the target view of the given size, through the pixel program.
    void DrawQuad(ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight,
                  float x0, float y0, float width, float height,
                  ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport,
                  ID3D11PixelShader* program, ID3D11SamplerState* sampler)
    {
        if (!BuiltInPrograms()) return;
        const float constants[8] = { x0 / sourceWidth, y0 / sourceHeight, width / sourceWidth, height / sourceHeight,
                                     1.0f / sourceWidth, 1.0f / sourceHeight, 0.0f, 0.0f };
        g_context->UpdateSubresource(g_quadConstants.Get(), 0, nullptr, constants, 0, 0);
        // Every texture the draws had bound goes, so the target is not
        // among them and the bindings forgotten below are truly gone.
        ID3D11ShaderResourceView* none[32] = {};
        g_context->PSSetShaderResources(0, 32, none);
        g_context->VSSetShaderResources(0, 32, none);
        ID3D11RenderTargetView* targets[1] = { target };
        g_context->OMSetRenderTargets(1, targets, nullptr);
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(RenderPipeline::RasterizerObject(RenderPipeline::Rasterizer(0, false, true)));
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(nullptr, 0);
        g_context->VSSetShader(g_quadVertexShader.Get(), nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(program, nullptr, 0);
        ID3D11Buffer* buffers[1] = { g_quadConstants.Get() };
        g_context->VSSetConstantBuffers(0, 1, buffers);
        g_context->PSSetConstantBuffers(0, 1, buffers);
        ID3D11ShaderResourceView* sources[1] = { source };
        g_context->PSSetShaderResources(0, 1, sources);
        ID3D11SamplerState* samplers[1] = { sampler };
        g_context->PSSetSamplers(0, 1, samplers);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(nullptr);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 32, none);
        ID3D11RenderTargetView* noTargets[1] = { nullptr };
        g_context->OMSetRenderTargets(1, noTargets, nullptr);
        // Everything the draws had bound is gone.
        ForgetBindings();
    }

    // --- starting --------------------------------------------------------------------------------

    bool Start()
    {
        if (g_started) return !g_failed;
        g_started = true;

        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        if (compiler != nullptr) g_compile = reinterpret_cast<CompileFunction>(GetProcAddress(compiler, "D3DCompile"));
        if (g_compile == nullptr)
        {
            printf("render: d3dcompiler_47.dll is not available; nothing can be drawn\n");
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
            printf("render: no Direct3D 11 device (0x%08lX); nothing can be drawn\n", hr);
            g_failed = true;
            return false;
        }
        g_context.As(&g_context1);
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory5> factory;
            DXGI_ADAPTER_DESC description{};
            if (SUCCEEDED(g_device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            {
                adapter->GetDesc(&description);
                if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))))
                {
                    BOOL allowed = FALSE;
                    if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))))
                        g_tearing = allowed != FALSE;
                }
            }
            printf("render: Direct3D 11 on %ls%s%s\n", description.Description,
                (flags & D3D11_CREATE_DEVICE_DEBUG) ? ", with the debug layer" : "", g_tearing ? ", tearing allowed" : "");
        }
        RenderPipeline::Initialize(g_device.Get());
        RenderShaders::Initialize(g_device.Get());
        RenderResources::Initialize(g_device.Get(), g_context.Get());
        RenderStats::Initialize(g_device.Get(), g_context.Get());
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 256;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            g_device->CreateBuffer(&desc, nullptr, &g_boolConstants);
            desc.ByteWidth = 4096;
            g_device->CreateBuffer(&desc, nullptr, &g_floatConstants[0]);
            g_device->CreateBuffer(&desc, nullptr, &g_floatConstants[1]);
            desc.ByteWidth = (sizeof(DrawConstants) + 15) & ~15u;
            g_device->CreateBuffer(&desc, nullptr, &g_drawConstants);
        }
        RenderShaders::Precompile();
        RenderStats::BeginFrame();
        (void)RenderResources::BeginFrame(g_frame);
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

    // --- the frame log -------------------------------------------------------------------------------

    // COD3_D3DFRAME=N: the draws, resolves and presents from the Nth swap
    // to the one after next are printed. COD3_D3DFRAME=auto picks the
    // frame itself: the one after the first swap with three hundred draws
    // or more, ten seconds into a level. COD3_D3DFRAME=loading: the first
    // frame of the loading screen with anything on it.
    std::atomic<int64_t> g_frameWanted{ -1 };
    long g_autoSeconds = 10;
    uint64_t g_drawsAtSwap = 0, g_drawsTotal = 0;
    int g_dumpNumber = 0;

    bool FrameLogged()
    {
        static const bool parsed = []() {
            const char* text = getenv("COD3_D3DFRAME");
            // COD3_RENDER_DEBUG=1 alone: the auto frame.
            if (text == nullptr && RenderStats::Debug()) text = "auto";
            if (text == nullptr) g_frameWanted.store(-1);
            else if (strncmp(text, "auto", 4) == 0)
            {
                g_frameWanted.store(-2);
                if (text[4] != 0) g_autoSeconds = strtol(text + 4, nullptr, 10);
            }
            else if (strcmp(text, "loading") == 0) g_frameWanted.store(-3);
            else g_frameWanted.store(strtol(text, nullptr, 10));
            return true;
        }();
        (void)parsed;
        const int64_t wanted = g_frameWanted.load(std::memory_order_relaxed);
        if (wanted < 0) return false;
        const int64_t swap = int64_t(g_swaps.load(std::memory_order_relaxed));
        return swap >= wanted && swap < wanted + 2;
    }

    void FrameSwapped()
    {
        const uint64_t inFrame = g_drawsTotal - g_drawsAtSwap;
        g_drawsAtSwap = g_drawsTotal;
        const uint64_t swap = g_swaps.load(std::memory_order_relaxed);
        static int64_t levelSince = 0;
        if (levelSince == 0 && Kernel::Stats().filesOpened.load(std::memory_order_relaxed) >= 40)
            levelSince = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool inLevel = levelSince != 0 &&
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - levelSince >= g_autoSeconds;
        static const uint64_t minDraws = []() { const char* t = getenv("COD3_D3DFRAME_MINDRAWS"); return t ? uint64_t(strtoull(t, nullptr, 10)) : 300ull; }();
        if (g_frameWanted.load(std::memory_order_relaxed) == -2 && inLevel && inFrame >= minDraws)
        {
            g_frameWanted.store(int64_t(swap) + 1);
            printf("render: frame %llu had %llu draws, the next two are logged\n", (unsigned long long)swap, (unsigned long long)inFrame);
        }
        if (g_frameWanted.load(std::memory_order_relaxed) == -3 && Kernel::Stats().filesOpened.load(std::memory_order_relaxed) >= 22 && inFrame >= 3)
        {
            g_frameWanted.store(int64_t(swap) + 1);
            printf("render: loading frame %llu had %llu draws, the next two are logged\n", (unsigned long long)swap, (unsigned long long)inFrame);
        }
    }

    void WriteBmp(const char* name, const std::vector<uint8_t>& pixels, uint32_t w, uint32_t h)
    {
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = int(w);
        info.biHeight = -int(h);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        info.biSizeImage = DWORD(pixels.size());
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + info.biSizeImage;
        if (FILE* out = fopen(name, "wb"))
        {
            fwrite(&file, sizeof(file), 1, out);
            fwrite(&info, sizeof(info), 1, out);
            fwrite(pixels.data(), 1, pixels.size(), out);
            fclose(out);
        }
    }

    // A texture read back at a quarter size, as B G R A rows; empty when it
    // could not be.
    std::vector<uint8_t> ReadBack(ID3D11Texture2D* texture, uint32_t& w, uint32_t& h, uint32_t shrink)
    {
        std::vector<uint8_t> pixels;
        if (texture == nullptr) return pixels;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return pixels;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return pixels;
        g_context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return pixels;
        w = desc.Width / shrink;
        h = desc.Height / shrink;
        pixels.resize(size_t(w) * h * 4);
        const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
        for (uint32_t y = 0; y < h; y++)
        {
            const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + size_t(y) * shrink * mapped.RowPitch;
            for (uint32_t x = 0; x < w; x++)
            {
                const uint8_t* from = row + size_t(x) * shrink * 4;
                uint8_t* to = pixels.data() + (size_t(y) * w + x) * 4;
                to[0] = rgba ? from[2] : from[0]; to[1] = from[1]; to[2] = rgba ? from[0] : from[2]; to[3] = 255;
            }
        }
        g_context->Unmap(staging.Get(), 0);
        return pixels;
    }

    // The depth alongside, as grey on a log scale, when COD3_D3DDRAWDUMPZ is set.
    void DumpDepth(const char* prefix, int number, ID3D11Texture2D* depth)
    {
        if (depth == nullptr) return;
        D3D11_TEXTURE2D_DESC desc{};
        depth->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_R32G8X24_TYPELESS) return;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return;
        g_context->CopyResource(staging.Get(), depth);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
        const uint32_t w = desc.Width / 4, h = desc.Height / 4;
        std::vector<uint8_t> pixels(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; y++)
        {
            const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + size_t(y) * 4 * mapped.RowPitch;
            for (uint32_t x = 0; x < w; x++)
            {
                float z;
                memcpy(&z, row + size_t(x) * 32, 4);
                const float shade = z <= 0.0f ? 0.0f : z >= 1.0f ? 1.0f : 1.0f + std::log10(z) / 6.0f;
                const uint8_t grey = uint8_t(std::min(std::max(shade, 0.0f), 1.0f) * 255.0f);
                uint8_t* to = pixels.data() + (size_t(y) * w + x) * 4;
                to[0] = grey; to[1] = grey; to[2] = z < 0.0f ? 255 : z > 1.0f ? 0 : grey; to[3] = 255;
            }
        }
        g_context->Unmap(staging.Get(), 0);
        char name[512];
        snprintf(name, sizeof(name), "%s-%04d-z.bmp", prefix, number);
        WriteBmp(name, pixels, w, h);
    }

    // COD3_D3DDRAWDUMP=prefix, with COD3_D3DFRAME: the colour target after
    // every draw of the logged frame, at a quarter size, as prefix-NNNN.bmp,
    // so a frame can be watched being built. COD3_D3DDRAWDUMPFROM=N skips
    // the first N draws of the frame.
    void DumpTargetAfterDraw(ID3D11Texture2D* texture, ID3D11Texture2D* depth)
    {
        static const char* const prefix = getenv("COD3_D3DDRAWDUMP");
        if (prefix == nullptr || texture == nullptr || !FrameLogged()) return;
        static const uint32_t from = []() { const char* t = getenv("COD3_D3DDRAWDUMPFROM"); return t ? uint32_t(strtoul(t, nullptr, 10)) : 0u; }();
        int& number = g_dumpNumber;
        if (number < int(from)) { number++; return; }
        if (number >= int(from) + 2000) return;
        uint32_t w = 0, h = 0;
        const std::vector<uint8_t> pixels = ReadBack(texture, w, h, 4);
        if (pixels.empty()) return;
        static const bool withDepth = getenv("COD3_D3DDRAWDUMPZ") != nullptr;
        if (withDepth) DumpDepth(prefix, number, depth);
        char name[512];
        snprintf(name, sizeof(name), "%s-%04d.bmp", prefix, number++);
        WriteBmp(name, pixels, w, h);
    }

    // COD3_FRAMEDUMP=path: the window's picture as a bitmap every
    // COD3_FRAMEDUMP_EVERY presents, cycling through COD3_FRAMEDUMP_KEEP files.
    void DumpFrame(ID3D11Texture2D* back)
    {
        static const char* const path = getenv("COD3_FRAMEDUMP");
        if (path == nullptr) return;
        static const int every = []() { const char* t = getenv("COD3_FRAMEDUMP_EVERY"); const int v = t ? int(strtol(t, nullptr, 10)) : 120; return v > 0 ? v : 120; }();
        static const int keep = []() { const char* t = getenv("COD3_FRAMEDUMP_KEEP"); const int v = t ? int(strtol(t, nullptr, 10)) : 40; return v > 0 ? v : 40; }();
        static int counter = 0;
        if ((counter++ % every) != 0) return;
        uint32_t w = 0, h = 0;
        const std::vector<uint8_t> pixels = ReadBack(back, w, h, 1);
        if (pixels.empty()) return;
        char name[512];
        snprintf(name, sizeof(name), "%s-%03d.bmp", path, (counter / every) % keep);
        printf("render: frame dump %03d at present %d, swap %llu\n", (counter / every) % keep, counter - 1, (unsigned long long)g_swaps.load(std::memory_order_relaxed));
        WriteBmp(name, pixels, w, h);
    }

    // --- the draw ------------------------------------------------------------------------------------

    enum SkipReason { SkipBackendOff, SkipPrimitive, SkipProgram, SkipProgramBuilding, SkipProgramOnRequest, SkipPitch, SkipTarget, SkipScissor, SkipBuffer, SkipCount };
    const char* const SkipNames[SkipCount] = { "render backend off", "primitive type", "program", "program building", "program skipped on request", "no surface pitch", "no target", "empty scissor", "vertex buffer" };
    uint64_t g_skips[SkipCount] = {};

    void Skip(SkipReason reason)
    {
        g_skips[reason]++;
        RenderStats::Frame().skipped++;
    }

    // Scratch for a draw's indices, kept between draws: no allocation once
    // it has grown to the largest draw.
    std::vector<uint32_t> g_indices32;
    std::vector<uint16_t> g_indices16;

    inline uint32_t BSwap16(uint16_t v) { return uint16_t((v >> 8) | (v << 8)); }

    struct Timed
    {
        uint64_t& into;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        explicit Timed(uint64_t& i) : into(i) {}
        ~Timed() { into += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count()); }
    };

    void BindTexture(int stage, uint32_t slot, ID3D11ShaderResourceView* view)
    {
        if (g_bound.textures[stage][slot] == view) return;
        g_bound.textures[stage][slot] = view;
        RenderStats::Frame().textureSwitches++;
        if (stage == 0) g_context->PSSetShaderResources(slot, 1, &view);
        else g_context->VSSetShaderResources(slot, 1, &view);
    }

    void BindSampler(int stage, uint32_t slot, ID3D11SamplerState* sampler)
    {
        if (g_bound.samplers[stage][slot] == sampler) return;
        g_bound.samplers[stage][slot] = sampler;
        if (stage == 0) g_context->PSSetSamplers(slot, 1, &sampler);
        else g_context->VSSetSamplers(slot, 1, &sampler);
    }

    // A view about to be a target cannot stay bound as a texture: the
    // host refuses and binds nothing. Only the slots holding it are cleared.
    void UnbindResource(ID3D11ShaderResourceView* view)
    {
        if (view == nullptr) return;
        for (int stage = 0; stage < 2; stage++)
            for (uint32_t slot = 0; slot < 32; slot++)
                if (g_bound.textures[stage][slot] == view) BindTexture(stage, slot, nullptr);
    }

    void DrawLocked(uint32_t initiator, uint32_t indexBase)
    {
        RenderState::Snapshot state;
        RenderState::Read(initiator, indexBase, state);
        if (state.indexCount == 0) return;
        if (state.modeControl == 0) { Skip(SkipBackendOff); return; }
        // EDRAM mode 5 is drawn as colour and depth like mode 4: Xenia calls
        // it "depth only", but this title draws its ground, tents and jeeps
        // in it with programs that fetch the shadow maps.

        D3D11_PRIMITIVE_TOPOLOGY topology;
        bool rectangles = false, convertQuads = false, convertFan = false;
        switch (state.primitive)
        {
        case RenderState::Points:
        {
            // The host draws a point as one pixel; a wider one is noted.
            topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            static int announced = 0;
            if (((state.pointSize & 0xFFFF) > 8 || (state.pointSize >> 16) > 8) && announced++ < 4)
                printf("render: a point list of %u with a point size of %u by %u eighths of a pixel, drawn as pixels\n", state.indexCount, state.pointSize & 0xFFFF, state.pointSize >> 16);
            break;
        }
        case RenderState::Lines: topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case RenderState::LineStrip: topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case RenderState::Triangles: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case RenderState::TriangleFan: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; convertFan = true; break;
        case RenderState::TriangleStrip: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        case RenderState::Rectangles: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; rectangles = true; break;
        case RenderState::Quads: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; convertQuads = true; break;
        default: Skip(SkipPrimitive); return;
        }

        // The programs: the stage's current ones, ready or not.
        const Handle vsHandle = RenderShaders::Current(false), psHandle = RenderShaders::Current(true);
        const RenderShaders::Program* vs = RenderShaders::Get(vsHandle);
        const RenderShaders::Program* ps = RenderShaders::Get(psHandle);
        if (vs == nullptr || ps == nullptr) { Skip(SkipProgram); return; }
        const RenderShaders::State vsState = vs->state.load(std::memory_order_acquire);
        const RenderShaders::State psState = ps->state.load(std::memory_order_acquire);
        if (vsState != RenderShaders::State::Ready || psState != RenderShaders::State::Ready)
        {
            // A program that failed is said the first time a draw wants it:
            // that is what a missing thing on screen comes from.
            for (const RenderShaders::Program* program : { vs, ps })
            {
                if (program->state.load(std::memory_order_acquire) != RenderShaders::State::Failed || program->announced) continue;
                const_cast<RenderShaders::Program*>(program)->announced = true;
                printf("render: a draw wants %s program %016llx, which could not be built: %s\n",
                    program->pixel ? "pixel" : "vertex", (unsigned long long)program->hash, program->problem.empty() ? "the compile failed" : program->problem.c_str());
                fflush(stdout);
            }
            Skip(vsState == RenderShaders::State::Failed || psState == RenderShaders::State::Failed ? SkipProgram : SkipProgramBuilding);
            return;
        }
        if (vs->skipped) { Skip(SkipProgramOnRequest); return; }
        if (state.pitch == 0) { Skip(SkipPitch); return; }

        // The rows the scissor reaches, so the surfaces are tall enough
        // before they are looked up.
        {
            const uint32_t tl = state.scissorTopLeft, br = state.scissorBottomRight;
            const int32_t offsetY = (tl & 0x80000000u) ? 0 : (int32_t(state.windowOffset << 1) >> 17);
            const int32_t bottom = int32_t((br >> 16) & 0x7FFF) + offsetY;
            if (bottom > 0 && RenderResources::EnsureRows(state.pitch, uint32_t(bottom))) ForgetBindings();
        }

        // Targets: only the ones the pixel program writes, each once: the
        // title leaves the other target registers pointing at the same
        // tiles, and the host refuses one surface bound twice.
        ID3D11RenderTargetView* views[4] = {};
        ID3D11Texture2D* colorTexture = nullptr;
        uint32_t targetCount = 0;
        for (uint32_t i = 0; i < 4; i++)
        {
            if (((ps->colourTargets >> i) & 1) == 0) continue;
            if (((state.colorMask >> (i * 4)) & 0xF) == 0) continue;
            const RenderResources::ColorTarget* target = RenderResources::ColorTargetOf(RenderResources::ColorTargetFor(state.colorInfo[i], state.pitch));
            if (target == nullptr) continue;
            bool duplicate = false;
            for (uint32_t j = 0; j < i; j++) if (views[j] == target->view) duplicate = true;
            if (duplicate) continue;
            views[i] = target->view;
            if (i == 0) colorTexture = target->texture;
            targetCount = i + 1;
        }
        ID3D11DepthStencilView* depthView = nullptr;
        ID3D11Texture2D* depthTexture = nullptr;
        if ((state.depthControl & 3) != 0)
        {
            if (const RenderResources::DepthTarget* depth = RenderResources::DepthTargetOf(RenderResources::DepthTargetFor(state.depthInfo, state.pitch)))
            {
                depthView = depth->view;
                depthTexture = depth->texture;
            }
        }
        if (targetCount == 0 && depthView == nullptr)
        {
            // The first few, with their state: what a draw with nowhere to
            // draw is asking for.
            static int announced = 0;
            if (announced++ < 6)
            {
                printf("render: draw with no target: primitive %u, %u indices, mode %u, colour mask %08X, depth control %08X, targets written %X, colour info %08X, depth info %08X, vs %016llx ps %016llx\n",
                    state.primitive, state.indexCount, state.modeControl, state.colorMask, state.depthControl, ps->colourTargets, state.colorInfo[0], state.depthInfo,
                    (unsigned long long)vs->hash, (unsigned long long)ps->hash);
                fflush(stdout);
            }
            Skip(SkipTarget);
            return;
        }

        // The target's host size, and the scissor in it. Bit 31 of the
        // scissor says the window offset does not apply.
        uint32_t targetWidth, targetHeight;
        float scaleX, scaleY;
        RenderResources::TargetSize(state.pitch, 4, targetWidth, targetHeight, scaleX, scaleY);
        D3D11_RECT scissor;
        {
            const uint32_t tl = state.scissorTopLeft, br = state.scissorBottomRight;
            const int32_t offsetX = (tl & 0x80000000u) ? 0 : (int32_t(state.windowOffset << 17) >> 17);
            const int32_t offsetY = (tl & 0x80000000u) ? 0 : (int32_t(state.windowOffset << 1) >> 17);
            scissor.left = std::max(0L, LONG(std::lround((int(tl & 0x7FFF) + offsetX) * scaleX)));
            scissor.top = std::max(0L, LONG(std::lround((int((tl >> 16) & 0x7FFF) + offsetY) * scaleY)));
            scissor.right = std::min(LONG(targetWidth), LONG(std::lround((int(br & 0x7FFF) + offsetX) * scaleX)));
            scissor.bottom = std::min(LONG(targetHeight), LONG(std::lround((int((br >> 16) & 0x7FFF) + offsetY) * scaleY)));
            if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) { Skip(SkipScissor); return; }
        }

        // The pipeline states, by handle.
        const Handle rasterizer = RenderPipeline::Rasterizer(state.suScModeControl, true, rectangles);
        const Handle blend = RenderPipeline::Blend(state.blendControl, state.colorMask);
        const Handle depthState = RenderPipeline::Depth(depthView ? state.depthControl : 0, state.stencilRefMask);

        // The draw's constants.
        DrawConstants& constants = g_constants;
        constants.viewportScale[0] = state.viewport[0]; constants.viewportOffset[0] = state.viewport[1];
        constants.viewportScale[1] = state.viewport[2]; constants.viewportOffset[1] = state.viewport[3];
        constants.viewportScale[2] = state.viewport[4]; constants.viewportOffset[2] = state.viewport[5];
        constants.targetSize[0] = float(state.pitch);
        constants.targetSize[1] = float(targetHeight) / scaleY;
        constants.targetSize[2] = 1.0f / float(state.pitch);
        constants.targetSize[3] = scaleY / float(targetHeight);
        constants.flags[0] = state.vteControl;
        constants.flags[1] = ((state.colorControl >> 3) & 1) ? (state.colorControl & 7) : 7;   // the alpha test, or always
        constants.flags[2] = state.alphaReference;
        constants.flags[3] = uint32_t(ps->hash);

        // The targets are set before the textures so a surface about to be
        // drawn into is not still bound as one.
        {
            bool same = targetCount == g_bound.targetCount && depthView == g_bound.depthView;
            for (uint32_t i = 0; same && i < 4; i++) same = views[i] == g_bound.targets[i];
            if (!same)
            {
                for (uint32_t i = 0; i < 4; i++)
                {
                    if (views[i] == nullptr) continue;
                    const RenderResources::ColorTarget* target = RenderResources::ColorTargetOf(RenderResources::ColorTargetFor(state.colorInfo[i], state.pitch));
                    if (target != nullptr) UnbindResource(target->resource);
                }
                if (depthView != nullptr)
                    if (const RenderResources::DepthTarget* depth = RenderResources::DepthTargetOf(RenderResources::DepthTargetFor(state.depthInfo, state.pitch)))
                        UnbindResource(depth->resource);
                g_context->OMSetRenderTargets(4, views, depthView);
                memcpy(g_bound.targets, views, sizeof(views));
                g_bound.depthView = depthView;
                g_bound.targetCount = targetCount;
                RenderStats::Frame().targetSwitches++;
            }
            if (g_bound.viewportWidth != targetWidth || g_bound.viewportHeight != targetHeight)
            {
                D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(targetWidth), float(targetHeight), 0.0f, 1.0f };
                g_context->RSSetViewports(1, &viewport);
                g_bound.viewportWidth = targetWidth;
                g_bound.viewportHeight = targetHeight;
            }
            if (memcmp(&scissor, &g_bound.scissor, sizeof(scissor)) != 0)
            {
                g_context->RSSetScissorRects(1, &scissor);
                g_bound.scissor = scissor;
            }
        }

        // Textures and samplers the programs sample: the pixel program's,
        // and the vertex program's (the terrain's height map).
        for (int stage = 0; stage < 2; stage++)
        {
            const RenderShaders::Program* program = stage == 0 ? ps : vs;
            for (const XenosHlsl::TextureFetch& fetch : program->textureFetches)
            {
                uint32_t words[6];
                RenderState::ReadTextureFetch(fetch.slot, words);
                uint32_t width = 1, height = 1;
                bool resolved = false;
                const Handle texture = RenderResources::TextureFor(words, width, height, resolved);
                ID3D11ShaderResourceView* view;
                if (resolved) view = RenderResources::ResolvedAt(words[1] & 0xFFFFF000u)->resource;
                else view = RenderResources::TextureView(texture, fetch.dimension);
                BindTexture(stage, fetch.slot, view);
                BindSampler(stage, fetch.sampler, RenderPipeline::SamplerObject(RenderPipeline::Sampler(words)));
                constants.textureSize[fetch.slot][0] = float(width);
                constants.textureSize[fetch.slot][1] = float(height);
                constants.textureSize[fetch.slot][2] = 1.0f / float(width);
                constants.textureSize[fetch.slot][3] = 1.0f / float(height);
                // A surface the resolve made is sampled by the title with red
                // and blue swapped: the resolve wrote the console's memory in
                // one byte order and the fetch reads it in another, and the
                // swizzle in the fetch constant undoes that. The surfaces
                // here never go through memory, so the swap is left out.
                uint32_t swizzle = (words[3] >> 1) & 0xFFF;
                if (resolved && swizzle == 0x60A && (words[1] & 0x3F) == 6) swizzle = 0x688;
                constants.textureAdjustment[fetch.slot][0] = swizzle;
                constants.textureAdjustment[fetch.slot][1] = (words[0] >> 2) & 0xFF;
            }
        }

        // The vertex buffers the vertex program fetches.
        for (const XenosHlsl::VertexFetch& fetch : vs->vertexFetches)
        {
            uint32_t word0, word1;
            RenderState::ReadVertexFetch(fetch.slot, word0, word1);
            // The programs turn every word round on the way in, which is the
            // console's usual byte order for a buffer; another would need a
            // conversion on upload, and is noted if it ever comes.
            if ((word1 & 3) != 2)
            {
                static int announced = 0;
                if (announced++ < 4) printf("render: a vertex buffer in byte order %u, drawn as order 2\n", word1 & 3);
            }
            const Handle buffer = RenderResources::VertexBufferFor(word0 & ~3u, ((word1 >> 2) & 0xFFFFFF) * 4, 2);
            ID3D11ShaderResourceView* view = RenderResources::VertexBufferView(buffer);
            if (view == nullptr) { Skip(SkipBuffer); return; }
            if (g_bound.vertexBuffers[fetch.slot] != view)
            {
                g_bound.vertexBuffers[fetch.slot] = view;
                g_context->VSSetShaderResources(32 + fetch.slot, 1, &view);
            }
        }

        // Constants: the float files, the booleans, and the draw's own.
        // A file goes up when something the program reads was written
        // since it last went up, and only as much of it as the program
        // reads: the title writes a few constants before most draws, and
        // the whole four kilobytes a draw was most of what a draw cost.
        // The blocks go through the ring in one map, bound at offsets.
        {
            const bool ring = RenderResources::ConstantOffsetsAvailable();
            const std::atomic<uint32_t>* file = Gpu::RegisterFile();
            // What has been written and not yet uploaded, in float4s.
            static uint32_t pendingFirst[2] = { 0, 0 }, pendingEnd[2] = { 256, 256 };
            for (uint32_t range = 0; range < 2; range++)
            {
                uint32_t spanFirst, spanEnd;
                if (!Gpu::TakeConstantSpan(range, spanFirst, spanEnd)) continue;
                pendingFirst[range] = std::min(pendingFirst[range], spanFirst / 4);
                pendingEnd[range] = std::max(pendingEnd[range], (spanEnd + 3) / 4);
            }
            {
                uint32_t spanFirst, spanEnd;
                if (Gpu::TakeConstantSpan(2, spanFirst, spanEnd) || !g_bound.constantsBound)
                {
                    uint32_t words[40];
                    for (uint32_t i = 0; i < 40; i++) words[i] = file[0x4900 + i].load(std::memory_order_relaxed);
                    g_context->UpdateSubresource(g_boolConstants.Get(), 0, nullptr, words, 0, 0);
                }
            }
            const bool drawChanged = memcmp(&g_lastConstants, &constants, sizeof(constants)) != 0;
            if (ring)
            {
                constexpr uint32_t DrawBytes = (sizeof(constants) + 255) & ~255u;
                uint32_t need[2], bytes = 0;
                bool upload[2];
                const RenderShaders::Program* programs[2] = { vs, ps };
                for (uint32_t stage = 0; stage < 2; stage++)
                {
                    const std::vector<uint16_t>& map = programs[stage]->constantMap;
                    const uint32_t count = map.empty() ? 256 : uint32_t(map.size());
                    need[stage] = std::min(256u, std::max(16u, (count + 15) & ~15u));
                    // The file's span the program reads, for the dirty check.
                    const uint32_t readFirst = map.empty() ? 0 : map.front(), readEnd = map.empty() ? 256 : map.back() + 1u;
                    const bool dirty = pendingFirst[stage] < readEnd && pendingEnd[stage] > readFirst;
                    upload[stage] = g_bound.floatAt[stage] == 0xFFFFFFFFu || g_bound.floatProgram[stage] != (stage == 0 ? vsHandle : psHandle) || dirty;
                    if (upload[stage]) bytes += need[stage] * 16;
                }
                bool drawUpload = drawChanged || g_bound.drawAt == 0xFFFFFFFFu;
                if (drawUpload) bytes += DrawBytes;
                if (bytes != 0)
                {
                    if (RenderResources::ConstantReserve(need[0] * 16 + need[1] * 16 + DrawBytes))
                    {
                        // A wrap: everything remembered is in the old buffer.
                        g_bound.floatAt[0] = g_bound.floatAt[1] = g_bound.drawAt = 0xFFFFFFFFu;
                        upload[0] = upload[1] = drawUpload = true;
                        bytes = need[0] * 16 + need[1] * 16 + DrawBytes;
                    }
                    uint32_t base;
                    uint8_t* to = RenderResources::ConstantMap(bytes, base);
                    if (to == nullptr) { Skip(SkipBuffer); return; }
                    uint32_t at = 0;
                    for (uint32_t stage = 0; stage < 2; stage++)
                    {
                        if (!upload[stage]) continue;
                        const uint32_t first = stage == 0 ? 0x4000 : 0x4400;
                        uint32_t* words = reinterpret_cast<uint32_t*>(to + at);
                        const std::vector<uint16_t>& map = programs[stage]->constantMap;
                        if (map.empty())
                            for (uint32_t i = 0; i < 1024; i++) words[i] = file[first + i].load(std::memory_order_relaxed);
                        else
                            for (size_t i = 0; i < map.size(); i++)
                            {
                                const std::atomic<uint32_t>* from = file + first + map[i] * 4u;
                                words[i * 4 + 0] = from[0].load(std::memory_order_relaxed);
                                words[i * 4 + 1] = from[1].load(std::memory_order_relaxed);
                                words[i * 4 + 2] = from[2].load(std::memory_order_relaxed);
                                words[i * 4 + 3] = from[3].load(std::memory_order_relaxed);
                            }
                        g_bound.floatAt[stage] = base + at;
                        g_bound.floatCount[stage] = need[stage];
                        g_bound.floatProgram[stage] = stage == 0 ? vsHandle : psHandle;
                        at += need[stage] * 16;
                        // Nothing is pending after an upload: a change of
                        // program uploads anyway, and the same program only
                        // needs what is written from here on.
                        pendingFirst[stage] = 256; pendingEnd[stage] = 0;
                    }
                    if (drawUpload)
                    {
                        memcpy(to + at, &constants, sizeof(constants));
                        g_lastConstants = constants;
                        g_bound.drawAt = base + at;
                        at += DrawBytes;
                    }
                    RenderResources::ConstantUnmap();
                    RenderStats::Frame().streamBytes += at;
                }
                // Bound again whenever an offset moved: the binding carries it.
                if (!g_bound.constantsBound || g_bound.boundFloatAt[0] != g_bound.floatAt[0] || g_bound.boundFloatAt[1] != g_bound.floatAt[1] ||
                    g_bound.boundDrawAt != g_bound.drawAt || g_bound.boundFloatCount[0] != g_bound.floatCount[0] || g_bound.boundFloatCount[1] != g_bound.floatCount[1])
                {
                    ID3D11Buffer* ringBuffer = RenderResources::ConstantRing();
                    ID3D11Buffer* bools = g_boolConstants.Get();
                    for (int stage = 0; stage < 2; stage++)
                    {
                        ID3D11Buffer* buffers[3] = { ringBuffer, bools, ringBuffer };
                        const UINT firsts[3] = { g_bound.floatAt[stage] / 16, 0, g_bound.drawAt / 16 };
                        const UINT counts[3] = { g_bound.floatCount[stage], 16, DrawBytes / 16 };
                        if (stage == 0) g_context1->VSSetConstantBuffers1(0, 3, buffers, firsts, counts);
                        else g_context1->PSSetConstantBuffers1(0, 3, buffers, firsts, counts);
                    }
                    g_bound.boundFloatAt[0] = g_bound.floatAt[0]; g_bound.boundFloatAt[1] = g_bound.floatAt[1];
                    g_bound.boundFloatCount[0] = g_bound.floatCount[0]; g_bound.boundFloatCount[1] = g_bound.floatCount[1];
                    g_bound.boundDrawAt = g_bound.drawAt;
                    g_bound.constantsBound = true;
                }
            }
            else
            {
                // No offsets on this device: the whole files, in place.
                uint32_t words[1024];
                for (uint32_t stage = 0; stage < 2; stage++)
                {
                    if (pendingFirst[stage] >= 256 && g_bound.constantsBound) continue;
                    const uint32_t first = stage == 0 ? 0x4000 : 0x4400;
                    for (uint32_t i = 0; i < 1024; i++) words[i] = file[first + i].load(std::memory_order_relaxed);
                    g_context->UpdateSubresource(g_floatConstants[stage].Get(), 0, nullptr, words, 0, 0);
                    pendingFirst[stage] = 256; pendingEnd[stage] = 0;
                    RenderStats::Frame().streamBytes += 4096;
                }
                if (drawChanged || !g_bound.constantsBound)
                {
                    g_lastConstants = constants;
                    g_context->UpdateSubresource(g_drawConstants.Get(), 0, nullptr, &constants, 0, 0);
                }
                if (!g_bound.constantsBound)
                {
                    ID3D11Buffer* buffers[3] = { g_floatConstants[0].Get(), g_boolConstants.Get(), g_drawConstants.Get() };
                    g_context->VSSetConstantBuffers(0, 3, buffers);
                    ID3D11Buffer* pixelBuffers[3] = { g_floatConstants[1].Get(), g_boolConstants.Get(), g_drawConstants.Get() };
                    g_context->PSSetConstantBuffers(0, 3, pixelBuffers);
                    g_bound.constantsBound = true;
                }
            }
        }

        // The programs and the states, when they changed.
        if (g_bound.vertexShader != vsHandle)
        {
            g_context->VSSetShader(static_cast<ID3D11VertexShader*>(vs->shader), nullptr, 0);
            g_bound.vertexShader = vsHandle;
            RenderStats::Frame().shaderSwitches++;
        }
        {
            // Only the opaque draws go flat: a blended one in a flat colour
            // with an alpha of one would cover the frame.
            const bool flat = g_flatPixelShader && state.blendControl[0] == 0x00010001;
            if (g_bound.pixelShader != psHandle || g_bound.flat != flat)
            {
                g_context->PSSetShader(flat ? g_flatPixelShader.Get() : static_cast<ID3D11PixelShader*>(ps->shader), nullptr, 0);
                g_bound.pixelShader = psHandle;
                g_bound.flat = flat;
                RenderStats::Frame().shaderSwitches++;
            }
        }
        if (g_bound.rectangles != rectangles)
        {
            if (!BuiltInPrograms()) return;
            g_context->GSSetShader(rectangles ? g_rectangleShader.Get() : nullptr, nullptr, 0);
            g_bound.rectangles = rectangles;
        }
        if (g_bound.rasterizer != rasterizer)
        {
            g_context->RSSetState(RenderPipeline::RasterizerObject(rasterizer));
            g_bound.rasterizer = rasterizer;
            RenderStats::Frame().pipelineSwitches++;
        }
        if (g_bound.blend != blend || memcmp(g_bound.blendFactor, state.blendFactor, sizeof(state.blendFactor)) != 0)
        {
            g_context->OMSetBlendState(RenderPipeline::BlendObject(blend), state.blendFactor, 0xFFFFFFFF);
            g_bound.blend = blend;
            memcpy(g_bound.blendFactor, state.blendFactor, sizeof(state.blendFactor));
            RenderStats::Frame().pipelineSwitches++;
        }
        {
            const uint32_t reference = state.stencilRefMask & 0xFF;
            if (g_bound.depth != depthState || g_bound.stencilReference != reference)
            {
                g_context->OMSetDepthStencilState(RenderPipeline::DepthObject(depthState), reference);
                g_bound.depth = depthState;
                g_bound.stencilReference = reference;
                RenderStats::Frame().pipelineSwitches++;
            }
        }
        if (g_bound.topology != topology)
        {
            g_context->IASetPrimitiveTopology(topology);
            g_bound.topology = topology;
        }

        // Indices: the title's, byte swapped, or none. Quads and fans become
        // triangle lists. The index that ends a strip and starts the next,
        // when the title has turned it on (PA_SU_SC_MODE_CNTL bit 21), is the
        // host's own cut index when it is all ones, and is rewritten to it
        // when it is not.
        uint32_t indexCount = state.indexCount;
        bool indexed = false, indices32 = false;
        uint32_t ringOffset = 0;
        const bool strips = topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP || topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        const bool resetEnabled = strips && (state.suScModeControl & (1u << 21)) != 0;
        if (state.sourceSelect == 0)
        {
            indexed = true;
            const uint8_t* data = Guest::Base + Guest::PhysicalAlias(state.indexBase);
            if (state.wideIndices)
            {
                indices32 = true;
                g_indices32.resize(indexCount);
                const uint32_t* from = reinterpret_cast<const uint32_t*>(data);
                for (uint32_t i = 0; i < indexCount; i++) g_indices32[i] = _byteswap_ulong(from[i]);
                if (resetEnabled)
                {
                    const uint32_t reset = state.resetIndex & 0xFFFFFFu;
                    for (uint32_t& index : g_indices32) if ((index & 0xFFFFFFu) == reset) index = 0xFFFFFFFFu;
                }
            }
            else
            {
                const uint16_t* from = reinterpret_cast<const uint16_t*>(data);
                const uint32_t reset = state.resetIndex & 0xFFFFu;
                if (resetEnabled && reset != 0xFFFFu)
                {
                    // Widened, so the cut index can be the host's.
                    indices32 = true;
                    g_indices32.resize(indexCount);
                    for (uint32_t i = 0; i < indexCount; i++)
                    {
                        const uint32_t index = BSwap16(from[i]);
                        g_indices32[i] = index == reset ? 0xFFFFFFFFu : index;
                    }
                }
                else
                {
                    g_indices16.resize(indexCount);
                    for (uint32_t i = 0; i < indexCount; i++) g_indices16[i] = uint16_t(BSwap16(from[i]));
                }
            }
        }
        if (convertQuads || convertFan)
        {
            // Plain draws get their vertex ids as indices first.
            if (!indexed)
            {
                indexed = true;
                indices32 = true;
                g_indices32.resize(indexCount);
                for (uint32_t i = 0; i < indexCount; i++) g_indices32[i] = i;
            }
            else if (!indices32)
            {
                indices32 = true;
                g_indices32.resize(indexCount);
                for (uint32_t i = 0; i < indexCount; i++) g_indices32[i] = g_indices16[i];
            }
            static std::vector<uint32_t> list;
            list.clear();
            if (convertQuads)
                for (uint32_t i = 0; i + 3 < indexCount; i += 4)
                {
                    const uint32_t q[6] = { g_indices32[i], g_indices32[i + 1], g_indices32[i + 2], g_indices32[i], g_indices32[i + 2], g_indices32[i + 3] };
                    list.insert(list.end(), q, q + 6);
                }
            else
                for (uint32_t i = 1; i + 1 < indexCount; i++)
                {
                    const uint32_t t[3] = { g_indices32[0], g_indices32[i], g_indices32[i + 1] };
                    list.insert(list.end(), t, t + 3);
                }
            g_indices32.swap(list);
            indexCount = uint32_t(g_indices32.size());
            if (indexCount == 0) return;
        }
        if (indexed)
        {
            const uint32_t bytes = indices32 ? indexCount * 4 : indexCount * 2;
            ringOffset = RenderResources::IndexAppend(indices32 ? static_cast<const void*>(g_indices32.data()) : static_cast<const void*>(g_indices16.data()), bytes);
            if (ringOffset == 0xFFFFFFFFu) { Skip(SkipBuffer); return; }
            RenderStats::Frame().streamBytes += bytes;
            if (!g_bound.indexBufferSet || g_bound.indices32 != indices32)
            {
                g_context->IASetIndexBuffer(RenderResources::IndexRing(), indices32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
                g_bound.indexBufferSet = true;
                g_bound.indices32 = indices32;
            }
        }

        RenderStats::Counters& counters = RenderStats::Frame();
        counters.draws++;
        g_drawsTotal++;
        switch (topology)
        {
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST: counters.triangles += rectangles ? indexCount / 3 * 2 : indexCount / 3; break;
        case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: counters.triangles += indexCount >= 2 ? indexCount - 2 : 0; break;
        default: break;
        }

        const bool logged = FrameLogged();
        if (logged)
        {
            printf("frame %llu (dump %d): draw %u of %u indices (%s), vte %03X viewport %g %g %g %g z %g %g, target %u %ux%u, scissor %ld,%ld-%ld,%ld, "
                   "depth %08X blend %08X mask %X, mode %u, cull %08X, alpha %08X, vs %016llx ps %016llx\n",
                (unsigned long long)g_swaps.load(std::memory_order_relaxed), g_dumpNumber, state.primitive, state.indexCount, indexed ? "indexed" : "plain",
                state.vteControl, state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3], state.viewport[4], state.viewport[5],
                targetCount, targetWidth, targetHeight, scissor.left, scissor.top, scissor.right, scissor.bottom,
                state.depthControl, state.blendControl[0], state.colorMask, state.modeControl, state.suScModeControl, state.colorControl,
                (unsigned long long)vs->hash, (unsigned long long)ps->hash);
            for (int stage = 0; stage < 2; stage++)
            {
                const RenderShaders::Program* program = stage == 0 ? ps : vs;
                for (const XenosHlsl::TextureFetch& fetch : program->textureFetches)
                {
                    uint32_t w[6];
                    RenderState::ReadTextureFetch(fetch.slot, w);
                    uint32_t tw, th; bool tr;
                    const bool white = RenderResources::TextureFor(w, tw, th, tr) == 1 && !tr;
                    printf("   %stexture %u (read as %uD)%s%s: format %u %ux%u at %08X%s swizzle %03X signs %02X, words %08X %08X %08X %08X %08X %08X\n", stage == 0 ? "" : "vs ", fetch.slot, fetch.dimension == 3 ? 6 : fetch.dimension + 1, white ? " WHITE: " : "", white ? RenderResources::WhiteReason() : "", w[1] & 0x3F,
                        (w[2] & 0x1FFF) + 1, ((w[2] >> 13) & 0x1FFF) + 1, w[1] & 0xFFFFF000u, RenderResources::ResolvedAt(w[1] & 0xFFFFF000u) ? " (resolved)" : "",
                        (w[3] >> 1) & 0xFFF, (w[0] >> 2) & 0xFF, w[0], w[1], w[2], w[3], w[4], w[5]);
                }
            }
            for (const XenosHlsl::VertexFetch& fetch : vs->vertexFetches)
            {
                uint32_t word0, word1;
                RenderState::ReadVertexFetch(fetch.slot, word0, word1);
                printf("   vertex slot %u at %08X, %u dwords, endian %u, stride %u\n", fetch.slot, word0 & ~3u, (word1 >> 2) & 0xFFFFFF, word1 & 3, fetch.stride);
            }
            fflush(stdout);
        }

        // VGT_INDX_OFFSET is added to every vertex index, which is how the
        // terrain draws its patches out of one buffer with one set of
        // indices; the host adds it as the base vertex.
        if (indexed) g_context->DrawIndexed(indexCount, ringOffset / (indices32 ? 4 : 2), state.indexOffset);
        else g_context->Draw(indexCount, UINT(state.indexOffset));

        if (logged && colorTexture != nullptr) DumpTargetAfterDraw(colorTexture, depthTexture);
    }

    // --- the resolve ----------------------------------------------------------------------------------

    void ResolveLocked()
    {
        const std::atomic<uint32_t>* file = Gpu::RegisterFile();
        auto reg = [&](uint32_t index) { return file[index].load(std::memory_order_relaxed); };
        const uint32_t control = reg(RenderState::CopyControl);
        const uint32_t destBase = reg(RenderState::CopyDestBase);
        const uint32_t pitch = reg(RenderState::SurfaceInfo) & 0x3FFF;
        const uint32_t tl = reg(RenderState::ScissorTopLeft), br = reg(RenderState::ScissorBottomRight);
        if (pitch == 0) return;

        const uint32_t x0 = tl & 0x7FFF, y0 = (tl >> 16) & 0x7FFF;
        const uint32_t x1 = br & 0x7FFF, y1 = (br >> 16) & 0x7FFF;
        if (x1 <= x0 || y1 <= y0 || x1 - x0 > 4096 || y1 - y0 > 4096) return;
        const uint32_t width = x1 - x0, height = y1 - y0;

        const uint32_t sourceSelect = control & 7;
        const uint32_t command = (control >> 20) & 3;
        const bool colorClear = ((control >> 8) & 1) != 0;
        const bool depthClear = ((control >> 9) & 1) != 0;
        RenderStats::Frame().resolves++;
        if (RenderResources::EnsureRows(pitch, y1)) ForgetBindings();

        uint32_t targetWidth, targetHeight;
        float scaleX, scaleY;
        RenderResources::TargetSize(pitch, 4, targetWidth, targetHeight, scaleX, scaleY);
        const float hostX0 = x0 * scaleX, hostY0 = y0 * scaleY;
        const uint32_t hostWidth = std::max(1u, uint32_t(std::lround(width * scaleX)));
        const uint32_t hostHeight = std::max(1u, uint32_t(std::lround(height * scaleY)));

        if (FrameLogged())
            printf("frame: resolve control %08X to %08X, %u,%u-%u,%u, pitch %u, clears %d%d, colour %08X depth %08X\n",
                control, destBase, x0, y0, x1, y1, pitch, colorClear ? 1 : 0, depthClear ? 1 : 0, reg(RenderState::ColorInfo0), reg(RenderState::DepthInfo));

        if (command != 3 && destBase != 0)
        {
            const bool fromDepth = sourceSelect == 4;
            ID3D11ShaderResourceView* source = nullptr;
            uint32_t sourceWidth = 0, sourceHeight = 0, sourceSamples = 1;
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (fromDepth)
            {
                const RenderResources::DepthTarget* depth = RenderResources::DepthTargetOf(RenderResources::DepthTargetFor(reg(RenderState::DepthInfo), pitch));
                if (depth == nullptr) return;
                source = depth->resource;
                sourceWidth = depth->width;
                sourceHeight = depth->height;
                sourceSamples = depth->samples;
                format = DXGI_FORMAT_R32_FLOAT;
            }
            else
            {
                const uint32_t info = reg(sourceSelect == 0 ? RenderState::ColorInfo0 : RenderState::ColorInfo1 + (sourceSelect - 1));
                const RenderResources::ColorTarget* color = RenderResources::ColorTargetOf(RenderResources::ColorTargetFor(info, pitch));
                if (color == nullptr) return;
                source = color->resource;
                sourceWidth = color->width;
                sourceHeight = color->height;
                sourceSamples = color->samples;
                format = color->format;
            }
            RenderResources::Resolved* out = RenderResources::ResolvedFor(destBase, hostWidth, hostHeight, format, fromDepth);
            if (out == nullptr) return;
            out->guestWidth = width;
            out->guestHeight = height;
            // The source may be bound as a texture from the draws before,
            // and the destination too: both go.
            UnbindResource(out->resource);
            const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, float(hostWidth), float(hostHeight), 0.0f, 1.0f };
            ID3D11PixelShader* copy = g_copyPixelShader.Get();
            if (sourceSamples > 1)
            {
                const int slot = sourceSamples >= 8 ? 3 : sourceSamples >= 4 ? 2 : 1;
                copy = fromDepth ? g_copyDepthMS[slot].Get() : g_copyColourMS[slot].Get();
            }
            if (copy != nullptr)
                DrawQuad(source, sourceWidth, sourceHeight, hostX0, hostY0, float(hostWidth), float(hostHeight), out->view, viewport, copy, g_pointSampler.Get());
        }

        if (colorClear)
        {
            if (const RenderResources::ColorTarget* color = RenderResources::ColorTargetOf(RenderResources::ColorTargetFor(reg(RenderState::ColorInfo0), pitch)))
            {
                const uint32_t value = reg(RenderState::CopyColorClear);
                const float rgba[4] = { ((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f, (value & 0xFF) / 255.0f, (value >> 24) / 255.0f };
                if (g_context1)
                {
                    const D3D11_RECT rect{ LONG(std::lround(hostX0)), LONG(std::lround(hostY0)), LONG(std::lround(x1 * scaleX)), LONG(std::lround(y1 * scaleY)) };
                    g_context1->ClearView(color->view, rgba, &rect, 1);
                }
                else g_context->ClearRenderTargetView(color->view, rgba);
            }
        }
        if (depthClear)
        {
            if (const RenderResources::DepthTarget* depth = RenderResources::DepthTargetOf(RenderResources::DepthTargetFor(reg(RenderState::DepthInfo), pitch)))
            {
                const uint32_t value = reg(RenderState::CopyDepthClear);
                g_context->ClearDepthStencilView(depth->view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, float(value >> 8) / 16777215.0f, UINT8(value & 0xFF));
            }
        }
    }

    // --- the present ------------------------------------------------------------------------------------

    bool SwapChainReady()
    {
        const HWND hwnd = g_window.load();
        const int width = g_clientWidth.load(), height = g_clientHeight.load();
        if (hwnd == nullptr || width <= 0 || height <= 0) return false;
        if (!g_swapChain || g_swapChainWindow != hwnd)
        {
            g_backView.Reset();
            g_backTexture.Reset();
            g_swapChain.Reset();
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            if (FAILED(g_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
                FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 3;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Flags = g_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            if (FAILED(factory->CreateSwapChainForHwnd(g_device.Get(), hwnd, &desc, nullptr, nullptr, &g_swapChain))) return false;
            // Alt+Enter is the window's own, not DXGI's exclusive mode.
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            ComPtr<IDXGISwapChain2> chain2;
            if (SUCCEEDED(g_swapChain.As(&chain2))) chain2->SetMaximumFrameLatency(2);
            g_swapChainWindow = hwnd;
            g_swapChainWidth = width;
            g_swapChainHeight = height;
        }
        else if (g_swapChainWidth != width || g_swapChainHeight != height)
        {
            g_backView.Reset();
            g_backTexture.Reset();
            g_context->OMSetRenderTargets(0, nullptr, nullptr);
            ForgetBindings();
            g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, g_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
            g_swapChainWidth = width;
            g_swapChainHeight = height;
        }
        if (!g_backView)
        {
            if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&g_backTexture)))) return false;
            g_device->CreateRenderTargetView(g_backTexture.Get(), nullptr, &g_backView);
        }
        return g_backView != nullptr;
    }

    // The last swapped frame onto the window, the overlay over it, and the
    // present with or without vertical sync.
    void PresentLocked(const Settings::Values& settings)
    {
        Timed timed(RenderStats::Frame().presentNanoseconds);
        if (!SwapChainReady() || !BuiltInPrograms()) return;
        const int clientWidth = g_swapChainWidth, clientHeight = g_swapChainHeight;
        const float black[4] = { 0, 0, 0, 1 };
        g_context->ClearRenderTargetView(g_backView.Get(), black);

        const RenderResources::Resolved* frame = RenderResources::ResolvedAt(g_frontBuffer);
        const bool fxaa = (AntiAliasing(settings) == 1 || AntiAliasing(settings) == 5) && g_fxaaPixelShader;
        if (frame != nullptr && frame->resource != nullptr)
        {
            // Sixteen by nine, as the console sends it, centred.
            const float aspect = 16.0f / 9.0f;
            float width = float(clientWidth), height = width / aspect;
            if (height > clientHeight) { height = float(clientHeight); width = height * aspect; }
            const D3D11_VIEWPORT viewport{ std::floor((clientWidth - width) * 0.5f), std::floor((clientHeight - height) * 0.5f), std::floor(width), std::floor(height), 0.0f, 1.0f };
            // The anti aliasing reads the frame itself, in its own pixels,
            // and writes into the picture's rectangle: the frame is the
            // chosen resolution, so its rows are the window's, and it has
            // no black either side to bleed a dark rim into the edges.
            DrawQuad(frame->resource, frame->width, frame->height, 0, 0, float(frame->width), float(frame->height),
                     g_backView.Get(), viewport, fxaa ? g_fxaaPixelShader.Get() : g_presentPixelShader.Get(), g_linearSampler.Get());
        }
        // The menu, the counters and the screenshot key, over the picture.
        Overlay::Initialize(g_swapChainWindow, g_device.Get(), g_context.Get());
        Overlay::Render(g_backView.Get(), g_backTexture.Get(), clientWidth, clientHeight);
        ID3D11RenderTargetView* noTargets[1] = { nullptr };
        g_context->OMSetRenderTargets(1, noTargets, nullptr);
        ForgetBindings();
        DumpFrame(g_backTexture.Get());

        RenderStats::EndGpuFrame();
        // COD3_VSYNC=0|1 over the settings.
        static const int vsyncOverride = []() { const char* t = getenv("COD3_VSYNC"); return t ? (t[0] == '0' ? 0 : 1) : -1; }();
        const bool vsync = vsyncOverride >= 0 ? vsyncOverride != 0 : settings.vsync;
        const HRESULT hr = g_swapChain->Present(vsync ? 1 : 0, (!vsync && g_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            static bool announced = false;
            if (!announced) { announced = true; printf("render: the device was lost (0x%08lX); nothing more can be drawn\n", hr); }
            g_failed = true;
        }
        g_lastPresent.store(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
        if (getenv("COD3_D3DDEBUG") != nullptr) DrainDebugMessages();
    }

    // The anti aliasing asked for: 0 none, 1 FXAA, 2 MSAA 2x, 3 MSAA 4x,
    // 4 MSAA 8x, 5 MSAA 4x with FXAA over it. COD3_AA=0|fxaa|msaa2|msaa4|
    // msaa8|msaa4fxaa over the settings.
    int AntiAliasing(const Settings::Values& settings)
    {
        static const int override = []() {
            const char* t = getenv("COD3_AA");
            if (t == nullptr) return -1;
            if (strcmp(t, "fxaa") == 0) return 1;
            if (strcmp(t, "msaa2") == 0) return 2;
            if (strcmp(t, "msaa4") == 0) return 3;
            if (strcmp(t, "msaa8") == 0) return 4;
            if (strcmp(t, "msaa4fxaa") == 0) return 5;
            return 0;
        }();
        return override >= 0 ? override : settings.antialiasing;
    }

    // What the settings ask of the renderer, applied at the frame's edge.
    void ApplySettings(const Settings::Values& settings)
    {
        const int antialiasing = AntiAliasing(settings);
        RenderResources::RequestMultisample(antialiasing == 2 ? 2 : antialiasing == 3 || antialiasing == 5 ? 4 : antialiasing == 4 ? 8 : 1);
        // The render scale: the chosen resolution's height over the title's
        // 624 rows (the desktop's height by default). COD3_SCALE=N pins it,
        // fractions allowed.
        static const float pinned = []() { const char* t = getenv("COD3_SCALE"); return t ? float(atof(t)) : 0.0f; }();
        float scale;
        if (pinned > 0.0f) scale = pinned;
        else
        {
            int width, height;
            Settings::Resolution(settings, width, height);
            scale = float(height) / 624.0f;
            if (scale < 0.5f) scale = 0.5f;
        }
        RenderResources::RequestScale(scale);

        // The texture filtering, unless the environment said.
        static const bool filterPinned = getenv("COD3_TEXTURE_FILTER") != nullptr || getenv("COD3_ANISO") != nullptr;
        if (!filterPinned)
        {
            const RenderPipeline::Filter filter = settings.textureFilter == 0 ? RenderPipeline::Filter::Native
                : settings.textureFilter == 1 ? RenderPipeline::Filter::Bilinear
                : settings.textureFilter == 2 ? RenderPipeline::Filter::Trilinear : RenderPipeline::Filter::Anisotropic;
            if (filter != RenderPipeline::Filtering() || uint32_t(settings.anisotropy) != RenderPipeline::Anisotropy())
            {
                RenderPipeline::SetFiltering(filter, uint32_t(settings.anisotropy));
                // The samplers were dropped: nothing bound is valid.
                memset(g_bound.samplers, 0, sizeof(g_bound.samplers));
            }
        }
    }
}

bool Render::Enabled()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return Start();
}

void Render::Draw(uint32_t initiator, uint32_t indexBase, uint32_t indexWord)
{
    (void)indexWord;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    Timed timed(RenderStats::Frame().drawNanoseconds);
    DrawLocked(initiator, indexBase);
}

void Render::Resolve()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    Timed timed(RenderStats::Frame().resolveNanoseconds);
    ResolveLocked();
}

void Render::ShaderLoaded(bool pixel, uint32_t guestAddress, uint32_t sizeDwords)
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    RenderShaders::Loaded(pixel, Guest::Base + guestAddress, sizeDwords);
}

uint64_t Render::CurrentProgramHash(bool pixel)
{
    return RenderShaders::HashOf(RenderShaders::Current(pixel));
}

void Render::Swap(uint32_t frontBufferPhysical, uint32_t width, uint32_t height)
{
    (void)width; (void)height;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!Start()) return;
    Overlay::NoteFrame();
    g_frontBuffer = frontBufferPhysical & 0x1FFFFFFFu;
    if (FrameLogged()) printf("frame: swap %llu of %08X\n", (unsigned long long)g_swaps.load(std::memory_order_relaxed), frontBufferPhysical);

    // The frame's end: onto the window, the counters closed, the next
    // frame's settings taken.
    const Settings::Values settings = Settings::Get();
    PresentLocked(settings);
    RenderStats::EndFrame();
    FrameSwapped();
    g_swaps.fetch_add(1, std::memory_order_relaxed);
    g_frame++;
    ApplySettings(settings);
    if (RenderResources::BeginFrame(g_frame)) ForgetBindings();   // the targets were made again
    RenderStats::BeginFrame();
}

void Render::SetWindow(void* hwnd, int clientWidth, int clientHeight)
{
    g_window.store(static_cast<HWND>(hwnd));
    g_clientWidth.store(clientWidth);
    g_clientHeight.store(clientHeight);
}

void Render::PresentIdle()
{
    // Only when the title has not swapped for a while: a resize, or the
    // menu, while it is loading or paused.
    const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now - g_lastPresent.load() < 250) return;
    std::unique_lock<std::recursive_mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    if (!g_started || g_failed) return;
    PresentLocked(Settings::Get());
}

void Render::Report()
{
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_started || g_failed) return;
    const RenderStats::Snapshot s = RenderStats::Current();
    const RenderResources::Statistics r = RenderResources::Stats();
    const RenderPipeline::Statistics p = RenderPipeline::Stats();
    printf("render: %.1f fps, %.2f ms a frame (%.2f ms cpu%s%.2f ms gpu), %.0f draws, %.0f k triangles, %.0f resolves, %.0f skipped a frame; "
           "switches a frame: %.0f programs, %.0f textures, %.0f states, %.0f targets\n",
        s.fps, s.frameMilliseconds, s.cpuMilliseconds, s.gpuProfiled ? ", " : ", no ", s.gpuMilliseconds,
        s.draws, s.triangles / 1000.0f, s.resolves, s.skipped, s.shaderSwitches, s.textureSwitches, s.pipelineSwitches, s.targetSwitches);
    printf("        %u textures, %u vertex buffers, %u colour and %u depth targets, %u resolved surfaces: %.1f MB; %u released; "
           "%llu texture uploads (%.1f MB), %llu buffer uploads (%.1f MB), %.1f MB of indices, %.1f MB of constants streamed\n",
        r.textures, r.buffers, r.colorTargets, r.depthTargets, r.resolved, r.resourceBytes / 1048576.0, r.released,
        (unsigned long long)r.textureUploads, r.textureBytes / 1048576.0, (unsigned long long)r.bufferUploads, r.bufferBytes / 1048576.0,
        r.indexBytes / 1048576.0, r.constantBytes / 1048576.0);
    printf("        %u blend, %u depth, %u rasteriser states, %u samplers; scale %.3f\n", p.blendStates, p.depthStates, p.rasterizerStates, p.samplers, RenderResources::Scale());
    RenderShaders::Report();
    bool any = false;
    for (int i = 0; i < SkipCount; i++) if (g_skips[i] != 0) any = true;
    if (any)
    {
        printf("        skipped:");
        for (int i = 0; i < SkipCount; i++) if (g_skips[i] != 0) printf(" %s x%llu;", SkipNames[i], (unsigned long long)g_skips[i]);
        printf("\n");
    }
    fflush(stdout);
}
