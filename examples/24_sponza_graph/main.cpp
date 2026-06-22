// Demo 24: Sponza with Render Graph, Shadow Mapping, and Multi-Light Forward Rendering
//
// Features:
// - Render graph based frame orchestration (no manual barriers)
// - Shadow map from directional light (2048x2048)
// - Forward rendering with directional light + point lights + shadow mapping
// - PCF 3x3 shadow filtering
// - Distance fog
// - Per-frame graph compile/execute with automatic resource state transitions
// - Animated point lights
//
// Controls: WASD move, Q/E down/up, hold right mouse and drag to look, IJKL also look, Esc quit.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gpu/gpu.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/rendergraph/gpu_render_graph.h"

#include <slang-rhi.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace rhi;

static constexpr uint32_t kMaxPathText = 1024;
static constexpr uint32_t kMouseButtonRight = 3;
static constexpr uint32_t kShadowMapSize = 2048;
static constexpr uint32_t kMaxPointLights = 8;

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct SponzaVertex { float position[3]; float normal[3]; float uv[2]; float material; };
struct SponzaDraw { uint32_t firstIndex=0; uint32_t indexCount=0; uint32_t materialIndex=0; };
struct SponzaMaterial { std::string name; std::string baseColor; std::string normal; uint32_t baseColorIndex=UINT32_MAX; };

struct SponzaScene {
    std::vector<SponzaVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SponzaDraw> draws;
    std::vector<SponzaMaterial> materials;
    Vec3 boundsMin, boundsMax;
};

struct FlyCamera { Vec3 position={0,120,-450}; float yaw=0, pitch=0; };

struct CameraParams {
    float cameraPos[3]; float pad0;
    float right[3]; float pad1;
    float up[3]; float pad2;
    float forward[3]; float pad3;
    float proj[2]; float zParams[2];
};

struct PointLightData { float position[3]; float radius; float color[3]; float intensity; };

struct LightUniforms {
    CameraParams shadowCamera;
    float dirLightDir[3]; float dirLightIntensity;
    float dirLightColor[3]; float shadowTexelSize;
    float ambientColor[3]; uint32_t pointLightCount;
    PointLightData pointLights[kMaxPointLights];
};

struct SponzaMaterialTextures {
    ComPtr<ISampler> sampler;
    ComPtr<ITexture> baseColorArray;
    ComPtr<ITextureView> baseColorArrayView;
    uint32_t layerSize=0, layerCount=0, loadedBaseColorCount=0;
};
// ============================================================================
// Path utilities (from 21_sponza)
// ============================================================================

static int pathJoin(char* out, size_t outSize, const char* root, const char* leaf) {
    size_t rootLen = strlen(root), leafLen = strlen(leaf);
    bool needsSlash = rootLen > 0 && root[rootLen-1] != '/' && root[rootLen-1] != '\\';
    size_t totalLen = rootLen + (needsSlash?1:0) + leafLen;
    if (totalLen+1 > outSize) return 0;
    memcpy(out, root, rootLen); size_t offset = rootLen;
    if (needsSlash) out[offset++] = '/';
    memcpy(out+offset, leaf, leafLen);
    out[totalLen] = 0; return 1;
}
static bool fileExists(const char* path) { std::ifstream f(path, std::ios::binary); return f.good(); }
static std::string textureFileToken(const std::string& value) {
    std::istringstream ss(value); std::string token, last;
    while (ss >> token) last = token; return last;
}
static std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash+1);
}
static bool resolveTexturePath(const char* root, const std::string& materialPath, char* out, size_t outSize) {
    std::string token = textureFileToken(materialPath);
    std::replace(token.begin(), token.end(), '\\', '/');
    if (token.empty()) return false;
    if (pathJoin(out, outSize, root, token.c_str()) && fileExists(out)) return true;
    char texturesDir[kMaxPathText];
    if (pathJoin(texturesDir, sizeof(texturesDir), root, "textures")) {
        std::string name = baseName(token);
        if (pathJoin(out, outSize, texturesDir, name.c_str()) && fileExists(out)) return true;
    }
    return false;
}
static bool pathDirName(char* path) {
    size_t len = strlen(path);
    while (len > 0) { char c = path[len-1]; if (c=='/'||c=='\\') { path[len-1]=0; return true; } len--; }
    return false;
}
static bool getExecutableDir(char* out, size_t outSize, const char* argv0) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, out, (DWORD)outSize);
    if (len > 0 && len < outSize && pathDirName(out)) return true;
#endif
    if (argv0 && argv0[0]) { size_t argvLen = strlen(argv0); if (argvLen+1 <= outSize) { memcpy(out, argv0, argvLen+1); return pathDirName(out); } }
    return false;
}
static bool isSponzaRoot(const char* root) {
    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    return pathJoin(objPath, sizeof(objPath), root, "sponza.obj") &&
           pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl") &&
           fileExists(objPath) && fileExists(mtlPath);
}
static const char* resolveSponzaRoot(const char* explicitRoot, const char* argv0, char* resolved, size_t resolvedSize) {
    if (!explicitRoot) explicitRoot = getenv("SPONZA_ROOT");
    if (explicitRoot && isSponzaRoot(explicitRoot)) return explicitRoot;
    char exeDir[kMaxPathText], candidate[kMaxPathText];
    if (getExecutableDir(exeDir, sizeof(exeDir), argv0)) {
        if (pathJoin(candidate, sizeof(candidate), exeDir, "Sponza") && isSponzaRoot(candidate)) { snprintf(resolved, resolvedSize, "%s", candidate); return resolved; }
        if (pathJoin(candidate, sizeof(candidate), exeDir, "assets/Sponza") && isSponzaRoot(candidate)) { snprintf(resolved, resolvedSize, "%s", candidate); return resolved; }
    }
    const char* candidates[] = {".", "Sponza", "assets/Sponza", "../Sponza", "../assets/Sponza"};
    for (const char* path : candidates) { if (isSponzaRoot(path)) { snprintf(resolved, resolvedSize, "%s", path); return resolved; } }
    return nullptr;
}

