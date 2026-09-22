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
   the PC render layer     Snapshot -> DrawCommand: handles from   render_commands.cpp
                           the caches, views, host pixels, the
                           constants and indices in their rings
   caches                  pipeline objects, programs, resources   render_pipeline.*, render_shaders.*, render_resources.*
   the executor            runs a DrawCommand: compares each       render_d3d11.cpp
                           handle and view with what is bound,
                           sets what changed, one draw call
   Direct3D 11             one immediate context, one thread
```

**render_state**: `RenderState::Read` copies the thirty registers a draw
depends on into a plain struct (relaxed loads; the command thread is the
thread that writes them). `ReadResolve` does the same for a resolve.
Nothing after reads the register file except the PC render layer, which
reads the constant files and the fetch constants by index.

**render_commands, the PC render layer**: where the console's terms end.
`PrepareDraw` turns a Snapshot into a `DrawCommand`: the programs and
the blend, depth and rasteriser states as integer handles into the
caches; the render targets, textures, samplers and vertex buffers as the
views the caches own; the viewport and scissor in host pixels; the
constants the programs read gathered into the constant ring and named
by offset; the indices turned round into the index ring. The EDRAM tile
a surface is named by becomes a render target handle here, a fetch
constant becomes a view and a sampler, a big endian index list becomes a
range of the ring. `PrepareResolve` does the same for the copy that
ends a pass. The executor below never sees a register; it runs commands.
The command is prepared and run at once, on the command thread: a
deferred queue would only add a copy, since Direct3D 11's immediate
context is single threaded anyway, and the profile shows no two
consecutive draws that could have been one (the title changes a
texture, a constant or a state between nearly every pair), so there is
nothing for a reordering pass to merge that the order-dependent draws
(blended, particles, the HUD) would allow.

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
and constant offset the command names is compared with what the context
has (the `Bound` struct, the CPU's shadow of the context) and set only
when it differs.
The present happens here too, at the title's swap: the resolved front
buffer onto the window, 16:9 centred, through FXAA when asked, the
overlay on top, and `Present(1)` or `Present(0, ALLOW_TEARING)` as the
settings say. With vertical sync the present blocks the command thread,
which is exactly what throttles the title to the display.

**render_stats**: counters per frame folded into one second averages: fps,
frame time, CPU time on the command thread, GPU time from timestamp
queries (`COD3_GPU_PROFILE=1`), draws, triangles, resolves, the
program, texture, pipeline and target switches, the uploads and the bytes
streamed, the programs compiled and read from the disk cache.
`COD3_RENDER_STATS=1` prints a line a second; the settings menu
shows the same panel over the picture. `COD3_RENDER_PROFILE=1` adds a
line with the draw path's sections timed by the cycle counter (state,
targets, pipeline, textures, buffers, constants, bind, indices, draw),
the constant blocks uploaded and the draws that could have joined the
one before. `COD3_RENDER_DEBUG=1` logs one frame's draws (the same as
`COD3_D3DFRAME=auto`).

**The translation's control flow**: a program's jumps that nest as
blocks are written as ifs, a loop whose end names its start as a for
over the loop constant's count, start and step. Of the 949 programs the
title has loaded across every level, six still get the loop over a
switch on the program counter: captured programs whose loop end names a
nop, which the title never draws with. Every drawn program is straight
HLSL.

## Resolution and picture

The title draws 1040x624. The render targets are that times a scale that
is a fraction: the window's height over 624 by default (a 1080p window
draws 1800x1080), or a whole multiple from the menu (`COD3_SCALE=N` pins
it). The viewport is folded into the vertex program in clip space, so any
target size works without the programs knowing; scissors and resolves are
scaled by the target's real size over the title's. The picture is then
put on the window at 16:9 with black either side, as the console's scaler
did. Anti aliasing is MSAA 2x, 4x or 8x on the render targets (the
default is 4x): the title's resolves copy a multisampled target through
a program that averages the samples of a colour target and takes the
first of a depth target, as the console's own resolve did, so the shadow
maps and the post processing's copies see a single sampled surface; and
FXAA 3.11 (quality path) over the frame at the present, alone or over
MSAA 4x. Colour stays as the title computes it, its gamma in its own
programs, with no sRGB or HDR path added: an SDR picture identical to the
console's.

## Settings and knobs

In the F11 menu, kept in `CoD3Recomp.ini`: window or borderless full
screen (Alt+Enter too), the resolution the frame is drawn at (the
desktop's by default), texture filtering and anisotropy, anti aliasing,
texture quality (Low and Medium leave the top two or one mip levels of
every texture out on upload), the field of view (the title's own
`cg_fov`, 65 to 100, in the config at start and on the command buffer
when it changes), vertical sync, the fps and stats panels. What the
title has no knob for is not offered: its shadow map is one resolution
(it already scales with the frame), its post processing is one chain,
and there is no `r_shadow` or `r_glow` in its console. From
the environment, over the menu: `COD3_SCALE`, `COD3_TEXTURE_FILTER=native|
bilinear|trilinear|anisotropic`, `COD3_ANISO=1..16`, `COD3_VSYNC=0|1`,
`COD3_AA=0|fxaa|msaa2|msaa4|msaa8|msaa4fxaa`, `COD3_TEXQUALITY=0|1|2`,
`COD3_FULLSCREEN=0|1`, `COD3_NOMIPS=1`,
`COD3_NOSHADERCACHE=1`, `COD3_NOPRECOMPILE=1`. For a scripted run,
`COD3_CMD="second:command;..."` puts console commands on the title's
buffer at those seconds and `COD3_STRINGS="prefix,..."` lists the
strings of the image that start so (the console variables the title
knows). Diagnostics: `COD3_D3DDEBUG`
(the debug layer), `COD3_D3DFRAME=N|auto|loading` with `COD3_D3DDRAWDUMP`,
`COD3_FRAMEDUMP`, `COD3_D3DSKIPVS`, `COD3_D3DFLAT`, `COD3_DUMPHLSL`,
`COD3_D3DTEXDUMP`. In the frame log a texture that reads white says why
(`WHITE: no texture in the fetch constant`, `format not uploaded`, `upload
failed`), and the first draw that wants a program that could not be built
says which and why; the captured programs that fail to translate are only
counted, since the title loads hundreds it never draws with.

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

The profile of the draw path (`COD3_RENDER_PROFILE=1`, the forest level,
2560 draws a frame), before and after the PC render layer was split from
the executor, in microseconds a draw:

|                        | before | after |
| ---------------------- | ------ | ----- |
| state (registers, programs) | 0.06 | 0.06 |
| targets and scissor    | 0.06   | 0.06  |
| pipeline handles       | 0.03   | 0.02  |
| textures and samplers  | 0.18   | 0.14  |
| vertex buffers         | 0.16   | 0.13  |
| constants into the ring | 0.44  | 0.29  |
| binding on the context | (in the above) | 0.18 |
| indices into the ring  | 0.39   | 0.22  |
| the draw call          | 0.01   | 0.02  |
| a draw                 | 1.35   | 1.13  |
| CPU a frame            | 3.9 ms | 3.2 ms |

The indices are turned round straight into the mapped ring, eight or
four at a time, instead of through a scratch and a copy; the constants
a program reads are copied in runs. The 4780 constant blocks a frame
are the title's: it writes a few constants before nearly every draw, so
the vertex program's block goes up nearly every draw, packed to what
the program reads. At 2560x1440 with MSAA 8x and 16x anisotropic
filtering the level holds 60 at 3.2 ms of CPU a frame.

## Visual differences

`scripts/compare_frames.py` matches each frame dump of one run to the
nearest of another and prints the mean pixel difference. Between the old
backend and the new renderer at the same scale the level's frames differ
by 6/255 on average, the difference being the runs' timing (the title's
clock, the HUD's fades) rather than the rendering; between the new
renderer with and without mip levels, 1.75/255. What is deliberately
different: mip levels and anisotropic filtering (the console had both;
the old backend had neither), MSAA and FXAA when on, and the picture at
the chosen resolution rather than a whole multiple. Cube maps are
uploaded as six faces now, where the old backend bound a flat texture
and the reflection read black; the sprites the title fetches "as a
volume" read their flat texture instead of white.

## What is left

- A fetch that says volume reads a flat texture: every one the title
  issues names a flat texture in its fetch constant, and no volume
  texture has been seen. If one turns up the log says so.
- Render targets start at 720 rows for the 1040 wide surfaces and the
  pitch for the rest, and grow when a draw's scissor or a resolve reaches
  further (the crossroads level's 1024 row shadow map): the surfaces of
  that pitch are made again taller, colour and depth together.
- No sRGB or HDR output path.
- The vertex fetch is by raw loads with the words turned round in the
  program; an input layout would let the GPU's vertex fetch do the work,
  but the buffers would have to be turned round on upload by the fetch
  constant's byte order, and the GPU's time is not in the vertex fetch.
- A draw is prepared and run at once; the DrawCommand is a value that a
  deferred queue could carry, but nothing today would gain from one.
- Settings the title has no knob for: shadow quality, post processing.
  A render scale beside the resolution was left out on purpose.
- The fingerprint that catches a rewritten texture or buffer samples 64
  points; a one texel change can be missed until the next.
