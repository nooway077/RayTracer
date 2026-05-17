#include "stdafx.h"
#include "Telemetry.h"

Telemetry::Telemetry() = default;

Telemetry::~Telemetry()
{
    Stop();
}

bool Telemetry::Start(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_active) return true;

    m_file.open(filename, std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) return false;

    m_active = true;
    m_loggedFrames = 0;

    // UTF-8 BOM for Excel to open the CSV correctly.
    m_file << "\xEF\xBB\xBF";
    m_file << "FrameIndex,AbsoluteTimeSec,Renderer,RenderWidth,RenderHeight,TotalPixels,SamplesPerPixel,TimePerPixel,FrameTimeMs,RenderTimeMs,GpuUploadMs,VSync,CPUThreadPoolSize,PrioritizePCores\n";
    m_file.flush();
    return true;
}

void Telemetry::Stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) return;
    m_file.flush();
    m_file.close();
    m_active = false;
}

void Telemetry::LogFrame(const FrameMetrics& metrics)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) return;

    m_file << metrics.frameIndex << ','
        << metrics.absoluteTimeSec << ','
        << metrics.rendererName << ','
        << metrics.renderWidth << ','
        << metrics.renderHeight << ','
        << metrics.totalPixels << ','
        << metrics.samplesPerPixel << ','
        << metrics.timePerPixel << ','
        << metrics.frameTimeMs << ','
        << metrics.renderTimeMs << ','
        << metrics.gpuUploadMs << ','
        << (metrics.vsyncEnabled ? 1 : 0) << ',' 
        << metrics.cpuThreadPoolSize << ','
        << metrics.cpuPrioritizePCores << '\n';

    ++m_loggedFrames;
    if ((m_loggedFrames % 60) == 0)   // Flush every 60 frame.
        m_file.flush();
}
