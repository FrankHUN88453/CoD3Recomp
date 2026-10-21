// Call of Duty 3's HDR output, built on RenoDX.
//
// RenoDX (https://github.com/clshortfuse/renodx, MIT, Carlos Lopez Jr. and
// contributors; thirdparty/renodx) does the tone mapping, the grading
// sliders and the swap chain's encoding; Prism (MIT, Musa Haji and
// OopyDoopy / KickFister / Jon; thirdparty/prism) is one more tone mapper.
// This file is the port's part, written the way a RenoDX mod's shaders are:
// the RENODX_* macros name the settings, then draw.hlsl's passes run where
// this game needs them.
//
// The game has no tone mapper of its own. It draws an SDR picture, encoded
// for a display, into 8 bit targets that clip it at one; in the HDR mode
// those targets are 16 bit float (render_resources.cpp), so the sky, the
// fire and the muzzle flashes keep what they had beyond one. Three passes:
//
//   scene    right before the bloom is added to the frame, the HUD's turn
//            just after (render_commands.cpp finds that draw): the frame
//            tone mapped to the display's peak, and encoded as RenoDX's
//            intermediate, where one is the UI's white, so the HUD blends
//            over it as it always did.
//   bloom    the game's bloom composite, added over the tone mapped frame.
//   present  the finished frame to the swap chain, scRGB: RenoDX's swap
//            chain pass. A frame without the scene pass (the menus, a
//            loading screen, a film) is all UI, or a film at the game's
//            brightness, optionally upgraded to HDR.
//   ui       the settings menu and the counters, drawn by Dear ImGui into a
//            layer of their own, laid over at the UI's brightness.
//
// Compiled by render_d3d11.cpp with one of HDR_SCENE, HDR_BLOOM, HDR_PRESENT
// or HDR_UI defined, HDR_TONEMAPPER (the scene's tone mapper) and HDR_FXAA (the
// present's), each variant in the background when it is first needed.

#ifndef HDR_TONEMAPPER
#define HDR_TONEMAPPER 3
#endif
#ifndef HDR_SAMPLES
#define HDR_SAMPLES 1
#endif

// The settings (render_d3d11.cpp's HdrConstants), already in RenoDX's units.
cbuffer HdrSettings : register(b1) {
  float hdrPeakNits;             // the display's peak
  float hdrGameNits;             // the game's white
  float hdrUiNits;               // the HUD's and the menus' white
  float hdrGammaCorrection;      // 0 sRGB, 1 gamma 2.2, 2 gamma 2.4
  float hdrExposure;             // 1 the game's own
  float hdrHighlights;           // 1 each, 0 .. 2
  float hdrShadows;
  float hdrContrast;
  float hdrSaturation;
  float hdrHighlightSaturation;  // RenoDRT
  float hdrBlowout;              // RenoDRT, 0 .. 1
  float hdrFlare;                // RenoDRT, 0 .. 2
  float hdrHueCorrection;        // 0 .. 1
  float hdrConeResponse;         // PsychoV, 1 its own
  float hdrTvRamp;               // how much of the title's display table, 0 .. 1 (1 as the SDR picture has it)
  float hdrFilmUpgrade;          // 1: films expanded to HDR (RenoDX's video upscale)
  float hdrGraded;               // present: the scene pass ran on this frame
  float hdrFilm;                 // present: a film is playing
  float hdrPad0;
  float hdrPad1;
};

