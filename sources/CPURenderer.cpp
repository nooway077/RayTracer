#include "stdafx.h"
#include "CPURenderer.h"
#include "RayTracerCore.h"
#include "RenderConfig.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range2d.h>

static inline uint32_t PackRGBA8(const Vec3& c)
{
    float r = sqrtf(clamp(c.x, 0.0f, 1.0f));
    float g = sqrtf(clamp(c.y, 0.0f, 1.0f));
    float b = sqrtf(clamp(c.z, 0.0f, 1.0f));
    uint32_t ir = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t ig = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t ib = (uint32_t)(b * 255.0f + 0.5f);
    return (0xFF000000u) | (ib << 16) | (ig << 8) | ir;
}

struct CPURenderer::PCoreObserver : public tbb::task_scheduler_observer
{
    bool m_enable;
    PCoreObserver(tbb::task_arena& arena, bool enable)
        : tbb::task_scheduler_observer(arena), m_enable(enable) {}

    void on_scheduler_entry(bool /* is_worker */) override
    {
        if (!m_enable) return;
#if defined(_WIN32)
        HANDLE hThread = GetCurrentThread();
        THREAD_POWER_THROTTLING_STATE pts{};
        pts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        pts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        pts.StateMask = 0;
        SetThreadInformation(hThread, ThreadPowerThrottling, &pts, sizeof(pts));
        SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
#endif
    }
};

CPURenderer::CPURenderer() = default;
CPURenderer::~CPURenderer()
{
    if (m_observer) {
        m_observer->observe(false);
        delete m_observer;
        m_observer = nullptr;
    }
}

void CPURenderer::Configure(int threadPoolSize, bool prioritizePCores)
{
    if (m_threadPoolSize != threadPoolSize || m_prioritizePCores != prioritizePCores) {
        m_threadPoolSize = threadPoolSize;
        m_prioritizePCores = prioritizePCores;
        m_configDirty = true;
    }
}

void CPURenderer::RecreateArena()
{
    if (m_observer) {
        m_observer->observe(false);
        delete m_observer;
        m_observer = nullptr;
    }
    m_arena.reset();

    if (m_threadPoolSize > 0)
        m_arena = std::make_unique<tbb::task_arena>(m_threadPoolSize);
    else
        m_arena = std::make_unique<tbb::task_arena>();

    m_observer = new PCoreObserver(*m_arena, m_prioritizePCores);
    m_observer->observe(true);
}

static void RenderTile1x(uint8_t* base, const Camera& cam, const Scene& scene,
    float su, float sv, UINT stagingRowPitch,
    unsigned int y0, unsigned int y1,
    unsigned int x0, unsigned int x1,
    unsigned int renderWidth, unsigned int renderHeight)
{
    for (unsigned int y = y0; y < y1; y += 2) {
        unsigned int y2 = (y + 1 < renderHeight) ? (y + 1) : y;
        for (unsigned int x = x0; x < x1; x += 2) {
            unsigned int x2 = (x + 1 < renderWidth) ? (x + 1) : x;

            Ray rays[4];
            cam.GetRay((x + 0.5f) * su, (y + 0.5f) * sv, rays[0]);
            cam.GetRay((x2 + 0.5f) * su, (y + 0.5f) * sv, rays[1]);
            cam.GetRay((x + 0.5f) * su, (y2 + 0.5f) * sv, rays[2]);
            cam.GetRay((x2 + 0.5f) * su, (y2 + 0.5f) * sv, rays[3]);
            for (int i = 0; i < 4; ++i) rays[i].tmax = RenderConfig::RayTMax;

            Vec3 cols[4];
            TracePacket4(rays, scene, cols);

            uint32_t* row0 = reinterpret_cast<uint32_t*>(base + y * stagingRowPitch);
            uint32_t* row1 = reinterpret_cast<uint32_t*>(base + y2 * stagingRowPitch);
            row0[x] = PackRGBA8(cols[0]);
            row0[x2] = PackRGBA8(cols[1]);
            row1[x] = PackRGBA8(cols[2]);
            row1[x2] = PackRGBA8(cols[3]);
        }
    }
}

