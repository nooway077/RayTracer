#include "stdafx.h"
#include "CudaRenderer.h"
#include "RenderConfig.h"
#include <cuda_runtime.h>

static inline RayCuda::float3 ToCuda(const Vec3& v) {
    return RayCuda::make_float3(v.x, v.y, v.z);
}

CudaRenderer::CudaRenderer() {
    int count = 0;
    if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
        cudaSetDevice(0);
    }
    cudaStreamCreate(&m_stream);
    cudaEventCreate(&m_eventStart);
    cudaEventCreate(&m_eventStop);
}

CudaRenderer::~CudaRenderer() {
    if (m_dPixels) { cudaFree(m_dPixels); m_dPixels = nullptr; }
    if (m_eventStart) { cudaEventDestroy(m_eventStart); m_eventStart = nullptr; }
    if (m_eventStop) { cudaEventDestroy(m_eventStop);  m_eventStop = nullptr; }
    if (m_stream) { cudaStreamDestroy(m_stream);    m_stream = nullptr; }
}

void CudaRenderer::SyncSceneToHostCuda() {
    m_hostScene.sphereCount = m_scene.sphereCount;
    for (int i = 0; i < m_scene.sphereCount; ++i) {
        const Sphere& s = m_scene.spheres[i];
        m_hostScene.spheres[i].center = ToCuda(s.center);
        m_hostScene.spheres[i].radius = s.radius;
        m_hostScene.spheres[i].materialIndex = s.materialIndex;
    }

    m_hostScene.quadCount = m_scene.quadCount;
    for (int i = 0; i < m_scene.quadCount; ++i) {
        const Quad& q = m_scene.quads[i];
        m_hostScene.quads[i].Q = ToCuda(q.Q);
        m_hostScene.quads[i].u = ToCuda(q.u);
        m_hostScene.quads[i].v = ToCuda(q.v);
        m_hostScene.quads[i].normal = ToCuda(q.normal);
        m_hostScene.quads[i].d = q.d;
        m_hostScene.quads[i].materialIndex = q.materialIndex;
        m_hostScene.quads[i].w = ToCuda(q.w);
        m_hostScene.quads[i].invDotW = q.invDotW;
    }

    m_hostScene.materialCount = m_scene.materialCount;
    for (int i = 0; i < m_scene.materialCount; ++i) {
        const Material& m = m_scene.materials[i];
        m_hostScene.materials[i].type = (RayCuda::MaterialType)m.type;
        m_hostScene.materials[i].albedo = ToCuda(m.albedo);
        m_hostScene.materials[i].ior = m.ior;
        m_hostScene.materials[i].roughness = m.roughness;
        m_hostScene.materials[i].texture = (RayCuda::TextureType)m.texture;
        m_hostScene.materials[i].texScale = m.texScale;
    }

    m_hostScene.primCount = m_scene.primCount;
    for (int i = 0; i < m_scene.primCount; ++i) {
        const PrimitiveDesc& p = m_scene.primitives[i];
        m_hostScene.primitives[i].type = p.type;
        m_hostScene.primitives[i].index = p.index;
        m_hostScene.primitives[i].materialIndex = p.materialIndex;
        m_hostScene.primitives[i].aabb.min = ToCuda(p.aabb.min);
        m_hostScene.primitives[i].aabb.max = ToCuda(p.aabb.max);
        m_hostScene.primitives[i].centroid = ToCuda(p.centroid);
    }

    m_hostScene.nodeCount = m_scene.nodeCount;
    for (int i = 0; i < m_scene.nodeCount; ++i) {
        const BVHNode& n = m_scene.nodes[i];
        m_hostScene.nodes[i].aabb.min = ToCuda(n.aabb.min);
        m_hostScene.nodes[i].aabb.max = ToCuda(n.aabb.max);
        m_hostScene.nodes[i].left = n.left;
        m_hostScene.nodes[i].right = n.right;
        m_hostScene.nodes[i].primOffset = n.primOffset;
        m_hostScene.nodes[i].primCount = n.primCount;
    }

    m_hostScene.lightRadius = m_scene.lightRadius;
    m_hostScene.lightMaterialIndex = m_scene.lightMaterialIndex;
}

