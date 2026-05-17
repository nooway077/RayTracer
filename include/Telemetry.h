#pragma once
#include <string>
#include <fstream>
#include <mutex>

struct FrameMetrics
{
    uint64_t frameIndex = 0;
    double absoluteTimeSec = 0.0;
    const char* rendererName = "";
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t totalPixels = 0;
    float timePerPixel = 0.0;
    uint32_t samplesPerPixel = 0;
    float frameTimeMs = 0.0f;
    float renderTimeMs = 0.0f;
    float gpuUploadMs = 0.0f;
    bool vsyncEnabled = false;
    uint32_t cpuThreadPoolSize = 0;
    bool cpuPrioritizePCores = false;
};

class Telemetry
{
public:
    Telemetry();
    ~Telemetry();

    bool Start(const std::string& filename);
    void Stop();
    void LogFrame(const FrameMetrics& metrics);

    bool IsActive() const { return m_active; }

private:
    std::ofstream m_file;
    std::mutex    m_mutex;
    bool          m_active = false;
    uint64_t      m_loggedFrames = 0;
};