// ============================================================================
// MTL/OBJ parser (from 21_sponza, condensed)
// ============================================================================

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end-start+1);
}
static uint32_t addMaterial(SponzaScene& scene, const std::string& name) {
    for (uint32_t i = 0; i < scene.materials.size(); ++i) if (scene.materials[i].name == name) return i;
    SponzaMaterial m; m.name = name; scene.materials.push_back(m); return (uint32_t)scene.materials.size()-1;
}
static bool parseMtl(const char* path, SponzaScene& scene) {
    std::ifstream file(path); if (!file) return false;
    std::string line; uint32_t cur = UINT32_MAX;
    while (std::getline(file, line)) {
        line = trim(line); if (line.empty()||line[0]=='#') continue;
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "newmtl") { std::string name; ss >> name; cur = addMaterial(scene, name); }
        else if (tag == "map_Kd" && cur != UINT32_MAX) { std::string rest; std::getline(ss, rest); scene.materials[cur].baseColor = trim(rest); }
        else if ((tag=="map_bump"||tag=="bump") && cur!=UINT32_MAX) { std::string rest; std::getline(ss, rest); scene.materials[cur].normal = trim(rest); }
    }
    return true;
}
struct ObjKey { int p,t,n; uint32_t m; bool operator==(const ObjKey& o) const { return p==o.p&&t==o.t&&n==o.n&&m==o.m; } };
struct ObjKeyHash { size_t operator()(const ObjKey& k) const { uint64_t h=1469598103934665603ull; auto mix=[&](uint32_t v){h=(h^v)*1099511628211ull;}; mix((uint32_t)k.p);mix((uint32_t)k.t);mix((uint32_t)k.n);mix(k.m); return (size_t)h; } };
static int resolveObjIndex(int idx, int count) { return idx>0?idx-1 : idx<0?count+idx : -1; }
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static Vec3 normalize(Vec3 v) { float l=sqrtf(std::max(dot(v,v),1e-12f)); return {v.x/l,v.y/l,v.z/l}; }
static bool parseObj(const char* path, SponzaScene& scene) {
    std::ifstream file(path); if (!file) return false;
    std::vector<Vec3> positions, normals; std::vector<Vec2> texcoords;
    std::unordered_map<ObjKey,uint32_t,ObjKeyHash> vmap;
    uint32_t activeMat = addMaterial(scene, "default"); SponzaDraw* activeDraw = nullptr;
    scene.boundsMin = {FLT_MAX,FLT_MAX,FLT_MAX}; scene.boundsMax = {-FLT_MAX,-FLT_MAX,-FLT_MAX};
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line); if (line.empty()||line[0]=='#') continue;
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag=="v") { Vec3 p; ss>>p.x>>p.y>>p.z; positions.push_back(p);
            scene.boundsMin.x=std::min(scene.boundsMin.x,p.x); scene.boundsMin.y=std::min(scene.boundsMin.y,p.y); scene.boundsMin.z=std::min(scene.boundsMin.z,p.z);
            scene.boundsMax.x=std::max(scene.boundsMax.x,p.x); scene.boundsMax.y=std::max(scene.boundsMax.y,p.y); scene.boundsMax.z=std::max(scene.boundsMax.z,p.z);
        } else if (tag=="vn") { Vec3 n; ss>>n.x>>n.y>>n.z; normals.push_back(normalize(n)); }
        else if (tag=="vt") { Vec2 uv; ss>>uv.x>>uv.y; texcoords.push_back(uv); }
        else if (tag=="usemtl") { std::string name; ss>>name; activeMat=addMaterial(scene,name); activeDraw=nullptr; }
        else if (tag=="f") {
            std::vector<uint32_t> poly; std::string tok;
            while (ss >> tok) {
                ObjKey key={-1,-1,-1,activeMat}; int vals[3]={0,0,0}; int vi=0; std::string cur;
                for (size_t i=0; i<=tok.size(); ++i) { char c = i<tok.size()?tok[i]:'/'; if (c=='/') { if(!cur.empty()&&vi<3) vals[vi]=atoi(cur.c_str()); cur.clear(); vi++; } else cur.push_back(c); }
                key.p=resolveObjIndex(vals[0],(int)positions.size()); key.t=resolveObjIndex(vals[1],(int)texcoords.size()); key.n=resolveObjIndex(vals[2],(int)normals.size());
                if (key.p<0||key.p>=(int)positions.size()) continue;
                auto it=vmap.find(key); uint32_t vi2;
                if (it==vmap.end()) {
                    Vec3 p=positions[key.p]; Vec3 n=key.n>=0&&key.n<(int)normals.size()?normals[key.n]:Vec3{0,1,0}; Vec2 uv=key.t>=0&&key.t<(int)texcoords.size()?texcoords[key.t]:Vec2{0,0};
                    SponzaVertex v={{p.x,p.y,p.z},{n.x,n.y,n.z},{uv.x,uv.y},(float)activeMat};
                    vi2=(uint32_t)scene.vertices.size(); scene.vertices.push_back(v); vmap.emplace(key,vi2);
                } else vi2=it->second;
                poly.push_back(vi2);
            }
            if (poly.size()>=3) {
                if (!activeDraw) { scene.draws.push_back({}); activeDraw=&scene.draws.back(); activeDraw->firstIndex=(uint32_t)scene.indices.size(); activeDraw->materialIndex=activeMat; }
                for (size_t i=1; i+1<poly.size(); ++i) { scene.indices.push_back(poly[0]); scene.indices.push_back(poly[i]); scene.indices.push_back(poly[i+1]); activeDraw->indexCount+=3; }
            }
        }
    }
    for (uint32_t i=0; i<scene.indices.size(); i+=3) {
        SponzaVertex& a=scene.vertices[scene.indices[i]]; SponzaVertex& b=scene.vertices[scene.indices[i+1]]; SponzaVertex& c=scene.vertices[scene.indices[i+2]];
        Vec3 pa={a.position[0],a.position[1],a.position[2]}, pb={b.position[0],b.position[1],b.position[2]}, pc={c.position[0],c.position[1],c.position[2]};
        Vec3 face=normalize(cross(sub(pb,pa),sub(pc,pa)));
        if (std::fabs(a.normal[0])+std::fabs(a.normal[1])+std::fabs(a.normal[2])<0.01f) {
            a.normal[0]=face.x;a.normal[1]=face.y;a.normal[2]=face.z; b.normal[0]=face.x;b.normal[1]=face.y;b.normal[2]=face.z; c.normal[0]=face.x;c.normal[1]=face.y;c.normal[2]=face.z;
        }
    }
    return !scene.vertices.empty() && !scene.indices.empty();
}
// ============================================================================
// Texture helpers (from 21_sponza)
// ============================================================================