#define RENODX_PEAK_WHITE_NITS               hdrPeakNits
#define RENODX_DIFFUSE_WHITE_NITS            hdrGameNits
#define RENODX_GRAPHICS_WHITE_NITS           hdrUiNits
#define RENODX_GAMMA_CORRECTION              hdrGammaCorrection
#define RENODX_TONE_MAP_EXPOSURE             hdrExposure
#define RENODX_TONE_MAP_HIGHLIGHTS           hdrHighlights
#define RENODX_TONE_MAP_SHADOWS              hdrShadows
#define RENODX_TONE_MAP_CONTRAST             hdrContrast
#define RENODX_TONE_MAP_SATURATION           hdrSaturation
#define RENODX_TONE_MAP_HIGHLIGHT_SATURATION hdrHighlightSaturation
#define RENODX_TONE_MAP_BLOWOUT              hdrBlowout
#define RENODX_TONE_MAP_FLARE                hdrFlare
#define RENODX_TONE_MAP_HUE_CORRECTION       hdrHueCorrection
// The tone mappers RenoDX's ToneMapPass runs: Vanilla 0 (the game's
// picture, clipped at one as the SDR one is), None 1, ACES 2, RenoDRT 3.
// PsychoV and Prism are called directly below.
#if HDR_TONEMAPPER <= 3
#define RENODX_TONE_MAP_TYPE (float(HDR_TONEMAPPER))
#else
#define RENODX_TONE_MAP_TYPE 1.f
#endif

#include "renodx/draw.hlsl"

// Prism's shoulder (prism/README.md): unchanged up to the anchor, then a
// Reinhard curve leaving the anchor with a slope of one and reaching the
// peak at infinity. No division can be by nought, whatever comes in.
float reinhardPiecewiseSmooth(float x, float peak, float anchor) {
  float headroom = max(peak - anchor, 1e-6f);
  float over = max(x - anchor, 0.f);
  float shoulder = anchor + over / (1.f + over / headroom);
  return x > anchor ? shoulder : x;
}
float3 reinhardPiecewiseSmooth(float3 x, float peak, float anchor) {
  return float3(
      reinhardPiecewiseSmooth(x.r, peak, anchor),
      reinhardPiecewiseSmooth(x.g, peak, anchor),
      reinhardPiecewiseSmooth(x.b, peak, anchor));
}
#include "prism/prismTM.hlsl"

SamplerState smp : register(s0);
// The display's colour table (Gpu::DisplayGamma), 256 entries: the title's
// ramp for a television, which the SDR present puts every pixel through.
Texture1D<float4> gammaTable : register(t1);

// The table over the frame's colour. Past one (the 16 bit targets) it
// carries on along the table's last step, so it stays continuous there.
float3 TvRamp(float3 c) {
  float3 at = saturate(c) * (255.f / 256.f) + (0.5f / 256.f);
  float3 inside = float3(
      gammaTable.SampleLevel(smp, at.r, 0).r,
      gammaTable.SampleLevel(smp, at.g, 0).g,
      gammaTable.SampleLevel(smp, at.b, 0).b);
  float3 top = gammaTable.Load(int2(255, 0)).rgb;
  float3 slope = (top - gammaTable.Load(int2(254, 0)).rgb) * 255.f;
  return c > 1.f ? top + (c - 1.f) * slope : inside;
}

// The table held to the HDR settings' brightness. On its own it dims the
// whole picture (its lower half goes out darker, nothing comes out
// brighter), so the game's mid grey no longer lands where Game brightness
// puts it, and the picture looks darker than the paper white says. Here the
// table's light is raised by the one power that brings 18% grey back to
// 18% (per channel, from the table itself): white stays at the game's
// white, mid grey at 18% of it, and what is left of the table is its shape,
// the deeper shadows (its lower half falls further than a power does).
// Past one, the 16 bit targets' highlights carry on along the curve's slope
// at one.
float3 TvRampAnchored(float3 game) {
  static const float grey = 0.18f;
  static const float greySignal = 0.461356f;  // sRGB's encoding of 18%
  float3 ramped = renodx::color::srgb::DecodeSafe(TvRamp(game));
  float3 greyRamped = renodx::color::srgb::DecodeSafe(TvRamp(greySignal.xxx));
  greyRamped = clamp(greyRamped, 1e-4f, 0.99f);
  float3 power = clamp(log(grey) / log(greyRamped), 0.25f, 4.f);
  float3 below = pow(max(ramped, 0.f), power);
  float3 above = 1.f + (ramped - 1.f) * power;
  return renodx::color::srgb::EncodeSafe(ramped > 1.f ? above : below);
}

