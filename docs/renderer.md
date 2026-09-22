# The renderer

The recompiled game talks to a Xenos GPU: its graphics driver writes PM4
packets into a ring, the packets write a register file, and a draw is a
packet issued against whatever the registers say. None of that can change;
it is the title's own code. What sits under it can, and since the renderer
rewrite of September 2026 it is a Direct3D 11 renderer built for a PC, not
a model of the console's GPU. This page is the audit that led to it, the
design, and what is known about how it performs.

## The audit: what was emulation

The old backend (`d3d11_backend.cpp`, `edram.cpp`, `raster.cpp`,
`shaders.cpp`) drew the right picture, and most of its cost was in things
that existed to imitate the console rather than to draw:

- **A software rasteriser with a shader interpreter** (`raster.cpp`, 2300
  lines, AVX) and a **ten megabyte EDRAM model** with the console's tile
  addressing (`edram.cpp`), the fallback path and the debugging path. Gone.
- **Programs looked up by their microcode**, a `std::map` keyed by the
  whole vector of words, compared on every draw after copying the words
  out of a mutex-guarded capture: 1.8 µs of the 8.4 µs a draw cost.
- **Vertex buffers byte swapped on the CPU** on upload (three passes over
  the data), textures untiled per draw check, index buffers copied word by
  word through the guest's endian helpers and mapped with `DISCARD` for
  every draw.
- **The whole 4 KB float constant file** of both stages uploaded whenever
  any constant had been written, which is before nearly every draw: 20 MB
  a frame through the ring.
- **Every state set on every draw**: blend, depth, rasteriser, samplers,
  32 shader resource slots cleared twice a draw, targets rebound, all
  looked up in `std::map`s. A `Skip()` that took a mutex and a
  `std::map<std::string>`.
- **Presenting on a 33 ms sleep loop** on the window thread, independent
  of the title's frames, with no vertical sync and the picture only ever
  at whole multiples of 1040x624.
- No mip levels on any texture, so no trilinear or anisotropic filtering
  could do anything.