static void resizeRgbaNearest(const uint8_t* src, int sw, int sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    for (uint32_t y=0; y<dh; ++y) { int sy=std::min((int)((uint64_t)y*sh/dh),sh-1);
        for (uint32_t x=0; x<dw; ++x) { int sx=std::min((int)((uint64_t)x*sw/dw),sw-1);
            const uint8_t* p=src+((size_t)sy*sw+sx)*4; uint8_t* q=dst+((size_t)y*dw+x)*4; q[0]=p[0];q[1]=p[1];q[2]=p[2];q[3]=p[3]; } }
}
static void fillFallbackLayer(uint8_t* dst, uint32_t w, uint32_t h, uint32_t mi) {
    uint32_t r=190+(mi*29)%50, g=190+(mi*47)%50, b=190+(mi*67)%50;
    for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x) { uint8_t* p=dst+((size_t)y*w+x)*4; p[0]=(uint8_t)r;p[1]=(uint8_t)g;p[2]=(uint8_t)b;p[3]=255; }
}
static bool createSponzaMaterialTextures(GpuDevice device, const char* root, SponzaScene& scene, SponzaMaterialTextures& out) {
    if (!device->rhiDevice->hasFeature(Feature::Bindless)) { printf("Bindless required.\n"); return false; }
    constexpr uint32_t ls=512; uint32_t lc=(uint32_t)std::max<size_t>(scene.materials.size(),1);
    std::vector<uint8_t> px((size_t)lc*ls*ls*4); uint32_t loaded=0;
    for (uint32_t i=0;i<lc;++i) {
        uint8_t* layer=px.data()+(size_t)i*ls*ls*4; fillFallbackLayer(layer,ls,ls,i);
        if (i>=scene.materials.size()||scene.materials[i].baseColor.empty()) continue;
        char tp[kMaxPathText]; if (!resolveTexturePath(root,scene.materials[i].baseColor,tp,sizeof(tp))) continue;
        int w=0,h=0,c=0; stbi_uc* img=stbi_load(tp,&w,&h,&c,4);
        if (!img||w<=0||h<=0) { if(img) stbi_image_free(img); continue; }
        resizeRgbaNearest(img,w,h,layer,ls,ls); stbi_image_free(img); scene.materials[i].baseColorIndex=i; loaded++;
    }
    SamplerDesc sd={}; sd.minFilter=TextureFilteringMode::Linear; sd.magFilter=TextureFilteringMode::Linear; sd.mipFilter=TextureFilteringMode::Linear;
    sd.addressU=TextureAddressingMode::Wrap; sd.addressV=TextureAddressingMode::Wrap; sd.addressW=TextureAddressingMode::ClampToEdge; sd.label="sponza_sampler";
    if (SLANG_FAILED(device->rhiDevice->createSampler(sd,out.sampler.writeRef()))||!out.sampler) return false;
    std::vector<SubresourceData> subs(lc); size_t lbs=(size_t)ls*ls*4;
    for (uint32_t i=0;i<lc;++i) subs[i]={px.data()+(size_t)i*lbs,ls*4,lbs};
    TextureDesc td={}; td.type=TextureType::Texture2DArray; td.size={ls,ls,1}; td.arrayLength=lc; td.mipCount=1;
    td.format=Format::RGBA8UnormSrgb; td.usage=TextureUsage::ShaderResource; td.defaultState=ResourceState::ShaderResource; td.sampler=out.sampler; td.label="sponza_texarray";
    if (SLANG_FAILED(device->rhiDevice->createTexture(td,subs.data(),out.baseColorArray.writeRef()))||!out.baseColorArray) return false;
    TextureViewDesc vd={}; vd.sampler=out.sampler;
    if (SLANG_FAILED(out.baseColorArray->createView(vd,out.baseColorArrayView.writeRef()))||!out.baseColorArrayView) return false;
    out.layerSize=ls; out.layerCount=lc; out.loadedBaseColorCount=loaded;
    printf("Textures: %u layers, %u loaded\n",lc,loaded); return true;
}