float3 Signal(float3 game) {
  float3 signal = game;
  [branch]
  if (hdrTvRamp > 0.f) signal = lerp(game, TvRampAnchored(game), hdrTvRamp);
  return signal;
}

// The display's peak over the game's white, for the tone mappers RenoDX's
// ToneMapPass does not run, corrected for the gamma as RenoDRT's is.
float PeakRatio() {
  float peak = hdrPeakNits / hdrGameNits;
  [branch]
  if (hdrGammaCorrection != 0.f) {
    peak = renodx::color::correct::Gamma(peak, true, hdrGammaCorrection == 1.f ? 2.2f : 2.4f);
  }
  return peak;
}

// The scene: the game's colour (encoded for a display, beyond one where
// the targets kept it) to RenoDX's intermediate.
float3 GradeScene(float3 game) {
  float3 signal = Signal(game);
  float3 tonemapped;
#if HDR_TONEMAPPER == 0
  tonemapped = renodx::draw::ToneMapPass(renodx::color::srgb::DecodeSafe(saturate(signal)));
#elif HDR_TONEMAPPER <= 3
  tonemapped = renodx::draw::ToneMapPass(renodx::color::srgb::DecodeSafe(signal));
#elif HDR_TONEMAPPER == 4
  tonemapped = renodx::tonemap::psychov::psychotm_test17(
      renodx::color::srgb::DecodeSafe(signal), PeakRatio(),
      hdrExposure, hdrHighlights, hdrShadows, hdrContrast, hdrSaturation,
      1.f, 100.f, 1.f, 1.f, 0, hdrConeResponse);
  tonemapped = renodx::color::bt709::clamp::AP1(tonemapped);
#elif HDR_TONEMAPPER == 5
  tonemapped = renodx::tonemap::psychov::psychotm_test22(
      renodx::color::srgb::DecodeSafe(signal), PeakRatio(),
      hdrExposure, hdrHighlights, hdrShadows, hdrContrast, hdrSaturation,
      1.f, 100.f, 1.f, 1.f, 0, hdrConeResponse,
      float3(0.18f, 0.18f, 0.18f), float3(0.18f, 0.18f, 0.18f), 1.f, 1, 1.f, 1.f);
  tonemapped = renodx::color::bt709::clamp::AP1(tonemapped);
#elif HDR_TONEMAPPER == 6
  tonemapped = renodx::tonemap::psychov::psychotm_test30(
      renodx::color::srgb::DecodeSafe(signal), PeakRatio(),
      hdrExposure, hdrHighlights, hdrShadows, hdrContrast, hdrSaturation,
      1.f, 100.f, 1.f, 1.f, 0, hdrConeResponse,
      float3(0.18f, 0.18f, 0.18f), float3(0.18f, 0.18f, 0.18f), 1.f, 1, 1.f, 1.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709);
  tonemapped = renodx::color::bt709::clamp::AP1(tonemapped);
#else
  float3 graded = renodx::color::grade::UserColorGrading(
      renodx::color::srgb::DecodeSafe(signal),
      hdrExposure, hdrHighlights, hdrShadows, hdrContrast, hdrSaturation);
  tonemapped = prism(graded, PeakRatio());
#endif
  return renodx::draw::RenderIntermediatePass(tonemapped);
}

#if defined(HDR_SCENE)
// A copy of the frame's target as the bloom left it (resolved, with MSAA:
// render_d3d11.cpp compiles this with HDR_SAMPLES 1); the pass writes the
// target itself. HDR_SAMPLES above one reads and writes each sample.
#if HDR_SAMPLES > 1
Texture2DMS<float4> sceneCopy : register(t0);
float4 scene(float4 position : SV_Position, uint sampleIndex : SV_SampleIndex) : SV_Target {
  float4 c = sceneCopy.Load(int2(position.xy), sampleIndex);
  return float4(GradeScene(c.rgb), c.a);
}
#else
Texture2D<float4> sceneCopy : register(t0);
float4 scene(float4 position : SV_Position) : SV_Target {
  float4 c = sceneCopy.Load(int3(position.xy, 0));
  return float4(GradeScene(c.rgb), c.a);
}
#endif
#endif

