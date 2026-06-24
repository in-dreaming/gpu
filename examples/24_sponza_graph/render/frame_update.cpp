#include "frame_data.h"

#include <algorithm>
#include <cmath>
#include <vector>

void updatePointLights(FrameData& frame, const Vec3& sceneCenter, float timeSec)
{
    if (!frame.features.pointLights || !frame.device || !frame.resources) return;

    const uint32_t count = frame.features.pointLightCount;
    std::vector<PointLightData> lights(count);
    for (uint32_t i = 0; i < count; i++) {
        float angle = (float)i / (float)count * 6.28318f + timeSec * 0.2f;
        float angle2 = (float)(i * 7 % 773) / 773.0f * 6.28318f + timeSec * 0.3f;
        float radius = 200.0f + (float)(i % 5) * 150.0f;
        float heightVar = sinf(angle2) * 150.0f;
        lights[i].position[0] = sceneCenter.x + cosf(angle) * radius;
        lights[i].position[1] = sceneCenter.y + 80.0f + heightVar;
        lights[i].position[2] = sceneCenter.z + sinf(angle) * radius;
        lights[i].radius = 80.0f + (float)(i % 4) * 60.0f;
        float hue = (float)i / (float)count;
        lights[i].color[0] = 0.5f + 0.5f * sinf(hue * 6.28318f + timeSec * 0.5f);
        lights[i].color[1] = 0.5f + 0.5f * sinf((hue + 0.33f) * 6.28318f + timeSec * 0.5f);
        lights[i].color[2] = 0.5f + 0.5f * sinf((hue + 0.67f) * 6.28318f + timeSec * 0.5f);
        lights[i].shadowMapIndex = -1;
    }

    if (frame.features.pointShadows) {
        uint32_t shadowSlots = std::min(count, kMaxPointShadowSlots);
        std::vector<std::pair<float, uint32_t>> nearest(count);
        // Camera position is embedded in frame.cameraParams.
        const float camX = frame.cameraParams.cameraPos[0];
        const float camY = frame.cameraParams.cameraPos[1];
        const float camZ = frame.cameraParams.cameraPos[2];
        for (uint32_t i = 0; i < count; i++) {
            float dx = lights[i].position[0] - camX;
            float dy = lights[i].position[1] - camY;
            float dz = lights[i].position[2] - camZ;
            nearest[i] = {dx * dx + dy * dy + dz * dz, i};
        }
        std::partial_sort(nearest.begin(), nearest.begin() + shadowSlots, nearest.end());

        for (uint32_t slot = 0; slot < shadowSlots; slot++) {
            uint32_t li = nearest[slot].second;
            lights[li].shadowMapIndex = (int32_t)slot;

            Vec3 lpos = {lights[li].position[0], lights[li].position[1], lights[li].position[2]};
            float nearZ = 0.25f;
            float farZ = std::max(lights[li].radius * 1.05f, 80.0f);
            frame.pointShadowNear[slot] = nearZ;
            frame.pointShadowFar[slot] = farZ;
            for (int face = 0; face < kCubeFaceCount; face++)
                buildPointCubeFaceViewProj(lpos, face, nearZ, farZ, frame.pointShadowViewProj[slot][face]);
        }
    }

    gpuUploadToBuffer(frame.device, frame.resources->lightBuffer, lights.data(), count * sizeof(PointLightData), 0);
    frame.lightCount = count;
}