// ============================================================================
// Camera (from 21_sponza)
// ============================================================================

static float clampLocal(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
static void updateCamera(FlyCamera& cam, const bool keys[256], float dt) {
    const float ms=430, ls=1.9f;
    if (keys['j']) cam.yaw-=ls*dt; if (keys['l']) cam.yaw+=ls*dt;
    if (keys['i']) cam.pitch+=ls*dt; if (keys['k']) cam.pitch-=ls*dt;
    cam.pitch=clampLocal(cam.pitch,-1.35f,1.35f);
    float sy=sinf(cam.yaw),cy=cosf(cam.yaw); Vec3 fwd={sy,0,cy}, rgt={cy,0,-sy}; Vec3 d={};
    if(keys['w'])d={d.x+fwd.x,d.y,d.z+fwd.z}; if(keys['s'])d={d.x-fwd.x,d.y,d.z-fwd.z};
    if(keys['d'])d={d.x+rgt.x,d.y,d.z+rgt.z}; if(keys['a'])d={d.x-rgt.x,d.y,d.z-rgt.z};
    if(keys['e'])d.y+=1; if(keys['q'])d.y-=1;
    cam.position.x+=d.x*ms*dt; cam.position.y+=d.y*ms*dt; cam.position.z+=d.z*ms*dt;
}
static void rotateCameraByMouse(FlyCamera& cam, int32_t dx, int32_t dy) {
    cam.yaw+=dx*0.0045f; cam.pitch-=dy*0.0045f; cam.pitch=clampLocal(cam.pitch,-1.35f,1.35f);
}
static CameraParams makeCameraParams(const FlyCamera& cam, float aspect) {
    float sy=sinf(cam.yaw),cy=cosf(cam.yaw),sp=sinf(cam.pitch),cp=cosf(cam.pitch);
    Vec3 fwd=normalize({sy*cp,sp,cy*cp}), rgt=normalize({cy,0,-sy}), up=normalize(cross(fwd,rgt));
    float f=1.0f/tanf(60.0f*0.5f*3.14159265f/180.0f);
    CameraParams p={};
    p.cameraPos[0]=cam.position.x;p.cameraPos[1]=cam.position.y;p.cameraPos[2]=cam.position.z;
    p.right[0]=rgt.x;p.right[1]=rgt.y;p.right[2]=rgt.z; p.up[0]=up.x;p.up[1]=up.y;p.up[2]=up.z;
    p.forward[0]=fwd.x;p.forward[1]=fwd.y;p.forward[2]=fwd.z; p.proj[0]=f/std::max(aspect,0.01f); p.proj[1]=f;
    p.zParams[0]=3.0f; p.zParams[1]=6000.0f; return p;
}
static CameraParams makeCameraLookAt(Vec3 pos, Vec3 target, float fov, float aspect, float nz, float fz) {
    Vec3 fwd=normalize(sub(target,pos)), rgt=normalize(cross(fwd,{0,1,0})), up=normalize(cross(fwd,rgt));
    float f=1.0f/tanf(fov*0.5f*3.14159265f/180.0f);
    CameraParams p={};
    p.cameraPos[0]=pos.x;p.cameraPos[1]=pos.y;p.cameraPos[2]=pos.z;
    p.right[0]=rgt.x;p.right[1]=rgt.y;p.right[2]=rgt.z; p.up[0]=up.x;p.up[1]=up.y;p.up[2]=up.z;
    p.forward[0]=fwd.x;p.forward[1]=fwd.y;p.forward[2]=fwd.z; p.proj[0]=f/std::max(aspect,0.01f); p.proj[1]=f;
    p.zParams[0]=nz; p.zParams[1]=fz; return p;
}

// ============================================================================
// Render graph pass callbacks
// ============================================================================

struct FrameRenderData {
    GpuDevice device; SponzaScene* scene;
    ComPtr<IBuffer> vertexBuffer, indexBuffer;
    ComPtr<IRenderPipeline> forwardPipeline, shadowPipeline;
    ComPtr<IShaderObject> forwardRootObject, shadowRootObject;
    CameraParams cameraParams; LightUniforms lightParams;
    uint32_t surfaceWidth, surfaceHeight;
};

static void shadowPassCallback(GpuGraphPassContext* ctx, void* ud) {
    FrameRenderData* d=(FrameRenderData*)ud;
    if (!ctx->renderPass || !d->shadowPipeline) return;
    ctx->renderPass->rhiPassEncoder->bindPipeline(d->shadowPipeline, d->shadowRootObject);
    RenderState st={};
    st.viewports[0]=Viewport::fromSize((float)kShadowMapSize,(float)kShadowMapSize); st.viewportCount=1;
    st.scissorRects[0]=ScissorRect::fromSize(kShadowMapSize,kShadowMapSize); st.scissorRectCount=1;
    st.vertexBuffers[0]=d->vertexBuffer; st.vertexBufferCount=1;
    st.indexBuffer=d->indexBuffer; st.indexFormat=IndexFormat::Uint32;
    ctx->renderPass->rhiPassEncoder->setRenderState(st);
    for (const SponzaDraw& draw : d->scene->draws) {
        DrawArguments a={}; a.vertexCount=draw.indexCount; a.instanceCount=1; a.startIndexLocation=draw.firstIndex;
        ctx->renderPass->rhiPassEncoder->drawIndexed(a);
    }
}

static void forwardPassCallback(GpuGraphPassContext* ctx, void* ud) {
    FrameRenderData* d=(FrameRenderData*)ud;
    if (!ctx->renderPass || !d->forwardPipeline) return;
    { ShaderCursor c(d->forwardRootObject); c["camera"].setData(d->cameraParams); c["lights"].setData(d->lightParams); }
    ctx->renderPass->rhiPassEncoder->bindPipeline(d->forwardPipeline, d->forwardRootObject);
    RenderState st={};
    st.viewports[0]=Viewport::fromSize((float)d->surfaceWidth,(float)d->surfaceHeight); st.viewportCount=1;
    st.scissorRects[0]=ScissorRect::fromSize(d->surfaceWidth,d->surfaceHeight); st.scissorRectCount=1;
    st.vertexBuffers[0]=d->vertexBuffer; st.vertexBufferCount=1;
    st.indexBuffer=d->indexBuffer; st.indexFormat=IndexFormat::Uint32;
    ctx->renderPass->rhiPassEncoder->setRenderState(st);
    for (const SponzaDraw& draw : d->scene->draws) {
        DrawArguments a={}; a.vertexCount=draw.indexCount; a.instanceCount=1; a.startIndexLocation=draw.firstIndex;
        ctx->renderPass->rhiPassEncoder->drawIndexed(a);
    }
}
// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* requestedRoot = nullptr; uint32_t maxFrames = 0;
    for (int i=1; i<argc; ++i) {
        if (strcmp(argv[i],"--frames")==0 && i+1<argc) maxFrames=(uint32_t)std::max(0,std::atoi(argv[++i]));
        else if (strcmp(argv[i],"--root")==0 && i+1<argc) requestedRoot=argv[++i];
        else if (argv[i][0]!='-') requestedRoot=argv[i];
    }
    char rootBuf[kMaxPathText];
    const char* root = resolveSponzaRoot(requestedRoot, argv[0], rootBuf, sizeof(rootBuf));
    if (!root) { printf("Usage: 24_sponza_graph <path-to-Sponza>\n"); return 0; }

    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    pathJoin(objPath,sizeof(objPath),root,"sponza.obj"); pathJoin(mtlPath,sizeof(mtlPath),root,"sponza.mtl");
    SponzaScene scene;
    if (!parseMtl(mtlPath,scene) || !parseObj(objPath,scene)) { printf("Parse failed\n"); return 1; }
    printf("Scene: %zu verts, %zu indices, %zu mats, %zu draws\n", scene.vertices.size(), scene.indices.size(), scene.materials.size(), scene.draws.size());

    gpuPlatformInit();
    GpuWindow window=nullptr; GpuWindowDesc wd={"24_sponza_graph",1280,720,false,true,true};
    gpuCreateWindow(&wd,&window);
    GpuDevice device=nullptr; GpuDeviceDesc dd={"24_sponza_graph",true,GPU_BACKEND_DEFAULT};
    if (gpuCreateDevice(&dd,&device)!=GPU_SUCCESS) { printf("Device failed\n"); return 1; }
    GpuSurface surface=nullptr;
    gpuCreateSurface(device,window,GPU_SURFACE_TYPE_VULKAN,&surface);
    GpuFormat fmt=gpuSurfaceGetPreferredFormat(surface);
    gpuSurfaceConfigure(surface,1280,720,fmt,true);

    SponzaMaterialTextures matTex;
    if (!createSponzaMaterialTextures(device,root,scene,matTex)) { printf("Texture creation failed\n"); return 1; }

    BufferDesc vbd={}; vbd.size=scene.vertices.size()*sizeof(SponzaVertex); vbd.usage=BufferUsage::VertexBuffer|BufferUsage::CopyDestination; vbd.defaultState=ResourceState::VertexBuffer; vbd.label="sponza_vb";
    ComPtr<IBuffer> vb=device->rhiDevice->createBuffer(vbd,scene.vertices.data());
    BufferDesc ibd={}; ibd.size=scene.indices.size()*sizeof(uint32_t); ibd.usage=BufferUsage::IndexBuffer|BufferUsage::CopyDestination; ibd.defaultState=ResourceState::IndexBuffer; ibd.label="sponza_ib";
    ComPtr<IBuffer> ib=device->rhiDevice->createBuffer(ibd,scene.indices.data());
    if (!vb||!ib) { printf("Buffer creation failed\n"); return 1; }

    InputElementDesc elems[]={{"POSITION",0,Format::RGB32Float,offsetof(SponzaVertex,position),0},{"NORMAL",0,Format::RGB32Float,offsetof(SponzaVertex,normal),0},{"TEXCOORD",0,Format::RG32Float,offsetof(SponzaVertex,uv),0},{"MATERIAL",0,Format::R32Float,offsetof(SponzaVertex,material),0}};
    VertexStreamDesc stream={sizeof(SponzaVertex),InputSlotClass::PerVertex,0};
    InputLayoutDesc ild={}; ild.inputElements=elems; ild.inputElementCount=(uint32_t)std::size(elems); ild.vertexStreams=&stream; ild.vertexStreamCount=1;
    ComPtr<IInputLayout> inputLayout=device->rhiDevice->createInputLayout(ild);

    GpuShaderCompiler compiler=nullptr; gpuCreateShaderCompiler(device,&compiler);
    GpuShaderCompileDesc scd={}; scd.sourcePath="shadow.slang"; scd.entryPoint="shadowVertexMain"; scd.target=GPU_SHADER_TARGET_SPIRV;
    GpuShaderProgram shadowProg=nullptr;
    if (gpuCompileShader(compiler,&scd,&shadowProg)!=GPU_SUCCESS) { printf("Shadow shader failed: %s\n",gpuGetShaderCompileDiagnostic(compiler)); return 1; }
    GpuShaderCompileDesc fcd={}; fcd.sourcePath="forward.slang"; fcd.entryPoint="vertexMain"; fcd.fragmentEntryPoint="fragmentMain"; fcd.target=GPU_SHADER_TARGET_SPIRV;
    GpuShaderProgram forwardProg=nullptr;
    if (gpuCompileShader(compiler,&fcd,&forwardProg)!=GPU_SUCCESS) { printf("Forward shader failed: %s\n",gpuGetShaderCompileDiagnostic(compiler)); return 1; }

    RenderPipelineDesc spd={}; spd.program=shadowProg->rhiProgram; spd.inputLayout=inputLayout; spd.targetCount=0; spd.targets=nullptr;
    spd.depthStencil.format=Format::D32Float; spd.depthStencil.depthTestEnable=true; spd.depthStencil.depthWriteEnable=true; spd.depthStencil.depthFunc=ComparisonFunc::Less;
    spd.rasterizer.cullMode=CullMode::None; spd.rasterizer.fillMode=FillMode::Solid; spd.label="shadow_pipeline";
    ComPtr<IRenderPipeline> shadowPipe;
    if (SLANG_FAILED(device->rhiDevice->createRenderPipeline(spd,shadowPipe.writeRef()))||!shadowPipe) { printf("Shadow pipeline failed\n"); return 1; }

    ColorTargetDesc tgt={}; tgt.format=gpuFormatToRhi(fmt);
    RenderPipelineDesc fpd={}; fpd.program=forwardProg->rhiProgram; fpd.inputLayout=inputLayout; fpd.targets=&tgt; fpd.targetCount=1;
    fpd.depthStencil.format=Format::D32Float; fpd.depthStencil.depthTestEnable=true; fpd.depthStencil.depthWriteEnable=true; fpd.depthStencil.depthFunc=ComparisonFunc::Less;
    fpd.rasterizer.cullMode=CullMode::None; fpd.rasterizer.fillMode=FillMode::Solid; fpd.label="forward_pipeline";
    ComPtr<IRenderPipeline> forwardPipe;
    if (SLANG_FAILED(device->rhiDevice->createRenderPipeline(fpd,forwardPipe.writeRef()))||!forwardPipe) { printf("Forward pipeline failed\n"); return 1; }

    ComPtr<IShaderObject> fwdRoot, shadowRoot;
    device->rhiDevice->createRootShaderObject(forwardProg->rhiProgram,fwdRoot.writeRef());
    device->rhiDevice->createRootShaderObject(shadowProg->rhiProgram,shadowRoot.writeRef());
    if (!fwdRoot||!shadowRoot) { printf("Root object failed\n"); return 1; }
    { ShaderCursor c(fwdRoot); c["baseColorArray"].setBinding(matTex.baseColorArrayView); c["linearSampler"].setBinding(matTex.sampler); }

    uint32_t surfW=1280, surfH=720;
    GpuTextureHandle depthHandle={0,0}, shadowHandle={0,0}, shadowSrvHandle={0,0};

    auto recreateDepth=[&]() {
        if (depthHandle.index) gpuDestroyTexture(device, depthHandle);
        GpuTextureDesc dd={};
        dd.type=GPU_TEXTURE_TYPE_2D; dd.width=surfW; dd.height=surfH; dd.depth=1;
        dd.arrayLength=1; dd.mipCount=1; dd.format=GPU_FORMAT_D32_FLOAT; dd.sampleCount=1;
        dd.usage=GPU_TEXTURE_USAGE_DEPTH_STENCIL; dd.label="forward_depth";
        gpuCreateTexture(device,&dd,&depthHandle);
    };
    recreateDepth();

    {
        GpuTextureDesc sd={};
        sd.type=GPU_TEXTURE_TYPE_2D; sd.width=kShadowMapSize; sd.height=kShadowMapSize; sd.depth=1;
        sd.arrayLength=1; sd.mipCount=1; sd.format=GPU_FORMAT_D32_FLOAT; sd.sampleCount=1;
        sd.usage=GPU_TEXTURE_USAGE_DEPTH_STENCIL|GPU_TEXTURE_USAGE_SHADER_RESOURCE; sd.label="shadow_map";
        if (gpuCreateTexture(device,&sd,&shadowHandle)!=GPU_SUCCESS) { printf("Shadow map failed\n"); return 1; }
        gpuCreateTextureView(device,shadowHandle,GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE,&shadowSrvHandle);
    }

    // Shadow comparison sampler
    GpuSamplerDesc ssd={};
    ssd.minFilter=GPU_FILTER_LINEAR; ssd.magFilter=GPU_FILTER_LINEAR; ssd.mipFilter=GPU_FILTER_NEAREST;
    ssd.addressModeU=GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; ssd.addressModeV=GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; ssd.addressModeW=GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ssd.compareEnable=true; ssd.compareOp=1; ssd.minLod=0.0f; ssd.maxLod=1000.0f; ssd.maxAnisotropy=1; ssd.label="shadow_cmp_sampler";
    GpuSamplerHandle shadowSamplerHandle={0,0};
    gpuCreateSampler(device,&ssd,&shadowSamplerHandle);

    // Bind shadow map SRV and sampler to forward root object
    {
        rhi::ITextureView* srv=device->textureViewPool.resolve(shadowSrvHandle.index,shadowSrvHandle.generation);
        rhi::ISampler* samp=device->samplerPool.resolve(shadowSamplerHandle.index,shadowSamplerHandle.generation);
        if (srv && samp) { ShaderCursor c(fwdRoot); c["shadowMap"].setBinding(srv); c["shadowSampler"].setBinding(samp); }
    }

    FlyCamera cam; Vec3 center={(scene.boundsMin.x+scene.boundsMax.x)*0.5f,(scene.boundsMin.y+scene.boundsMax.y)*0.5f,(scene.boundsMin.z+scene.boundsMax.z)*0.5f};
    cam.position={center.x,center.y+180,scene.boundsMin.z-450};

    LightUniforms lp={};
    lp.dirLightDir[0]=0.45f; lp.dirLightDir[1]=-0.75f; lp.dirLightDir[2]=-0.35f;
    lp.dirLightColor[0]=1.0f; lp.dirLightColor[1]=0.95f; lp.dirLightColor[2]=0.85f; lp.dirLightIntensity=2.5f;
    lp.ambientColor[0]=0.18f; lp.ambientColor[1]=0.20f; lp.ambientColor[2]=0.25f;
    lp.shadowTexelSize=1.0f/(float)kShadowMapSize; lp.pointLightCount=4;
    PointLightData pls[4]={ {{center.x-200,center.y+50,center.z},400,{1,0.3f,0.2f},3}, {{center.x+200,center.y+50,center.z},400,{0.2f,0.4f,1},3}, {{center.x,center.y+300,center.z+200},500,{0.8f,0.8f,0.3f},2}, {{center.x,center.y-50,center.z-200},300,{0.5f,1,0.5f},2.5f} };
    for (int i=0;i<4;i++) lp.pointLights[i]=pls[i];
    Vec3 shCamPos={center.x-400,center.y+600,center.z+300};
    lp.shadowCamera=makeCameraLookAt(shCamPos,center,90,1,10,4000);

    FrameRenderData rd={}; rd.device=device; rd.scene=&scene; rd.vertexBuffer=vb; rd.indexBuffer=ib;
    rd.forwardPipeline=forwardPipe; rd.shadowPipeline=shadowPipe; rd.forwardRootObject=fwdRoot; rd.shadowRootObject=shadowRoot;
    rd.surfaceWidth=surfW; rd.surfaceHeight=surfH;

    bool keys[256]={}; bool rmd=false; bool quit=false; uint32_t frame=0;
    auto last=std::chrono::steady_clock::now(); auto lastPrint=last;
    printf("WASD move, Q/E up/down, right-drag look, IJKL look, Esc quit.\n");
    printf("Shadow: %ux%u, Point lights: %u\n", kShadowMapSize, kShadowMapSize, lp.pointLightCount);

    while (!quit) {
        auto now=std::chrono::steady_clock::now();
        float dt=std::chrono::duration<float>(now-last).count(); last=now;
        if (dt<=0||dt>0.1f) dt=1.0f/60.0f;

        GpuPlatformEvent ev;
        while (gpuPollEvent(&ev)) {
            if (ev.type==GPU_PLATFORM_EVENT_QUIT) quit=true;
            if (ev.type==GPU_PLATFORM_EVENT_RESIZE) { surfW=std::max(ev.resize.width,1u); surfH=std::max(ev.resize.height,1u); gpuSurfaceConfigure(surface,surfW,surfH,fmt,true); recreateDepth(); }
            if (ev.type==GPU_PLATFORM_EVENT_KEY_DOWN||ev.type==GPU_PLATFORM_EVENT_KEY_UP) { uint32_t k=ev.key.keycode; bool dn=ev.type==GPU_PLATFORM_EVENT_KEY_DOWN; if(k>='A'&&k<='Z')k+='a'-'A'; if(k<256)keys[k]=dn; if(dn&&k==27)quit=true; }
            if (ev.type==GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN||ev.type==GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP) { if(ev.mouse.button==kMouseButtonRight) rmd=ev.type==GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN; }
            if (ev.type==GPU_PLATFORM_EVENT_MOUSE_MOVE&&rmd) rotateCameraByMouse(cam,ev.mouse.dx,ev.mouse.dy);
        }
        updateCamera(cam,keys,dt);

        float t=(float)frame*0.02f;
        lp.pointLights[0].position[0]=center.x+sinf(t)*300; lp.pointLights[0].position[2]=center.z+cosf(t)*300;
        lp.pointLights[1].position[0]=center.x+sinf(t+3.14f)*300; lp.pointLights[1].position[2]=center.z+cosf(t+3.14f)*300;
        lp.pointLights[2].position[1]=center.y+200+sinf(t*0.7f)*100;
        lp.pointLights[3].position[0]=center.x+cosf(t*1.3f)*250; lp.pointLights[3].position[2]=center.z+sinf(t*1.3f)*250;

        rd.cameraParams=makeCameraParams(cam,(float)surfW/surfH); rd.lightParams=lp; rd.surfaceWidth=surfW; rd.surfaceHeight=surfH;

        GpuSurfaceTexture bb=nullptr;
        if (gpuSurfaceAcquireNextImage(surface,&bb)!=GPU_SUCCESS) continue;

        GpuGraph graph=nullptr;
        if (gpuGraphCreate(device,&graph)!=GPU_SUCCESS) { gpuSurfaceTextureRelease(bb); continue; }

        GpuGraphResource bbRes=gpuGraphImportSurfaceTexture(graph,bb,"backbuffer");
        GpuGraphResource depthRes=gpuGraphImportTexture(graph,depthHandle,GPU_RESOURCE_STATE_DEPTH_WRITE,"depth");
        GpuGraphResource shadowRes=gpuGraphImportTexture(graph,shadowHandle,GPU_RESOURCE_STATE_DEPTH_WRITE,"shadow_map");

        // Shadow pass
        GpuGraphPass sp=gpuGraphAddRenderPass(graph,"shadow_pass");
        GpuGraphDepthAttachment sda={}; sda.resource=shadowRes; sda.loadOp=GPU_LOAD_OP_CLEAR; sda.storeOp=GPU_STORE_OP_STORE; sda.clearDepth=1.0f;
        gpuGraphPassSetDepthAttachment(sp,&sda);
        gpuGraphPassSetCallback(sp,shadowPassCallback,&rd);

        // Forward pass
        GpuGraphPass fp=gpuGraphAddRenderPass(graph,"forward_pass");
        GpuGraphColorAttachment ca={}; ca.resource=bbRes; ca.loadOp=GPU_LOAD_OP_CLEAR; ca.storeOp=GPU_STORE_OP_STORE;
        ca.clearColor[0]=0.025f; ca.clearColor[1]=0.030f; ca.clearColor[2]=0.040f; ca.clearColor[3]=1.0f;
        gpuGraphPassSetColorAttachments(fp,1,&ca);
        GpuGraphDepthAttachment da={}; da.resource=depthRes; da.loadOp=GPU_LOAD_OP_CLEAR; da.storeOp=GPU_STORE_OP_STORE; da.clearDepth=1.0f;
        gpuGraphPassSetDepthAttachment(fp,&da);
        gpuGraphPassRead(fp,shadowRes);
        gpuGraphPassSetCallback(fp,forwardPassCallback,&rd);

        if (gpuGraphCompile(graph)!=GPU_SUCCESS) { printf("Graph compile failed\n"); gpuGraphDestroy(graph); gpuSurfaceTextureRelease(bb); continue; }
        GpuCommandQueue queue=nullptr; gpuGetQueue(device,GPU_QUEUE_TYPE_GRAPHICS,&queue);
        gpuGraphExecute(graph,queue);
        gpuGraphDestroy(graph);
        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(bb);
        frame++; if (maxFrames>0&&frame>=maxFrames) quit=true;

        if (std::chrono::duration<float>(now-lastPrint).count()>1.0f) {
            printf("pos=(%.0f,%.0f,%.0f) yaw=%.2f draws=%zu frame=%u\r",cam.position.x,cam.position.y,cam.position.z,cam.yaw,scene.draws.size(),frame);
            fflush(stdout); lastPrint=now;
        }
    }

    device->graphicsQueue->waitOnHost();
    if (depthHandle.index) gpuDestroyTexture(device, depthHandle); if (shadowHandle.index) gpuDestroyTexture(device, shadowHandle); if (shadowSrvHandle.index) gpuDestroyTextureView(device, shadowSrvHandle); if (shadowSamplerHandle.index) gpuDestroySampler(device, shadowSamplerHandle); gpuDestroyShaderProgram(forwardProg); gpuDestroyShaderProgram(shadowProg); gpuDestroyShaderCompiler(compiler);
    gpuSurfaceUnconfigure(surface); gpuDestroySurface(device,surface);
    gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
    printf("\nRendered %u frames.\n",frame);
    return 0;
}





