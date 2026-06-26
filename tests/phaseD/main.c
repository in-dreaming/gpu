#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { GpuResult _r = (expr); if (_r != GPU_SUCCESS) { fprintf(stderr, "  FAIL: line %d: %s returned %d\n", __LINE__, #expr, _r); return 1; } } while(0)
#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "  FAIL: line %d: %s is false\n", __LINE__, #expr); return 1; } } while(0)

static void flush(void) { fflush(stdout); fflush(stderr); }

int main(void)
{
    printf("=== Phase D: Descriptor/Layout/Pipeline Test ===\n\n"); flush();

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phaseD_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

    GpuShaderCompiler compiler;
    CHECK(gpuCreateShaderCompiler(device, &compiler));

    int passed = 0;

    // =========================================================================
    // [D.1] Pipeline layout reflection - compile shader and reflect layout
    // =========================================================================
    printf("[D.1] Pipeline layout reflection\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));
        CHECK_TRUE(layout != NULL);

        GpuPipelineLayoutInfo info;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info));

        printf("  layoutHash: 0x%llx\n", (unsigned long long)info.layoutHash);
        printf("  descriptorSetCount: %u\n", info.descriptorSetCount);
        printf("  bindingRangeCount: %u\n", info.bindingRangeCount);
        printf("  entryPointCount: %u\n", info.entryPointCount);
        printf("  globalConstantBufferSize: %u\n", info.globalConstantBufferSize);
        printf("  bindlessSpaceIndex: %d\n", info.bindlessSpaceIndex);

        CHECK_TRUE(info.layoutHash != 0);
        CHECK_TRUE(info.entryPointCount >= 2);  // vertex + fragment

        // Print entry points
        for (uint32_t i = 0; i < info.entryPointCount; i++) {
            printf("  entryPoint[%u]: name=%s stage=%u\n",
                   i, info.entryPoints[i].name ? info.entryPoints[i].name : "(null)",
                   info.entryPoints[i].stage);
        }

        // Print binding ranges
        for (uint32_t i = 0; i < info.bindingRangeCount; i++) {
            const GpuBindingRange* r = &info.bindingRanges[i];
            printf("  bindingRange[%u]: set=%u binding=%u kind=%u name=%s writable=%d\n",
                   i, r->set, r->binding, r->kind,
                   r->name ? r->name : "(null)", r->writable);
        }

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.2] Binding ranges - verify binding ranges are extracted
    // =========================================================================
    printf("[D.2] Binding ranges extraction\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuPipelineLayoutInfo info;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info));

        // We expect at least some binding ranges from the global params
        // (ConstantBuffer, Texture, Sampler, StructuredBuffer, RWStructuredBuffer)
        CHECK_TRUE(info.bindingRangeCount > 0);

        // Verify we have at least one constant buffer binding
        bool hasConstantBuffer = false;
        bool hasTexture = false;
        bool hasSampler = false;
        bool hasTypedBuffer = false;
        bool hasWritable = false;

        for (uint32_t i = 0; i < info.bindingRangeCount; i++) {
            const GpuBindingRange* r = &info.bindingRanges[i];
            if (r->kind == GPU_BINDING_KIND_CONSTANT_BUFFER) hasConstantBuffer = true;
            if (r->kind == GPU_BINDING_KIND_TEXTURE) hasTexture = true;
            if (r->kind == GPU_BINDING_KIND_SAMPLER) hasSampler = true;
            if (r->kind == GPU_BINDING_KIND_TYPED_BUFFER ||
                r->kind == GPU_BINDING_KIND_RAW_BUFFER) hasTypedBuffer = true;
            if (r->writable) hasWritable = true;
        }

        printf("  hasConstantBuffer=%d hasTexture=%d hasSampler=%d hasTypedBuffer=%d hasWritable=%d\n",
               hasConstantBuffer, hasTexture, hasSampler, hasTypedBuffer, hasWritable);

        CHECK_TRUE(hasConstantBuffer);
        CHECK_TRUE(hasTexture);
        CHECK_TRUE(hasSampler);

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.3] Descriptor sets - verify descriptor set grouping
    // =========================================================================
    printf("[D.3] Descriptor set grouping\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuPipelineLayoutInfo info;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info));

        CHECK_TRUE(info.descriptorSetCount > 0);

        // Verify each descriptor set has valid data
        for (uint32_t i = 0; i < info.descriptorSetCount; i++) {
            const GpuDescriptorSetLayoutInfo* ds = &info.descriptorSets[i];
            printf("  descriptorSet[%u]: set=%u space=%u bindingCount=%u\n",
                   i, ds->set, ds->space, ds->bindingCount);
            CHECK_TRUE(ds->bindings != NULL || ds->bindingCount == 0);
        }

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.4] Entry points - verify entry point info
    // =========================================================================
    printf("[D.4] Entry point info\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuPipelineLayoutInfo info;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info));

        // Should have vertex and fragment entry points
        bool hasVertex = false;
        bool hasFragment = false;

        for (uint32_t i = 0; i < info.entryPointCount; i++) {
            const GpuEntryPointInfo* ep = &info.entryPoints[i];
            if (ep->stage == GPU_STAGE_VERTEX) hasVertex = true;
            if (ep->stage == GPU_STAGE_FRAGMENT) hasFragment = true;
        }

        CHECK_TRUE(hasVertex);
        CHECK_TRUE(hasFragment);

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.5] Layout hash - verify hash is non-zero and deterministic
    // =========================================================================
    printf("[D.5] Layout hash determinism\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog1 = NULL;
        GpuShaderProgram prog2 = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog1));
        CHECK(gpuCompileShader(compiler, &cdesc, &prog2));

        GpuPipelineLayout layout1 = NULL;
        GpuPipelineLayout layout2 = NULL;
        CHECK(gpuReflectPipelineLayout(prog1, &layout1));
        CHECK(gpuReflectPipelineLayout(prog2, &layout2));

        uint64_t hash1 = gpuGetPipelineLayoutHash(layout1);
        uint64_t hash2 = gpuGetPipelineLayoutHash(layout2);

        printf("  hash1=0x%llx hash2=0x%llx\n", (unsigned long long)hash1, (unsigned long long)hash2);
        CHECK_TRUE(hash1 != 0);
        CHECK_TRUE(hash1 == hash2);

        gpuDestroyPipelineLayout(layout1);
        gpuDestroyPipelineLayout(layout2);
        gpuDestroyShaderProgram(prog1);
        gpuDestroyShaderProgram(prog2);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.6] JSON serialization - serialize layout to JSON
    // =========================================================================
    printf("[D.6] JSON serialization\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        // Get JSON string
        char* jsonStr = NULL;
        CHECK(gpuGetPipelineLayoutJson(layout, &jsonStr));
        CHECK_TRUE(jsonStr != NULL);

        // Verify JSON contains expected fields
        CHECK_TRUE(strstr(jsonStr, "layoutHash") != NULL);
        CHECK_TRUE(strstr(jsonStr, "entryPoints") != NULL);
        CHECK_TRUE(strstr(jsonStr, "bindingRanges") != NULL);
        CHECK_TRUE(strstr(jsonStr, "descriptorSets") != NULL);

        printf("  JSON length: %zu\n", strlen(jsonStr));

        gpuFreePipelineLayoutJson(jsonStr);

        // Also test file serialization
        CHECK(gpuSerializePipelineLayoutJson(layout, "phaseD_layout.json"));
        printf("  Written to phaseD_layout.json\n");

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.7] Sampler creation
    // =========================================================================
    printf("[D.7] Sampler creation\n"); flush();
    {
        GpuSamplerDesc sdesc = {
            .minFilter = GPU_FILTER_LINEAR,
            .magFilter = GPU_FILTER_LINEAR,
            .mipFilter = GPU_FILTER_LINEAR,
            .addressModeU = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .minLod = 0.0f,
            .maxLod = 1000.0f,
            .lodBias = 0.0f,
            .maxAnisotropy = 1,
            .compareEnable = false,
            .compareOp = 0,
            .reductionMode = GPU_SAMPLER_REDUCTION_MODE_STANDARD,
            .borderColor = {0, 0, 0, 0},
            .unnormalizedCoordinates = false,
            .label = "test_sampler",
        };
        GpuSamplerHandle sampler = {0, 0};
        CHECK(gpuCreateSampler(device, &sdesc, &sampler));
        CHECK_TRUE(sampler.index != 0);

        printf("  sampler handle: index=%u gen=%u\n", sampler.index, sampler.generation);

        gpuDestroySampler(device, sampler);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.8] Bindless heap with texture
    // =========================================================================
    printf("[D.8] Bindless heap with texture\n"); flush();
    {
        GpuBindlessHeapDesc hdesc = {
            .maxDescriptors = 64,
            .descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE,
        };
        GpuBindlessHeap heap = NULL;
        CHECK(gpuCreateBindlessHeap(device, &hdesc, &heap));
        CHECK_TRUE(heap != NULL);

        // Create a test texture
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = 64, .height = 64, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "bindless_test_tex",
        };
        GpuTextureHandle tex = {0, 0};
        CHECK(gpuCreateTexture(device, &tdesc, &tex));

        // Allocate in bindless heap
        uint32_t idx = gpuBindlessAllocate(heap, (GpuHandle){tex.index, tex.generation});
        CHECK_TRUE(idx != UINT32_MAX);
        printf("  bindless index: %u\n", idx);

        // Verify allocation
        CHECK_TRUE(gpuBindlessIsAllocated(heap, idx));

        // Get descriptor handle info
        GpuDescriptorHandleInfo dhInfo;
        CHECK(gpuBindlessGetDescriptorHandle(heap, idx, &dhInfo));
        printf("  descriptor handle: type=%u value=%llu\n", dhInfo.type, (unsigned long long)dhInfo.value);

        // Free
        gpuBindlessFree(heap, idx);
        CHECK_TRUE(!gpuBindlessIsAllocated(heap, idx));

        gpuDestroyTexture(device, tex);
        gpuDestroyBindlessHeap(heap);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.9] Bindless heap stats
    // =========================================================================
    printf("[D.9] Bindless heap stats\n"); flush();
    {
        GpuBindlessHeapDesc hdesc = {
            .maxDescriptors = 32,
            .descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE,
        };
        GpuBindlessHeap heap = NULL;
        CHECK(gpuCreateBindlessHeap(device, &hdesc, &heap));

        GpuBindlessHeapStats stats;
        gpuGetBindlessHeapStats(heap, &stats);
        CHECK_TRUE(stats.totalAllocated == 0);
        CHECK_TRUE(stats.capacity == 32);
        printf("  initial: allocated=%u capacity=%u\n", stats.totalAllocated, stats.capacity);

        // Create textures and allocate
        GpuTextureHandle texs[4];
        uint32_t indices[4];
        for (int i = 0; i < 4; i++) {
            GpuTextureDesc tdesc = {
                .type = GPU_TEXTURE_TYPE_2D,
                .width = 16, .height = 16, .depth = 1,
                .arrayLength = 1, .mipCount = 1,
                .format = GPU_FORMAT_RGBA8_UNORM,
                .sampleCount = 1,
                .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
                .label = "stat_test_tex",
            };
            CHECK(gpuCreateTexture(device, &tdesc, &texs[i]));
            indices[i] = gpuBindlessAllocate(heap, (GpuHandle){texs[i].index, texs[i].generation});
            CHECK_TRUE(indices[i] != UINT32_MAX);
        }

        gpuGetBindlessHeapStats(heap, &stats);
        CHECK_TRUE(stats.totalAllocated == 4);
        CHECK_TRUE(stats.allocatedTextures == 4);
        printf("  after 4 allocs: allocatedTextures=%u totalAllocated=%u\n",
               stats.allocatedTextures, stats.totalAllocated);

        // Free 2
        gpuBindlessFree(heap, indices[0]);
        gpuBindlessFree(heap, indices[1]);

        gpuGetBindlessHeapStats(heap, &stats);
        CHECK_TRUE(stats.totalAllocated == 2);
        printf("  after 2 frees: totalAllocated=%u\n", stats.totalAllocated);

        // Cleanup
        for (int i = 0; i < 4; i++) {
            gpuDestroyTexture(device, texs[i]);
        }
        gpuDestroyBindlessHeap(heap);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.10] Descriptor pool
    // =========================================================================
    printf("[D.10] Descriptor pool\n"); flush();
    {
        GpuDescriptorPoolDesc pdesc = {
            .maxSets = 256,
            .maxBindingsPerSet = 32,
        };
        GpuDescriptorPool pool = NULL;
        CHECK(gpuCreateDescriptorPool(device, &pdesc, &pool));
        CHECK_TRUE(pool != NULL);

        GpuDescriptorPoolStats stats;
        gpuGetDescriptorPoolStats(pool, &stats);
        CHECK_TRUE(stats.allocatedSets == 0);
        CHECK_TRUE(stats.freeSets == 256);
        printf("  initial: allocatedSets=%u freeSets=%u\n", stats.allocatedSets, stats.freeSets);

        gpuDestroyDescriptorPool(pool);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.11] Pipeline cache with layout hash
    // =========================================================================
    printf("[D.11] Pipeline cache with layout hash\n"); flush();
    {
        GpuPipelineCacheDesc pcDesc = { .cachePath = NULL };
        GpuPipelineCache cache = NULL;
        CHECK(gpuCreatePipelineCache(device, &pcDesc, &cache));
        CHECK_TRUE(cache != NULL);

        // Create a pipeline desc and hash it with a layout hash
        GpuPipelineDesc pdesc = {
            .type = GPU_PIPELINE_TYPE_COMPUTE,
            .compute = {
                .label = "test_compute",
                .computeShader = { .data = NULL, .size = 0 },
            },
        };

        uint8_t hash1[32], hash2[32];
        CHECK(gpuHashPipelineDesc(&pdesc, hash1));
        CHECK(gpuHashPipelineDescWithLayout(&pdesc, 0x12345678ABCDEF00ULL, hash2));

        // The hashes should be different since layout hash is included
        CHECK_TRUE(memcmp(hash1, hash2, 32) != 0);
        printf("  hash without layout != hash with layout (correct)\n");

        // Same layout hash should produce same hash
        uint8_t hash3[32];
        CHECK(gpuHashPipelineDescWithLayout(&pdesc, 0x12345678ABCDEF00ULL, hash3));
        CHECK_TRUE(memcmp(hash2, hash3, 32) == 0);
        printf("  same layout hash -> same pipeline hash (deterministic)\n");

        // Different layout hash should produce different hash
        uint8_t hash4[32];
        CHECK(gpuHashPipelineDescWithLayout(&pdesc, 0xDEADBEEFCAFEBABEULL, hash4));
        CHECK_TRUE(memcmp(hash2, hash4, 32) != 0);
        printf("  different layout hash -> different pipeline hash\n");

        gpuDestroyPipelineCache(device, cache);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.12] Pipeline cache stats
    // =========================================================================
    printf("[D.12] Pipeline cache stats\n"); flush();
    {
        GpuPipelineCacheDesc pcDesc = { .cachePath = NULL };
        GpuPipelineCache cache = NULL;
        CHECK(gpuCreatePipelineCache(device, &pcDesc, &cache));

        // Create a fake pipeline handle for testing
        GpuPipelineHandle fakeHandle = { 1, 1 };
        uint8_t hash[32];
        memset(hash, 0x42, 32);

        // Store
        CHECK(gpuPipelineCacheStore(cache, hash, fakeHandle));

        uint32_t hits, misses, entries;
        gpuPipelineCacheGetStats(cache, &hits, &misses, &entries);
        CHECK_TRUE(entries == 1);
        CHECK_TRUE(hits == 0);
        CHECK_TRUE(misses == 0);
        printf("  after store: entries=%u hits=%u misses=%u\n", entries, hits, misses);

        // Lookup (hit)
        GpuPipelineHandle foundHandle;
        CHECK(gpuPipelineCacheLookup(cache, hash, &foundHandle));

        gpuPipelineCacheGetStats(cache, &hits, &misses, &entries);
        CHECK_TRUE(hits == 1);
        printf("  after hit lookup: hits=%u\n", hits);

        // Lookup (miss) with different hash
        uint8_t hash2[32];
        memset(hash2, 0x99, 32);
        GpuResult res = gpuPipelineCacheLookup(cache, hash2, &foundHandle);
        CHECK_TRUE(res == GPU_ERROR_NOT_FOUND);

        gpuPipelineCacheGetStats(cache, &hits, &misses, &entries);
        CHECK_TRUE(misses == 1);
        printf("  after miss lookup: misses=%u\n", misses);

        gpuDestroyPipelineCache(device, cache);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.13] Compute shader layout reflection
    // =========================================================================
    printf("[D.13] Compute shader layout reflection\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "computeMain",
            .fragmentEntryPoint = NULL,
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuPipelineLayoutInfo info;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info));

        // Should have compute entry point
        bool hasCompute = false;
        for (uint32_t i = 0; i < info.entryPointCount; i++) {
            if (info.entryPoints[i].stage == GPU_STAGE_COMPUTE) {
                hasCompute = true;
                printf("  compute entry point: threadGroupSize=[%u, %u, %u]\n",
                       info.entryPoints[i].threadGroupSizeX,
                       info.entryPoints[i].threadGroupSizeY,
                       info.entryPoints[i].threadGroupSizeZ);
                // The shader has [numthreads(8, 8, 1)]
                CHECK_TRUE(info.entryPoints[i].threadGroupSizeX == 8);
                CHECK_TRUE(info.entryPoints[i].threadGroupSizeY == 8);
                CHECK_TRUE(info.entryPoints[i].threadGroupSizeZ == 1);
            }
        }
        CHECK_TRUE(hasCompute);

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.14] Invalid args - verify error handling
    // =========================================================================
    printf("[D.14] Invalid args\n"); flush();
    {
        GpuResult res;

        // Null device
        res = gpuCreateSampler(NULL, NULL, NULL);
        CHECK_TRUE(res != GPU_SUCCESS);

        // Null program
        GpuPipelineLayout layout = NULL;
        res = gpuReflectPipelineLayout(NULL, &layout);
        CHECK_TRUE(res != GPU_SUCCESS);

        // Null layout
        GpuPipelineLayoutInfo info;
        res = gpuGetPipelineLayoutInfo(NULL, &info);
        CHECK_TRUE(res != GPU_SUCCESS);

        // Null heap
        GpuBindlessHeapStats stats;
        gpuGetBindlessHeapStats(NULL, &stats);

        // Null pool
        GpuDescriptorPoolStats poolStats;
        gpuGetDescriptorPoolStats(NULL, &poolStats);

        // Destroy null is safe
        gpuDestroyPipelineLayout(NULL);
        gpuDestroyDescriptorPool(NULL);
        gpuDestroyBindlessHeap(NULL);
        gpuDestroySampler(device, (GpuSamplerHandle){0, 0});

        printf("  all invalid arg checks passed\n");
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.15] Layout info stability - verify pointers remain valid
    // =========================================================================
    printf("[D.15] Layout info pointer stability\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuPipelineLayoutInfo info1, info2;
        CHECK(gpuGetPipelineLayoutInfo(layout, &info1));
        CHECK(gpuGetPipelineLayoutInfo(layout, &info2));

        // Pointers should be the same (data is stable)
        CHECK_TRUE(info1.bindingRanges == info2.bindingRanges);
        CHECK_TRUE(info1.descriptorSets == info2.descriptorSets);
        CHECK_TRUE(info1.entryPoints == info2.entryPoints);
        CHECK_TRUE(info1.layoutHash == info2.layoutHash);

        // Verify binding range names are accessible
        for (uint32_t i = 0; i < info1.bindingRangeCount; i++) {
            if (info1.bindingRanges[i].name) {
                // Just access the string to verify it's valid (may be empty)
                size_t len = strlen(info1.bindingRanges[i].name);
                (void)len;
            }
        }

        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.16] Stress - reflect multiple times
    // =========================================================================
    printf("[D.16] Stress - reflect 100 times\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        uint64_t firstHash = 0;
        for (int i = 0; i < 100; i++) {
            GpuPipelineLayout layout = NULL;
            CHECK(gpuReflectPipelineLayout(prog, &layout));

            uint64_t hash = gpuGetPipelineLayoutHash(layout);
            if (i == 0) firstHash = hash;
            CHECK_TRUE(hash == firstHash);

            gpuDestroyPipelineLayout(layout);
        }
        printf("  100 reflections, all hashes match: 0x%llx\n", (unsigned long long)firstHash);

        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.17] Descriptor set allocate / update / validation
    // =========================================================================
    printf("[D.17] Descriptor set API\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuDescriptorPoolDesc pdesc = { .maxSets = 16, .maxBindingsPerSet = 32 };
        GpuDescriptorPool pool = NULL;
        CHECK(gpuCreateDescriptorPool(device, &pdesc, &pool));

        GpuDescriptorSet set = NULL;
        CHECK(gpuAllocateDescriptorSet(pool, layout, 0, &set));
        CHECK_TRUE(set != NULL);

        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 16,
            .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER | GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "desc_set_buf"
        };
        GpuBufferHandle buf = {0, 0};
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));

        GpuDescriptorWrite wBuf = { .type = GPU_DESCRIPTOR_WRITE_BUFFER, .buffer = buf };
        CHECK(gpuUpdateDescriptorSet(set, 0, 0, &wBuf));

        GpuBufferDesc vdesc = {
            .size = 1024, .elementSize = 16,
            .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "desc_set_vertices"
        };
        GpuBufferHandle vbuf = {0, 0};
        CHECK(gpuCreateBuffer(device, &vdesc, &vbuf));
        GpuDescriptorWrite wVert = { .type = GPU_DESCRIPTOR_WRITE_BUFFER, .buffer = vbuf };
        CHECK(gpuUpdateDescriptorSet(set, 1, 0, &wVert));

        GpuDescriptorWrite bad = { .type = GPU_DESCRIPTOR_WRITE_TEXTURE, .texture = {1, 1} };
        CHECK_TRUE(gpuUpdateDescriptorSet(set, 0, 0, &bad) == GPU_ERROR_INVALID_ARGS);

        gpuDestroyBuffer(device, vbuf);

        gpuFreeDescriptorSet(set);
        gpuDestroyBuffer(device, buf);
        gpuDestroyDescriptorPool(pool);
        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.18] Mixed bindless heap per-slot type
    // =========================================================================
    printf("[D.18] Mixed bindless heap\n"); flush();
    {
        GpuBindlessHeapDesc2 hdesc2 = {
            .maxTextures = 8,
            .maxBuffers = 8,
            .maxSamplers = 4,
            .shaderVisible = true,
        };
        GpuBindlessHeap heap = NULL;
        CHECK(gpuCreateBindlessHeap2(device, &hdesc2, &heap));

        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 16, .height = 16, .depth = 1,
            .arrayLength = 1, .mipCount = 1, .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "mixed_tex"
        };
        GpuTextureHandle tex = {0, 0};
        CHECK(gpuCreateTexture(device, &tdesc, &tex));

        GpuBufferDesc bdesc = {
            .size = 128, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "mixed_buf"
        };
        GpuBufferHandle buf = {0, 0};
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));

        GpuSamplerDesc sdesc = {
            .minFilter = GPU_FILTER_LINEAR, .magFilter = GPU_FILTER_LINEAR,
            .mipFilter = GPU_FILTER_LINEAR,
            .addressModeU = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .label = "mixed_sampler"
        };
        GpuSamplerHandle sampler = {0, 0};
        CHECK(gpuCreateSampler(device, &sdesc, &sampler));

        uint32_t texIdx = gpuBindlessRegister(heap, (GpuHandle){tex.index, tex.generation}, GPU_DESCRIPTOR_TYPE_TEXTURE);
        uint32_t bufIdx = gpuBindlessRegister(heap, (GpuHandle){buf.index, buf.generation}, GPU_DESCRIPTOR_TYPE_BUFFER);
        uint32_t sampIdx = gpuBindlessRegister(heap, (GpuHandle){sampler.index, sampler.generation}, GPU_DESCRIPTOR_TYPE_SAMPLER);
        CHECK_TRUE(texIdx != UINT32_MAX);
        CHECK_TRUE(bufIdx != UINT32_MAX);
        CHECK_TRUE(sampIdx != UINT32_MAX);

        GpuDescriptorType slotType = 0;
        CHECK(gpuBindlessGetSlotType(heap, texIdx, &slotType));
        CHECK_TRUE(slotType == GPU_DESCRIPTOR_TYPE_TEXTURE);
        CHECK(gpuBindlessValidateSlot(heap, bufIdx, GPU_DESCRIPTOR_TYPE_BUFFER));
        CHECK_TRUE(gpuBindlessValidateSlot(heap, texIdx, GPU_DESCRIPTOR_TYPE_BUFFER) == GPU_ERROR_INVALID_ARGS);

        GpuBindlessHeapStats stats;
        gpuGetBindlessHeapStats(heap, &stats);
        CHECK_TRUE(stats.allocatedTextures == 1);
        CHECK_TRUE(stats.allocatedBuffers == 1);
        CHECK_TRUE(stats.allocatedSamplers == 1);
        CHECK_TRUE(stats.totalAllocated == 3);

        gpuBindlessUnregister(heap, texIdx);
        gpuBindlessUnregister(heap, bufIdx);
        gpuBindlessUnregister(heap, sampIdx);
        gpuDestroySampler(device, sampler);
        gpuDestroyBuffer(device, buf);
        gpuDestroyTexture(device, tex);
        gpuDestroyBindlessHeap(heap);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [D.19] Descriptor set update by binding name
    // =========================================================================
    printf("[D.19] Descriptor set by name\n"); flush();
    {
        GpuShaderCompileDesc cdesc = {
            .sourcePath = "reflection_test.slang",
            .entryPoint = "vertexMain",
            .fragmentEntryPoint = "fragmentMain",
            .target = GPU_SHADER_TARGET_SPIRV,
        };
        GpuShaderProgram prog = NULL;
        CHECK(gpuCompileShader(compiler, &cdesc, &prog));

        GpuPipelineLayout layout = NULL;
        CHECK(gpuReflectPipelineLayout(prog, &layout));

        GpuDescriptorPoolDesc pdesc = { .maxSets = 8, .maxBindingsPerSet = 16 };
        GpuDescriptorPool pool = NULL;
        CHECK(gpuCreateDescriptorPool(device, &pdesc, &pool));

        GpuDescriptorSet set = NULL;
        CHECK(gpuAllocateDescriptorSet(pool, layout, 0, &set));

        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 16,
            .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER,
            .label = "named_cb"
        };
        GpuBufferHandle buf = {0, 0};
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));

        GpuDescriptorWrite wBuf = { .type = GPU_DESCRIPTOR_WRITE_BUFFER, .buffer = buf };
        CHECK(gpuUpdateDescriptorSetByName(set, "gUniforms", &wBuf));
        CHECK_TRUE(gpuUpdateDescriptorSetByName(set, "missingBinding", &wBuf) == GPU_ERROR_NOT_FOUND);

        gpuFreeDescriptorSet(set);
        gpuDestroyBuffer(device, buf);
        gpuDestroyDescriptorPool(pool);
        gpuDestroyPipelineLayout(layout);
        gpuDestroyShaderProgram(prog);
    }
    printf("  OK\n"); flush();

    // Cleanup
    gpuDestroyShaderCompiler(compiler);
    gpuDestroyDevice(device);

    printf("\nALL PASSED\n"); flush();
    passed = 1;
    return passed ? 0 : 1;
}
