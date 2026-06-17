#include "gpu/core/gpu_render_pass.h"
#include "gpu/core/gpu_command.h"
#include "gpu/platform/gpu_surface.h"
#include "gpu/core/gpu_internal.h"

void gpuCmdClearSurfaceTexture(GpuCommandEncoder encoder, GpuSurfaceTexture texture, float r, float g, float b, float a)
{
    if (!encoder || !texture) return;

    GpuCommandEncoder_t* enc = static_cast<GpuCommandEncoder_t*>(encoder);
    GpuSurfaceTexture_t* surfTex = static_cast<GpuSurfaceTexture_t*>(texture);

    rhi::SubresourceRange range = {};
    range.layerCount = 1;
    range.mipCount = 1;

    float clearValue[4] = { r, g, b, a };
    enc->rhiEncoder->clearTextureFloat(surfTex->rhiTexture, range, clearValue);
}