static void RenderTileNx(uint8_t* base, const Camera& cam, const Scene& scene,
    float su, float sv, UINT stagingRowPitch,
    unsigned int y0, unsigned int y1,
    unsigned int x0, unsigned int x1,
    int spp)
{
    int n = (int)ceilf(sqrtf((float)spp));
    float invN = 1.0f / n;

    for (unsigned int y = y0; y < y1; ++y) {
        for (unsigned int x = x0; x < x1; ++x) {
            Vec3 color = make_vec3(0.0f, 0.0f, 0.0f);
            int si = 0;

            for (; si + 4 <= spp; si += 4) {
                Ray rays[4];
                for (int k = 0; k < 4; ++k) {
                    int sampleIdx = si + k;
                    int ix = sampleIdx % n;
                    int iy = sampleIdx / n;
                    float ox = (ix + 0.5f) * invN;
                    float oy = (iy + 0.5f) * invN;
                    cam.GetRay((x + ox) * su, (y + oy) * sv, rays[k]);
                    rays[k].tmax = RenderConfig::RayTMax;
                }
                Vec3 cols[4];
                TracePacket4(rays, scene, cols);
                for (int k = 0; k < 4; ++k) color += cols[k];
            }

            for (; si < spp; ++si) {
                int ix = si % n;
                int iy = si / n;
                float ox = (ix + 0.5f) * invN;
                float oy = (iy + 0.5f) * invN;
                Ray r;
                cam.GetRay((x + ox) * su, (y + oy) * sv, r);
                r.tmax = RenderConfig::RayTMax;
                color += TracePrimaryRay(r, scene);
            }

            color = color * (1.0f / spp);
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * stagingRowPitch);
            row[x] = PackRGBA8(color);
        }
    }
}

void CPURenderer::RenderInternal(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
    UINT renderWidth, UINT renderHeight, bool multithreaded,
    float time)
{
    if (!m_init) {
        float aspect = float(renderWidth) / float(renderHeight);
        m_camera.Init(make_vec3(RenderConfig::CamEyeX, RenderConfig::CamEyeY, RenderConfig::CamEyeZ),
            make_vec3(RenderConfig::CamLookX, RenderConfig::CamLookY, RenderConfig::CamLookZ),
            make_vec3(RenderConfig::CamUpX, RenderConfig::CamUpY, RenderConfig::CamUpZ),
            RenderConfig::CamFovDeg, aspect);
        m_scene.SetupStaticScene();
        m_init = true;
    }

    float a = time * RenderConfig::LightOrbitSpeed;
    m_scene.light.position = make_vec3(
        cosf(a) * RenderConfig::LightOrbitR,
        RenderConfig::LightBaseY + sinf(time * RenderConfig::LightBobFreq) * RenderConfig::LightBobAmp,
        -0.5f + sinf(a) * RenderConfig::LightOrbitR);
    m_scene.light.color = make_vec3(1.0f, 1.0f, 1.0f);
    m_scene.light.intensity = RenderConfig::LightIntensity;

    uint8_t* base = static_cast<uint8_t*>(stagingPtr) + static_cast<size_t>(stagingOffset);
    float su = 1.0f / float(renderWidth);
    float sv = 1.0f / float(renderHeight);
    const unsigned int tileSize = RenderConfig::CPUTileSize;

    auto start = std::chrono::high_resolution_clock::now();

    if (m_samplesPerPixel == 1) {
        auto TileFn = [&](const tbb::blocked_range2d<unsigned int>& r) {
            RenderTile1x(base, m_camera, m_scene, su, sv, stagingRowPitch,
                r.rows().begin(), r.rows().end(),
                r.cols().begin(), r.cols().end(),
                renderWidth, renderHeight);
            };
        if (multithreaded) {
            if (m_configDirty) { RecreateArena(); m_configDirty = false; }
            m_arena->execute([&] {
                tbb::parallel_for(
                    tbb::blocked_range2d<unsigned int>(0, renderHeight, tileSize,
                        0, renderWidth, tileSize),
                    TileFn);
                });
        }
        else {
            tbb::blocked_range2d<unsigned int> full(0, renderHeight, renderHeight, 0, renderWidth, renderWidth);
            TileFn(full);
        }
    }
    else {
        auto TileFn = [&](const tbb::blocked_range2d<unsigned int>& r) {
            RenderTileNx(base, m_camera, m_scene, su, sv, stagingRowPitch,
                r.rows().begin(), r.rows().end(),
                r.cols().begin(), r.cols().end(),
                m_samplesPerPixel);
            };
        if (multithreaded) {
            if (m_configDirty) { RecreateArena(); m_configDirty = false; }
            m_arena->execute([&] {
                tbb::parallel_for(
                    tbb::blocked_range2d<unsigned int>(0, renderHeight, tileSize,
                        0, renderWidth, tileSize),
                    TileFn);
                });
        }
        else {
            tbb::blocked_range2d<unsigned int> full(0, renderHeight, renderHeight, 0, renderWidth, renderWidth);
            TileFn(full);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    m_lastRenderTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
}

void CPURenderer::RenderFrame(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
    UINT renderWidth, UINT renderHeight, float time)
{
    RenderInternal(stagingPtr, stagingRowPitch, stagingOffset,
        renderWidth, renderHeight, true, time);
}

void CPURenderer::RenderFrameSequential(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
    UINT renderWidth, UINT renderHeight, float time)
{
    RenderInternal(stagingPtr, stagingRowPitch, stagingOffset,
        renderWidth, renderHeight, false, time);
}