What had to stay, because the title's output depends on it: the register
layout and the meaning of its fields (the translation layer reads them),
the shader translation from microcode (the programs are the title's), the
rectangle list primitive, the EDRAM tile base as the *name* of a render
target, the resolve as the copy that ends a frame, and the fetch
constants' swizzles and sign modes.

## The layers

```
game logic (recompiled)  ->  PM4 packets  ->  register file        gpu.cpp
   Render::Draw / Resolve / Swap / ShaderLoaded                    render.h
   RenderState::Snapshot   the registers of a draw, read once      render_state.*
   handles                 pipeline objects, programs, resources   render_pipeline.*, render_shaders.*, render_resources.*
   the executor            compares handles with what is bound,    render_d3d11.cpp
                           sets what changed, one draw call
   Direct3D 11             one immediate context, one thread
```

**render_state**: `RenderState::Read` copies the thirty registers a draw
depends on into a plain struct (relaxed loads; the command thread is the
thread that writes them). Nothing after reads the register file except
the constant files and fetch constants, which are read by index.

**render_pipeline**: blend, depth stencil, rasteriser and sampler states
by integer handle. The key is the register words that define the state
(five words for blend, two for depth, one for the rasteriser, one for a
sampler), hashed into a small open addressing table (`render_table.h`).
A hit is a few loads and a `memcmp`; a miss creates the object once. The
sampler cache is where the PC's filtering is decided: `COD3_TEXTURE_FILTER`
and `COD3_ANISO`, or the settings menu, lay bilinear, trilinear or
anisotropic filtering over the title's own fetch constants.

**render_shaders**: a program is known by the FNV hash of its microcode
(the same hash the captured files are named by). The stream's `IM_LOAD`
hashes the words in place and looks the hash up; a draw reads the stage's
current handle. A new program is translated and compiled on a worker
thread (2 to 6 of them, below normal priority) while the frames go on
without the draws that need it. The disk cache
(`%LOCALAPPDATA%\CoD3Recomp\shaders\<hash>.<vs|ps>.<version>.bin`) carries
the translation's metadata (fetches, targets, the constant map) beside the
bytecode, keyed by the microcode hash and `XenosHlsl::Version()`, so a hit
needs no translation at all. At start, every program captured under the
working directory's `shaders` folder by earlier runs that is not in the
cache is compiled in the background ("precompile"); the stream's own
programs go to the front of the queue.

**The translation** (`xenos_hlsl.cpp`) is unchanged in what it computes and
changed in two things. The vertex fetches turn the words round in the
shader (`bswap`), so a vertex buffer is bound as it lies in the console's
memory and an upload is one copy. And the float constants a program reads
are packed: `c[i]` in the HLSL is the file's `c[constantMap[i]]`, so the
backend gathers only those (a program with `a0` relative addressing reads
the whole file). Programs whose control flow nests as blocks are emitted
as `if`s; only loops and crossing jumps fall back to the loop over a
switch, which the host compiler handles.

**render_resources**: textures by (address, format, dimension, size),
uploaded once with their mip levels (from the fetch constant's mip
address, each level's pitch and height rounded up to whole 32 block tiles,
down to 32 texels; the packed tail is left to the sampler's clamp), cube
maps as six faces; a change under a texture the title rewrites (the HUD)
is caught by a 64 sample fingerprint taken once a frame, not once a draw.
Vertex buffers by address, raw byte address buffers, `UpdateSubresource`
straight from guest memory. Indices through a 16 MB ring mapped
`NO_OVERWRITE` (16 bit indices stay 16 bit; the strip cut index is the
host's own when the title's is all ones). Constants through a 16 MB ring
bound at offsets (`VSSetConstantBuffers1`), one map for a draw's blocks.
Render targets keyed by base tile, pitch and format at the current scale;
resolved surfaces by the physical address the title samples them by.
What is unused for 1800 frames is released.

**render_d3d11**: the executor. Targets are set before the textures so a
surface about to be drawn into is unbound from the slots holding it, and
only those. Each texture, sampler, vertex buffer, state object, program
and constant offset is compared with what the context has and set only
when it differs. Indices are converted into a scratch that never shrinks.
The present happens here too, at the title's swap: the resolved front
buffer onto the window, 16:9 centred, through FXAA when asked, the
overlay on top, and `Present(1)` or `Present(0, ALLOW_TEARING)` as the
settings say. With vertical sync the present blocks the command thread,
which is exactly what throttles the title to the display.

**render_stats**: counters per frame folded into one second averages: fps,
frame time, CPU time on the command thread, GPU time from timestamp
queries (`COD3_GPU_PROFILE=1`), draws, triangles, resolves, and the
program, texture, pipeline and target switches, the uploads and the bytes
streamed. `COD3_RENDER_STATS=1` prints a line a second; the settings menu
shows the same panel over the picture. `COD3_RENDER_DEBUG=1` logs one
frame's draws (the same as `COD3_D3DFRAME=auto`).

## Resolution and picture

The title draws 1040x624. The render targets are that times a scale that
is a fraction: the window's height over 624 by default (a 1080p window
draws 1800x1080), or a whole multiple from the menu (`COD3_SCALE=N` pins
it). The viewport is folded into the vertex program in clip space, so any
target size works without the programs knowing; scissors and resolves are
scaled by the target's real size over the title's. The picture is then
put on the window at 16:9 with black either side, as the console's scaler
did. FXAA 3.11 (quality path) runs over the presented picture when the
menu says; MSAA is not offered, since the title's resolves and its depth
sampled as shadow maps would need resolving in the middle of the frame.
Colour stays as the title computes it, its gamma in its own programs,
with no sRGB or HDR path added: an SDR picture identical to the console's.

## Settings and knobs

In the F11 menu, kept in `CoD3Recomp.ini`: window or borderless full
screen (Alt+Enter too), internal resolution, texture filtering and
anisotropy, anti aliasing, vertical sync, the fps and stats panels. From
the environment, over the menu: `COD3_SCALE`, `COD3_TEXTURE_FILTER=native|
bilinear|trilinear|anisotropic`, `COD3_ANISO=1..16`, `COD3_VSYNC=0|1`,
`COD3_AA=0|fxaa`, `COD3_FULLSCREEN=0|1`, `COD3_NOMIPS=1`,
`COD3_NOSHADERCACHE=1`, `COD3_NOPRECOMPILE=1`. Diagnostics: `COD3_D3DDEBUG`
(the debug layer), `COD3_D3DFRAME=N|auto|loading` with `COD3_D3DDRAWDUMP`,
`COD3_FRAMEDUMP`, `COD3_D3DSKIPVS`, `COD3_D3DFLAT`, `COD3_DUMPHLSL`,
`COD3_D3DTEXDUMP`.

## Performance

The forest level, 1280x720 window, the title's frame rate is its own cap
of 60 (its vertical blank). Measured with `COD3_RENDER_STATS=1` on a
GeForce RTX 4070 Ti; the old numbers from the same level and machine
with the old backend's ten second report.

|                              | old backend        | new renderer            |
| ---------------------------- | ------------------ | ----------------------- |
| frames a second in the level | 30                 | 60 (the title's cap)    |
| draws a frame                | ~2700              | ~2550                   |
| CPU time drawing, a frame    | ~22.7 ms           | 3.7 ms                  |
| CPU time a draw              | 8.4 µs             | 1.45 µs                 |
| of which: finding programs   | 1.8 µs             | ~0 (a hash compare)     |
| state objects set a frame    | every draw, all    | 150 pipeline, 20 target |
| program switches a frame     | every draw         | ~255                    |
| texture binds a frame        | every draw, 32 x 2 cleared | ~1245           |
| constants streamed a frame   | 20 MB              | ~5 MB (packed)          |
| index data a frame           | copied twice       | ~6 MB, one copy         |
| vertex buffer uploads        | 39 MB/s, swapped on the CPU | 1 to 2 a frame, one copy |
| texture uploads in a run     | 360, no mips       | ~5 a minute steady, with mips |
| program compiles at start    | at first use, draws missing | precompiled in the background from the capture; the disk cache carries metadata |
| GPU time (timestamp span)    | not measured       | ~14 ms of a 16.7 ms frame; the GPU idles inside it, so this is a bound, not busy time |

At 4x (4160x2496 targets) the level also holds 60 with 5 ms of CPU a
frame: the CPU cost is per draw, not per pixel.

## Visual differences

`scripts/compare_frames.py` matches each frame dump of one run to the
nearest of another and prints the mean pixel difference. Between the old
backend and the new renderer at the same scale the level's frames differ
by 6/255 on average, the difference being the runs' timing (the title's
clock, the HUD's fades) rather than the rendering; between the new
renderer with and without mip levels, 1.75/255. What is deliberately
different: mip levels and anisotropic filtering (the console had both;
the old backend had neither), FXAA when on, and the picture at the
window's height rather than a whole multiple. Cube maps are uploaded as
six faces now, where the old backend bound a flat texture and the
reflection read black. Volume textures are still white.

## What is left

- Volume (3D) textures are not uploaded (their tiling is different).
- Render targets are allocated at 1440 rows times the scale for every
  pitch, more than the title draws into; at 4x that is a lot of memory.
- No MSAA; no sRGB or HDR output path.
- The vertex fetch is by raw loads; an input layout would let the GPU's
  vertex fetch do the work. The programs' loops still go through the
  switch when they cannot be structured.
- The fingerprint that catches a rewritten texture or buffer samples 64
  points; a one texel change can be missed until the next.
