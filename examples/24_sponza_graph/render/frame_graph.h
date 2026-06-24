#pragma once

#include "gpu/rendergraph/gpu_render_graph.h"
#include "render/frame_data.h"

// Build, compile, and execute one frame using gpuGraph* (render graph API).
// Returns false when graph compile/execute fails.
bool executeSponzaFrameGraph(const FrameGraphContext& ctx);
