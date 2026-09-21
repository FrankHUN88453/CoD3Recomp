#include "render_state.h"
#include "render.h"
#include "gpu.h"

#include <cstring>

namespace
{
    inline uint32_t Reg(uint32_t index)
    {
        return Gpu::RegisterFile()[index].load(std::memory_order_relaxed);
    }

    inline float RegFloat(uint32_t index)
    {
        const uint32_t bits = Reg(index);
        float value;
        memcpy(&value, &bits, 4);
        return value;
    }
}

void RenderState::Read(uint32_t initiator, uint32_t indexBase, Snapshot& out)
{
    out.primitive = initiator & 0x3F;
    out.sourceSelect = (initiator >> 6) & 3;
    out.indexCount = initiator >> 16;
    out.wideIndices = ((initiator >> 11) & 1) != 0;
    out.indexBase = indexBase;

    out.pitch = Reg(SurfaceInfo) & 0x3FFF;
    out.colorInfo[0] = Reg(ColorInfo0);
    out.colorInfo[1] = Reg(ColorInfo1);
    out.colorInfo[2] = Reg(ColorInfo2);
    out.colorInfo[3] = Reg(ColorInfo3);
    out.depthInfo = Reg(DepthInfo);
    out.modeControl = Reg(ModeControl) & 7;
    out.colorMask = Reg(ColorMask);
    out.depthControl = Reg(DepthControl);
    out.stencilRefMask = Reg(StencilRefMask);

    out.suScModeControl = Reg(SuScModeControl);
    out.scissorTopLeft = Reg(ScissorTopLeft);
    out.scissorBottomRight = Reg(ScissorBottomRight);
    out.windowOffset = Reg(WindowOffset);
    out.indexOffset = int32_t(Reg(IndexOffset));
    out.resetIndex = Reg(ResetIndex);
    out.pointSize = Reg(PointSize);

    out.blendControl[0] = Reg(BlendControl0);
    out.blendControl[1] = Reg(BlendControl1);
    out.blendControl[2] = Reg(BlendControl2);
    out.blendControl[3] = Reg(BlendControl3);
    for (uint32_t i = 0; i < 4; i++) out.blendFactor[i] = RegFloat(BlendRed + i);

    out.vteControl = Reg(VteControl);
    out.colorControl = Reg(ColorControl);
    out.alphaReference = Reg(AlphaReference);
    for (uint32_t i = 0; i < 6; i++) out.viewport[i] = RegFloat(ViewportXScale + i);

    out.vertexProgram = Render::CurrentProgramHash(false);
    out.pixelProgram = Render::CurrentProgramHash(true);
}

void RenderState::ReadTextureFetch(uint32_t slot, uint32_t words[6])
{
    for (uint32_t i = 0; i < 6; i++) words[i] = Reg(FetchConstants + slot * 6 + i);
}

void RenderState::ReadVertexFetch(uint32_t slot, uint32_t& word0, uint32_t& word1)
{
    word0 = Reg(FetchConstants + slot * 2);
    word1 = Reg(FetchConstants + slot * 2 + 1);
}
