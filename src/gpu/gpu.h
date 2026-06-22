#pragma once

// Core
#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_render_pass.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"

// Platform
#include "gpu/platform/gpu_platform.h"
#include "gpu/platform/gpu_surface.h"
#include "gpu/platform/gpu_swapchain.h"

// Capabilities & Debug
#include "gpu/capability/gpu_capabilities.h"
#include "gpu/capability/gpu_feature_info.h"
#include "gpu/debug/gpu_debug.h"

// Sync
#include "gpu/sync/gpu_fence.h"

// Resources
#include "gpu/resource/gpu_barrier.h"
#include "gpu/resource/gpu_resource_manager.h"
#include "gpu/resource/gpu_readback.h"
#include "gpu/resource/gpu_streaming_manager.h"
#include "gpu/resource/gpu_sparse_resource.h"

// Shader
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/shader/gpu_slang_module.h"
#include "gpu/shader/gpu_shader_cache.h"

// Pipeline (Phase 2)
#include "gpu/core/gpu_pipeline.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/pipeline/gpu_pipeline_cache.h"
#include "gpu/pipeline/gpu_cmd_bind.h"
#include "gpu/pipeline/gpu_mesh_pipeline.h"

// Layout (Phase 2)
#include "gpu/reflection/gpu_type_info.h"
#include "gpu/layout/gpu_shader_object.h"
#include "gpu/layout/gpu_parameter_block.h"

// Queue (Phase 2)
#include "gpu/queue/gpu_multi_queue.h"

// Bindless
#include "gpu/bindless/gpu_bindless_heap.h"

// Capability (Phase 3)
#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/capability/gpu_fallback_registry.h"

// AI / Neural Compute (Phase 3)
#include "gpu/ai/gpu_tensor.h"
#include "gpu/ai/gpu_matrix_pipeline.h"
#include "gpu/ai/gpu_neural.h"

// Work Graph (Phase 3)
#include "gpu/workgraph/gpu_work_graph.h"

// Ray Tracing (Phase 3)
#include "gpu/raytracing/gpu_raytracing.h"
