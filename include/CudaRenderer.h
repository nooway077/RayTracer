#pragma once

#include <cstdint>
#include <memory>
#include "RayTracerCuda.cuh"
#include "Scene.h"
#include "Camera.h"

class CudaRenderer
{
    Scene  m_scene;
    Camera m_camera;
    bool   m_init = false;
    int    m_samplesPerPixel = 1;

    uint32_t* m_dPixels = nullptr;
    size_t       m_dPixelsSize = 0;
    cudaStream_t m_stream = nullptr;

    cudaEvent_t  m_eventStart = nullptr;
    cudaEvent_t  m_eventStop = nullptr;
    float        m_lastRenderTimeMs = 0.0f;

    RayCuda::CudaScene m_hostScene{};

    void Init(uint32_t renderWidth, uint32_t renderHeight);
    void SyncSceneToHostCuda();

public:
    CudaRenderer();
    ~CudaRenderer();

    void SetSamplesPerPixel(int samples) {
        m_samplesPerPixel = std::max(1, std::min(16, samples));
    }
    void ResetInit() { m_init = false; }

    float GetLastRenderTimeMs() const { return m_lastRenderTimeMs; }

    void RenderFrame(void* stagingPtr, uint32_t stagingRowPitch, uint64_t stagingOffset,
        uint32_t renderWidth, uint32_t renderHeight, float time);

    void RenderFrameSequential(void* stagingPtr, uint32_t stagingRowPitch, uint64_t stagingOffset,
        uint32_t renderWidth, uint32_t renderHeight, float time);
};
