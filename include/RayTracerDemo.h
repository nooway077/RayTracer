#pragma once

#include "DXApp.h"
#include "StepTimer.h"
#include "GPUTimer.h"
#include "CPURenderer.h"
#include "CudaRenderer.h"
#include "Telemetry.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class UILayer;

class RayTracerDemo : public DXApp
{
public:
	RayTracerDemo(UINT width, UINT height, std::wstring name);
    ~RayTracerDemo();

	void OnInit() override;
	void OnUpdate() override;
	void OnRender() override;
    void OnSizeChanged(UINT width, UINT height, bool minimized) override;
    void OnWindowMoved(int xPos, int yPos) override;
    void OnDestroy() override;
    void OnKeyDown(UINT8 key) override;
    void OnKeyUp(UINT8 key) override;

    virtual IDXGISwapChain* GetSwapchain() { return m_swapChain.Get(); }

private:
    static const UINT FrameCount = 3; // Tripple-buffering
    static constexpr float kClearColor[4] = { 1.0f, 0.0f, 1.0f, 1.0f }; // magenta

    enum class RendererType {
        CPU,
        CUDA,
        _Count
    };

    static const UINT m_rendererTypeCount = static_cast<UINT>(RendererType::_Count);
    RendererType m_rendererType;

    enum class CPURendererMode {
        Sequential,
        Multithreaded,
        _Count
    };

    static const UINT m_cpuRendererModeCount = static_cast<UINT>(CPURendererMode::_Count);
    CPURendererMode m_cpuRendererMode;
    UINT m_cpuThreadPoolSize;
    bool m_prioritizePCores;

    struct Vertex
    {
        XMFLOAT4 position;
        XMFLOAT2 uv;
    };

    struct Resolution
    {
        UINT Width;
        UINT Height;
    };

    static const Resolution m_resolutionOptions[];
    static const UINT m_resolutionOptionsCount;
    static UINT m_resolutionIndex; // Index of the current scene rendering resolution from m_resolutionOptions.

    CD3DX12_VIEWPORT m_blitViewport;
    CD3DX12_RECT m_blitScissorRect;
    ComPtr<ID3D12Device> m_d3d12Device;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    ComPtr<ID3D12Resource> m_rendererBuffer;
    D3D12_RESOURCE_STATES m_rendererBufferState;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

    ComPtr<ID3D12Resource> m_stagingBuffer;
    void* m_stagingMappedPtr = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_placedFootprint;
    UINT m_uploadRequiredSize;
    UINT m_stagingRowPitchInBytes;
    UINT64 m_stagingDataOffset;

    UINT m_rtvDescriptorSize;
    UINT m_srvDescriptorSize;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    UINT64 m_fenceValues[FrameCount];
    ComPtr<ID3D12Fence> m_fence;

    // UILayer.
    std::unique_ptr<UILayer> m_uiLayer;
    bool m_bCtrlKeyIsPressed;
    bool m_vsyncEnabled;
    bool m_telemetryEnabled;

    // Per Frame Stats.
    float m_avgFps;
    float m_frameTimeMs;
    float m_gpuUploadMs;
    float m_cpuTimeMs;
    float m_cudaTimeMs;

    UINT64 m_totalFrames;
    UINT m_samplesPerPixel;

    // Timers for instumenting sections.
    StepTimer m_timer;
    std::unique_ptr<GPUTimer> m_gpuTimer;
    const UINT GPU_UPLOAD_TIMER = 0; // Id for GPU Upload Timer

    // Track the state of the window.
    // If it's minimized the app may decide not to render frames.
    bool m_windowVisible;;
    bool m_windowedMode;

    // Renderers.
    std::unique_ptr<CPURenderer> m_cpuRenderer;
    std::unique_ptr<CudaRenderer> m_cudaRenderer;

    // Telemetry.
    std::unique_ptr<Telemetry> m_telemetry;

    void LoadPipeline();
    void LoadAssets();
    void LoadResolutionDependentResources();
    void LoadSizeDependentResources();
    void ReleaseSizeDependentResources();
    void LoadRenderer();
    void LoadTelemetry();
    void UpdateUI();
    void RenderScene();
    void WaitForGpu();
    void MoveToNextFrame();
    void UpdateSwapChainBuffer(UINT width, UINT height/*, DXGI_FORMAT format*/);
    void UpdateBlitViewAndScissor();
    void ReleaseD3DResources();
    void RestoreD3DResources();

    void UpdateTitle();
    void CalculateFrameStats();
    void UpdateTelemetry();

    // UI Controls
    void ToggleVsync();
    void ToggleRenderer();
    void ToggleCPURendererMode();
    void TogglePrioritizePCores();
    void ToggleUI();
    void ToggleTelemetry();
    void UpdateCPUThreadPoolSize(UINT delta);
};
