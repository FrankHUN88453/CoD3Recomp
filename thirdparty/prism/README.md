# Prism tone mapper

`prismTM.hlsl` is the Prism tone mapper by Musa Haji and OopyDoopy / KickFister / Jon,
MIT licensed (`LICENSE`, and the notice at the top of the file). It is one of the tone
mappers offered in the HDR tab (F11).

It is used as supplied, with one change: its `#include "./shared.h"` (the mod it came
from) is taken out. Prism calls `reinhardPiecewiseSmooth(color, peak, anchor)` from that
mod's common file, which was not supplied; `CoD3Host/hdr/cod3_hdr.hlsl` defines it before
including this file: the colour unchanged below the anchor (0.18), and above it a
Reinhard shoulder that leaves the anchor with a slope of one and reaches the peak at
infinity, with no division by zero for any input.
