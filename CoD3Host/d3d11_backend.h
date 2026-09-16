#pragma once

// The console's GPU on the host's, through Direct3D 11.
//
// The software rasteriser in raster.cpp draws a level frame in seconds.
// This draws it with the host GPU: the console's shaders are translated to
// HLSL (xenos_hlsl.cpp), its EDRAM render targets are textures, its vertex
// buffers and textures are uploaded out of guest memory as they are used,
// and a resolve copies a render target into a texture kept under the guest
// address the title will sample it by.
//
// The command thread makes every draw; the window thread presents. Both
// go through one lock, since a Direct3D 11 context is for one thread at a
// time.

#include <cstdint>

namespace D3D11Backend
{
    // Whether the host GPU is used at all: COD3_GPU=soft keeps the software
    // rasteriser. The device is made on first use.
    bool Enabled();

    // A draw from the command stream, as the rasteriser takes it.
    void Draw(uint32_t initiator, uint32_t indexBase, uint32_t indexWord);

    // A resolve: the render target the registers describe into the texture
    // at the destination they name, with the clears they ask for.
    void Resolve();

    // VdSwap: the front buffer the title finished, by physical address.
    void Swap(uint32_t frontBufferPhysical, uint32_t width, uint32_t height);

    // The window thread: the last swapped frame onto the window. Returns
    // false when there is nothing to show yet.
    bool Present(void* hwnd, int clientWidth, int clientHeight);

    void Report();
}
