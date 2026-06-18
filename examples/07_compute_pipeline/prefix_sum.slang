// prefix_sum.slang - GPU prefix sum compute shader
// This shader computes an exclusive prefix sum (scan) of input data

StructuredBuffer<float> gInput : register(t0);
RWStructuredBuffer<float> gOutput : register(u0);

// Shared memory for parallel scan within workgroup
groupshared float sharedData[256];

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint3 lid : SV_GroupThreadID) {
    uint globalId = tid.x;
    uint localId = lid.x;
    uint groupId = gid.x;
    
    // Load data into shared memory
    // For simplicity, this example assumes input size <= 256 elements
    // and only uses one workgroup
    float value = gInput[globalId];
    sharedData[localId] = value;
    
    // Wait for all threads to load
    GroupMemoryBarrierWithGroupSync();
    
    // Up-sweep phase (reduce)
    for (uint stride = 1; stride < 256; stride *= 2) {
        uint index = (localId + 1) * stride * 2 - 1;
        if (index < 256) {
            sharedData[index] += sharedData[index - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    // Down-sweep phase
    if (localId == 0) {
        sharedData[255] = 0; // Clear the last element for exclusive scan
    }
    GroupMemoryBarrierWithGroupSync();
    
    for (uint stride = 128; stride > 0; stride /= 2) {
        uint index = (localId + 1) * stride * 2 - 1;
        if (index < 256) {
            float temp = sharedData[index - stride];
            sharedData[index - stride] = sharedData[index];
            sharedData[index] += temp;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    // Write result
    gOutput[globalId] = sharedData[localId];
}

// Simple compute shader for testing - just copies and sums
[shader("compute")]
[numthreads(64, 1, 1)]
void sumMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    
    // Simple prefix sum - accumulate all elements before this index
    float sum = 0.0;
    for (uint i = 0; i <= idx && i < 256; i++) {
        sum += gInput[i];
    }
    
    gOutput[idx] = sum;
}
