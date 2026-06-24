#pragma once

#include "render/frame_data.h"
#include "render/pipelines.h"

// One-time bindless descriptor binding for all shader root objects.
bool bindAllPassResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures = false);
