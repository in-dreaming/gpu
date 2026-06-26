#pragma once

#include "render/frame_data.h"
#include "render/pipelines.h"

// One-time bindless descriptor binding for all shader root objects.
bool bindAllPassResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);

// Re-bind forward pass GPU resources each frame (graphics pass direct binding).
bool bindForwardFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);

// Re-bind forward shadow map SRVs each frame (graphics pass direct binding).
bool bindForwardShadowResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);

// Re-bind gbuffer pass GPU resources each frame.
bool bindGbufferFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);

// Re-bind SSGI compute inputs/output each frame.
bool bindSsgiFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);
