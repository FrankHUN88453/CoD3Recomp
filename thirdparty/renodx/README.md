# RenoDX shaders

The HDR mode (F11, the HDR tab) is built on [RenoDX](https://github.com/clshortfuse/renodx)
by Carlos Lopez Jr. and its contributors. RenoDX's shader library does the tone mapping,
the colour grading sliders and the swap chain output; the port only decides where in
Call of Duty 3's frame they run.

`hlsl/` is RenoDX's `src/shaders` folder, the files `draw.hlsl` includes, copied
unchanged from commit `6d563066956190c8b9a2c7a907398bfac3629a3c` (2026-10-16). The
folder is called `hlsl` here only because the repository ignores every folder named
`shaders` (the game's captured microcode goes in one).

What comes from it:

- the tone mappers: RenoDRT, ACES, and PsychoV (tests 17, 22 and 30, by the RenoDX
  contributors who wrote them)
- the grading: exposure, highlights, shadows, contrast, saturation, highlight
  saturation, blowout, flare and hue correction, as RenoDX mods offer them
- `ToneMapPass`, `RenderIntermediatePass`, `SwapChainPass` and `UpscaleVideoPass`:
  the game's picture to the UI's brightness and on to scRGB

The files are embedded in the executable at build time (`CoD3Host/CMakeLists.txt`) and
compiled at run time with `d3dcompiler_47.dll`, like the rest of the renderer's programs.

RenoDX is MIT licensed; its license is in `LICENSE` beside this file and applies to
everything under `hlsl/`.
