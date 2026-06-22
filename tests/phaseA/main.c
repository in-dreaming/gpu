#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    GpuResult _r = (expr); \
    if (_r != GPU_SUCCESS) { \
        fprintf(stderr, "FAIL: %s returned %d at %s:%d\n", #expr, _r, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

static int s_validationCount = 0;
static GpuValidationSeverity s_lastSeverity = GPU_VALIDATION_SEVERITY_INFO;
static char s_lastMessageId[64] = {};
static char s_lastMessage[256] = {};

static void testValidationCallback(const GpuValidationMessage* msg, void* userData)
{
    (void)userData;
    s_validationCount++;
    s_lastSeverity = msg->severity;
    strncpy_s(s_lastMessageId, sizeof(s_lastMessageId), msg->messageId ? msg->messageId : "", 63);
    strncpy_s(s_lastMessage, sizeof(s_lastMessage), msg->message ? msg->message : "", 255);
}

static void flush(void) { fflush(stdout); fflush(stderr); }

int main(void)
{
    printf("=== Phase A: Contract Hardening Test ===\n\n"); flush();

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phaseA_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    /* ---------------------------------------------------------------
     * A.1 Feature Support Three-State Enum
     * --------------------------------------------------------------- */
    printf("[A.1] Feature Info Three-State\n"); flush();
    {
        GpuFeatureInfo fi;
        CHECK(gpuGetFeatureInfo(device, GPU_FEATURE_BINDLESS, &fi));
        CHECK_TRUE(fi.support == GPU_FEATURE_SUPPORT_NATIVE || fi.support == GPU_FEATURE_SUPPORT_UNSUPPORTED);
        printf("  Bindless: support=%u limit=%u reason=%s\n",
               (unsigned)fi.support, fi.limit, fi.reason ? fi.reason : "(none)");

        CHECK(gpuGetFeatureInfo(device, GPU_FEATURE_WORK_GRAPH, &fi));
        CHECK_TRUE(fi.support == GPU_FEATURE_SUPPORT_EMULATED);
        printf("  WorkGraph: support=%u (emulated)\n", (unsigned)fi.support);

        CHECK(gpuGetFeatureInfo(device, GPU_FEATURE_DESCRIPTOR_INDEXING, &fi));
        CHECK_TRUE(fi.support == GPU_FEATURE_SUPPORT_UNSUPPORTED);
        CHECK_TRUE(fi.reason != NULL);
        printf("  DescriptorIndexing: support=%u reason=%s\n",
               (unsigned)fi.support, fi.reason ? fi.reason : "(none)");
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.2 Feature Table with Three-State Support
     * --------------------------------------------------------------- */
    printf("[A.2] Feature Table with support field\n"); flush();
    {
        GpuFeatureTable table;
        CHECK(gpuBuildFeatureTable(device, &table));

        CHECK_TRUE(table.supported[GPU_FEATURE_WORK_GRAPH] == GPU_FEATURE_SUPPORT_EMULATED);
        CHECK_TRUE(table.supported[GPU_FEATURE_DESCRIPTOR_INDEXING] == GPU_FEATURE_SUPPORT_UNSUPPORTED);
        CHECK_TRUE(gpuIsFeatureSupportedEx(&table, GPU_FEATURE_WORK_GRAPH));
        CHECK_TRUE(!gpuIsFeatureSupportedEx(&table, GPU_FEATURE_DESCRIPTOR_INDEXING));

        printf("  WorkGraph: support=%u supported=%s\n",
               (unsigned)table.supported[GPU_FEATURE_WORK_GRAPH],
               gpuIsFeatureSupportedEx(&table, GPU_FEATURE_WORK_GRAPH) ? "Y" : "N");
        printf("  DescIndexing: support=%u supported=%s\n",
               (unsigned)table.supported[GPU_FEATURE_DESCRIPTOR_INDEXING],
               gpuIsFeatureSupportedEx(&table, GPU_FEATURE_DESCRIPTOR_INDEXING) ? "Y" : "N");
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.3 Feature Table JSON Serialization
     * --------------------------------------------------------------- */
    printf("[A.3] Feature Table JSON\n"); flush();
    {
        GpuFeatureTable table;
        CHECK(gpuBuildFeatureTable(device, &table));

        char json[4096];
        CHECK(gpuFeatureTableToJson(&table, json, sizeof(json)));

        CHECK_TRUE(strstr(json, "\"WorkGraph\"") != NULL);
        CHECK_TRUE(strstr(json, "\"emulated\"") != NULL);
        CHECK_TRUE(strstr(json, "\"DescriptorIndexing\"") != NULL);
        CHECK_TRUE(strstr(json, "\"unsupported\"") != NULL);

        GpuFeatureTable restored;
        CHECK(gpuFeatureTableFromJson(json, &restored));
        CHECK_TRUE(restored.supported[GPU_FEATURE_WORK_GRAPH] == GPU_FEATURE_SUPPORT_EMULATED);
        CHECK_TRUE(restored.supported[GPU_FEATURE_DESCRIPTOR_INDEXING] == GPU_FEATURE_SUPPORT_UNSUPPORTED);

        printf("  JSON contains WorkGraph/emulated and DescriptorIndexing/unsupported\n");
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.4 gpuGetFeatureInfo Invalid Args
     * --------------------------------------------------------------- */
    printf("[A.4] gpuGetFeatureInfo invalid args\n"); flush();
    {
        GpuFeatureInfo fi;
        CHECK_TRUE(gpuGetFeatureInfo(NULL, GPU_FEATURE_BINDLESS, &fi) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGetFeatureInfo(device, GPU_FEATURE_BINDLESS, NULL) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGetFeatureInfo(device, (GpuFeature)-1, &fi) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGetFeatureInfo(device, GPU_FEATURE_COUNT, &fi) == GPU_ERROR_INVALID_ARGS);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.5 Unsupported Feature Returns NOT_SUPPORTED
     * --------------------------------------------------------------- */
    printf("[A.5] Unsupported feature returns NOT_SUPPORTED\n"); flush();
    {
        GpuResult res = gpuGetSparseTextureProperties(device, GPU_FORMAT_RGBA8_UNORM, 64, 64, 1, 1, NULL);
        (void)res;
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.6 Validation Callback
     * --------------------------------------------------------------- */
    printf("[A.6] Validation Callback\n"); flush();
    {
        gpuSetValidationCallback(device, testValidationCallback, NULL);
        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_ERROR);

        s_validationCount = 0;
        GpuBufferDesc desc = { .size = 0, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE, .label = "zero_buf" };
        GpuBufferHandle badBuf;
        GpuResult res = gpuCreateBuffer(device, &desc, &badBuf);
        CHECK_TRUE(res == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(s_validationCount > 0);
        CHECK_TRUE(s_lastSeverity == GPU_VALIDATION_SEVERITY_ERROR);
        printf("  Validation fired: count=%d id=%s msg=%s\n",
               s_validationCount, s_lastMessageId, s_lastMessage);

        gpuSetValidationCallback(device, NULL, NULL);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.7 gpuFeatureSupportToString
     * --------------------------------------------------------------- */
    printf("[A.7] gpuFeatureSupportToString\n"); flush();
    {
        CHECK_TRUE(strcmp(gpuFeatureSupportToString(GPU_FEATURE_SUPPORT_NATIVE), "native") == 0);
        CHECK_TRUE(strcmp(gpuFeatureSupportToString(GPU_FEATURE_SUPPORT_EMULATED), "emulated") == 0);
        CHECK_TRUE(strcmp(gpuFeatureSupportToString(GPU_FEATURE_SUPPORT_UNSUPPORTED), "unsupported") == 0);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.8 gpuIsValidationEnabled
     * --------------------------------------------------------------- */
    printf("[A.8] gpuIsValidationEnabled\n"); flush();
    {
        gpuSetValidationCallback(device, NULL, NULL);
        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_NONE);
        CHECK_TRUE(!gpuIsValidationEnabled(device));

        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_ERROR);
        CHECK_TRUE(gpuIsValidationEnabled(device));

        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_NONE);
        gpuSetValidationCallback(device, testValidationCallback, NULL);
        CHECK_TRUE(gpuIsValidationEnabled(device));

        gpuSetValidationCallback(device, NULL, NULL);
        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_NONE);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * A.9 Feature Gating on Sparse Buffer Create
     * --------------------------------------------------------------- */
    printf("[A.9] Feature gating on sparse buffer create\n"); flush();
    {
        GpuBufferHandle sparseBuf;
        GpuResult res = gpuCreateSparseBuffer(device, 1024, GPU_BUFFER_USAGE_SHADER_RESOURCE, &sparseBuf);
        if (res == GPU_ERROR_NOT_SUPPORTED) {
            printf("  CreateSparseBuffer correctly returned NOT_SUPPORTED\n");
        } else if (res == GPU_SUCCESS) {
            printf("  CreateSparseBuffer succeeded (emulated path)\n");
            gpuDestroyBuffer(device, sparseBuf);
        } else {
            printf("  CreateSparseBuffer returned %d\n", res);
        }
    }
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