void CudaRenderer::Init(uint32_t renderWidth, uint32_t renderHeight) {
    float aspect = float(renderWidth) / float(renderHeight);
    m_camera.Init(
        make_vec3(RenderConfig::CamEyeX, RenderConfig::CamEyeY, RenderConfig::CamEyeZ),
        make_vec3(RenderConfig::CamLookX, RenderConfig::CamLookY, RenderConfig::CamLookZ),
        make_vec3(RenderConfig::CamUpX, RenderConfig::CamUpY, RenderConfig::CamUpZ),
        RenderConfig::CamFovDeg, aspect);

    m_scene.SetupStaticScene();
    SyncSceneToHostCuda();

    size_t need = (size_t)renderWidth * renderHeight * sizeof(uint32_t);
    if (m_dPixelsSize < need) {
        if (m_dPixels) cudaFree(m_dPixels);
        cudaMalloc(&m_dPixels, need);
        m_dPixelsSize = need;
    }
    m_init = true;
}

void CudaRenderer::RenderFrame(void* stagingPtr, uint32_t stagingRowPitch, uint64_t stagingOffset,
    uint32_t renderWidth, uint32_t renderHeight, float time)
{
    if (!m_init) Init(renderWidth, renderHeight);

    float a = time * RenderConfig::LightOrbitSpeed;
    m_hostScene.light.position = RayCuda::make_float3(
        cosf(a) * RenderConfig::LightOrbitR,
        RenderConfig::LightBaseY + sinf(time * RenderConfig::LightBobFreq) * RenderConfig::LightBobAmp,
        -0.5f + sinf(a) * RenderConfig::LightOrbitR);
    m_hostScene.light.color = RayCuda::make_float3(1.0f, 1.0f, 1.0f);
    m_hostScene.light.intensity = RenderConfig::LightIntensity;

    RayCuda::UploadSceneAsync(&m_hostScene, m_stream);

    RayCuda::CameraData cam;
    cam.origin = ToCuda(m_camera.origin);
    cam.lowerLeftCorner = ToCuda(m_camera.lowerLeftCorner);
    cam.horizontal = ToCuda(m_camera.horizontal);
    cam.vertical = ToCuda(m_camera.vertical);

    // Start GPU timing (upload + kernel).
    cudaEventRecord(m_eventStart, m_stream);

    if (m_samplesPerPixel == 1) {
        RayCuda::LaunchRenderKernel1x(m_dPixels, renderWidth, renderHeight, cam, m_stream);
    }
    else {
        RayCuda::LaunchRenderKernelNx(m_dPixels, renderWidth, renderHeight, cam, m_samplesPerPixel, m_stream);
    }

    cudaEventRecord(m_eventStop, m_stream);

    // Async copy to the D3D12 upload heap.
    uint8_t* dst = static_cast<uint8_t*>(stagingPtr) + static_cast<size_t>(stagingOffset);
    cudaMemcpy2DAsync(dst, stagingRowPitch,
        m_dPixels, renderWidth * sizeof(uint32_t),
        renderWidth * sizeof(uint32_t), renderHeight,
        cudaMemcpyDeviceToHost, m_stream);

    // Wait for everything to finish so the CPU can safely hand the buffer to D3D12.
    cudaStreamSynchronize(m_stream);

    float ms = 0.0f;
    cudaEventElapsedTime(&ms, m_eventStart, m_eventStop);
    m_lastRenderTimeMs = ms;
}

void CudaRenderer::RenderFrameSequential(void* stagingPtr, uint32_t stagingRowPitch, uint64_t stagingOffset,
    uint32_t renderWidth, uint32_t renderHeight, float time)
{
    RenderFrame(stagingPtr, stagingRowPitch, stagingOffset, renderWidth, renderHeight, time);
}
