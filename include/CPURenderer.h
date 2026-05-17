#pragma once

#include "Scene.h"
#include "Camera.h"
#include <tbb/task_arena.h>
#include <tbb/task_scheduler_observer.h>

class CPURenderer
{
    struct PCoreObserver;
    Scene  m_scene;
    Camera m_camera;
    bool   m_init = false;

    int  m_threadPoolSize = 0;
    bool m_prioritizePCores = false;
    bool m_configDirty = true;
    int  m_samplesPerPixel = 1;

    std::unique_ptr<tbb::task_arena> m_arena;
    PCoreObserver* m_observer = nullptr;
    float m_lastRenderTimeMs = 0.0f;

    void RecreateArena();
    void RenderInternal(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
        UINT renderWidth, UINT renderHeight, bool multithreaded, float time);

public:
    CPURenderer();
    ~CPURenderer();

    void Configure(int threadPoolSize, bool prioritizePCores);
    void ResetInit() { m_init = false; }
    void SetSamplesPerPixel(int samples) {
        m_samplesPerPixel = std::max(1, std::min(16, samples));
    }

    float GetLastRenderTimeMs() const { return m_lastRenderTimeMs; }

    void RenderFrame(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
        UINT renderWidth, UINT renderHeight, float frameIndex);
    void RenderFrameSequential(void* stagingPtr, UINT stagingRowPitch, UINT64 stagingOffset,
        UINT renderWidth, UINT renderHeight, float frameIndex);
};
