#pragma once

#include "gpu/rendergraph/gpu_render_graph.h"
#include "render/frame_data.h"

void gbufferPassCallback(GpuGraphPassContext* ctx, void* userData);
void shadowPassCallback(GpuGraphPassContext* ctx, void* userData);
void forwardPassCallback(GpuGraphPassContext* ctx, void* userData);
void ssgiPassCallback(GpuGraphPassContext* ctx, void* userData);
void lightCullPassCallback(GpuGraphPassContext* ctx, void* userData);