#if defined(HDR_PRESENT)
Texture2D<float4> source : register(t0);
cbuffer Quad : register(b0) { float4 uvRect; float4 texel; };
#if HDR_FXAA
#include "cod3_fxaa.hlsl"
#endif

float3 PresentColor(float3 frame) {
  float3 intermediate = frame;
  [branch]
  if (hdrGraded <= 0.5f) {
    float3 signal = Signal(frame);
    [branch]
    if (hdrFilm > 0.5f) {
      float3 video;
      [branch]
      if (hdrFilmUpgrade > 0.5f) {
        // As RenoDX's mods upgrade their videos (endfield's AutoHDRVideo).
        video = renodx::draw::UpscaleVideoPass(saturate(signal));
        video = renodx::color::srgb::DecodeSafe(video);
      } else {
        video = renodx::color::srgb::DecodeSafe(signal);
      }
      intermediate = max(0, renodx::draw::RenderIntermediatePass(video));
    } else {
      // The menus, the loading screens: the UI's white.
      intermediate = signal;
    }
  }
  return renodx::draw::SwapChainPass(intermediate);
}

float4 present(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
#if HDR_FXAA
  float3 frame = fxaaColor(uv);
#else
  float3 frame = source.Sample(smp, uv).rgb;
#endif
  return float4(PresentColor(frame), 1.f);
}
#endif

#if defined(HDR_BLOOM)
// The bloom's composite in the HDR mode, in the game's program's place
// (pixel program fe6578445de135ea): the blurred sky (the game's bloom is
// the sky's, the chain masks the rest out by the depth) times twice the
// game's strength (c1.x), added by the blend (the constant colour, which
// the bloom slider scales) onto the frame the scene pass has already tone
// mapped. Added after the tone map, as the user's RenoDX World at War mod
// does it: the halos keep the game's shape and colour, and where they add
// up past white over the sky they go on into the highlights instead of
// clipping there, which is what makes them brighter than the SDR ones.
//
// The intermediate's one is the UI's white, the scene's is the game's: the
// glow is scaled by the same step, so it stands to the picture as it does
// in SDR whatever the two are set to. Past the game's white it is rolled
// off towards twice that, so a raised slider cannot swamp the frame.
cbuffer FloatConstants : register(b0) { float4 c[256]; };
Texture2D<float4> bloomTexture : register(t0);
float4 bloom(float4 position : SV_Position, float4 o0 : TEXCOORD0) : SV_Target {
  float4 t = bloomTexture.Sample(smp, o0.xy);
  float strength = 2.f * c[1].x;
  // As the game's: red the strength alone, the rest with c2.x added.
  float4 glow = t * strength + float4(0.f, c[2].x, c[2].x, c[2].x);
  glow = glow == glow ? glow : 0;   // not a number: none
  float3 light = max(0, glow.rgb);
  float white = max(renodx::draw::RenderIntermediatePass(float3(1.f, 1.f, 1.f)).g, 1e-3f);
  light *= white;
  float peak = max(light.r, max(light.g, light.b));
  float over = max(peak - white, 0.f);
  float mapped = white + over / (1.f + over / white);
  light *= peak > white ? mapped / peak : 1.f;
  return float4(light, saturate(glow.a));
}
#endif

#if defined(HDR_UI)
// Dear ImGui's layer, the swap chain's size, drawn over a clear: its colour
// is multiplied by its alpha already. Blended over the frame with one and
// one less the alpha.
Texture2D<float4> uiLayer : register(t0);
float4 ui(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  float4 u = uiLayer.Load(int3(position.xy, 0));
  float4 output = 0;
  [branch]
  if (u.a > 0.f) output = float4(renodx::draw::SwapChainPass(u.rgb / u.a) * u.a, u.a);
  return output;
}
#endif
