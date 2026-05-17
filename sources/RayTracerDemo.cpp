#include "stdafx.h"
#include "RayTracerDemo.h"
#include "UILayer.h"
#include "CPURenderer.h"

const RayTracerDemo::Resolution RayTracerDemo::m_resolutionOptions[] =
{
	{ 640u, 480u },
	{ 800u, 600u },
	{ 1280u, 720u },
	{ 1920u, 1080u },
	{ 2560u, 1440u },
	{ 3840u, 2160u }
};
const UINT RayTracerDemo::m_resolutionOptionsCount = _countof(m_resolutionOptions);
UINT RayTracerDemo::m_resolutionIndex = 2; // Default 1280x720, Main() launches with this.

RayTracerDemo::RayTracerDemo(UINT width, UINT height, std::wstring name) :
	DXApp(width, height, name),
	m_frameIndex(0),
	m_blitViewport(0.0f, 0.0f, 0.0f, 0.0f),
	m_blitScissorRect(0, 0, 0, 0),
	m_windowVisible(true),
	m_windowedMode(true),
	m_rtvDescriptorSize(0),
	m_fenceValues{},
	m_fenceEvent{},
	m_vsyncEnabled(false),
	m_telemetryEnabled(false),
	m_samplesPerPixel(1),

	m_avgFps(0),
	m_frameTimeMs(0),
	m_gpuUploadMs(0),
	m_cpuTimeMs(0),
	m_cudaTimeMs(0),
	m_totalFrames(0),

	m_rendererType(RendererType::CPU),
	m_cpuRendererMode(CPURendererMode::Multithreaded),
	m_cpuThreadPoolSize(8),
	m_prioritizePCores(true),

	m_rendererBufferState(D3D12_RESOURCE_STATE_COPY_DEST),
	m_bCtrlKeyIsPressed(false)
{
	m_enableUI = true;
	ThrowIfFailed(DXGIDeclareAdapterRemovalSupport());
}

RayTracerDemo::~RayTracerDemo()
{
}

void RayTracerDemo::OnInit()
{
	LoadPipeline();
	LoadAssets();
	LoadRenderer();
}

// Update frame-based values.
void RayTracerDemo::OnUpdate()
{
	m_timer.Tick();

	CalculateFrameStats();

	if (m_enableUI)
	{
		UpdateUI();
	}

	UpdateTitle();
}

void RayTracerDemo::OnRender()
{
	if (m_windowVisible)
	{
		try
		{
			// Render the scene.
			RenderScene();

			if (m_enableUI)
			{
				m_uiLayer->Render(m_frameIndex); // Uses Acquire/ReleaseWrappedResources, transitions swapchain from RENDER_TARGET --> PRESENT.
			}

			// When using sync interval 0, it is recommended to always pass the tearing
			// flag when it is supported, even when presenting in windowed mode.
			// However, this flag cannot be used if the app is in fullscreen mode as a
			// result of calling SetFullscreenState. When Vsync is enabled, tearing
			// is not supported. https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
			UINT presentFlags = (!m_vsyncEnabled && m_tearingSupport && m_windowedMode) ? DXGI_PRESENT_ALLOW_TEARING : 0;

			// Determine sync interval: 1 for vsync, 0 for no wait.
			UINT syncInterval = m_vsyncEnabled ? 1 : 0;

			// Present the frame.
			ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));

			// Resolve GPU timers (uses internal fence, doesn't block the caller).
			if (m_gpuTimer) {
				m_gpuUploadMs = m_gpuTimer->ResolveAndRead(GPU_UPLOAD_TIMER, m_frameIndex);
			}

			MoveToNextFrame();

			UpdateTelemetry();
		}
		catch (HrException& e)
		{
			if (e.Error() == DXGI_ERROR_DEVICE_REMOVED || e.Error() == DXGI_ERROR_DEVICE_RESET)
			{
				RestoreD3DResources();
			}
			else
			{
				throw;
			}
		}
	}
}

void RayTracerDemo::OnSizeChanged(UINT width, UINT height, bool minimized)
{
	// Determine if the swap buffers and other resources need to be resized or not.
	if ((width != m_width || height != m_height) && !minimized)
	{
		// Update the width, height, and aspect ratio member variables.
		UpdateForSizeChange(width, height);

		// Release the resources holding references to the swap chain (requirement of
		// IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
		// current fence value.
		ReleaseSizeDependentResources();

		// Update the size of swapchain buffers.
		UpdateSwapChainBuffer(width, height/*, GetBackBufferFormat()*/);

		// Re-create resolution-dependent resources.
		LoadResolutionDependentResources();
	}

	m_windowVisible = !minimized;
}

void RayTracerDemo::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	if (!m_tearingSupport)
	{
		// Fullscreen state should always be false before exiting the app.
		ThrowIfFailed(m_swapChain->SetFullscreenState(FALSE, nullptr));
	}

	CloseHandle(m_fenceEvent);
}

void RayTracerDemo::OnWindowMoved(int xPos, int yPos)
{
	UNREFERENCED_PARAMETER(xPos);
	UNREFERENCED_PARAMETER(yPos);

	if (!m_swapChain)
	{
		return;
	}
}

void RayTracerDemo::OnKeyDown(UINT8 key)
{
	switch (key)
	{
	case 'V': // Press V to toggle vsync.
		ToggleVsync();
		break;
	case 'R': // Press R to toggle Renderer.
		ToggleRenderer();
		break;
	case 'I': // Press CTRL+I to increase CPU threadpool size.
		if (m_bCtrlKeyIsPressed)
		{
			UpdateCPUThreadPoolSize(1);
		}
		break;
	case 'D': // Press CTRL+D to decrease CPU threadpool size.
		if (m_bCtrlKeyIsPressed)
		{
			UpdateCPUThreadPoolSize(-1);
		}
		break;
	case 'U': // Press U to toggle UI.
		ToggleUI();
		break;
	case 'P': // Press P to toggle P core prioritization for CPU renderer.
		TogglePrioritizePCores();
		break;
	case 'M': // Press CTRL+M to toggle CPU renderer mode (Sequential / Multithreaded).
		if (m_bCtrlKeyIsPressed) 
		{ 
			ToggleCPURendererMode();
		}
		break;
	case 'L': // Press L to enable telemetry logging to file.
		ToggleTelemetry();
		break;
	case 'J': // Press CTRL+J to increase samples per pixel
		if (m_bCtrlKeyIsPressed)
		{
			m_samplesPerPixel = std::min(16u, m_samplesPerPixel + 1);
			if (m_cpuRenderer) m_cpuRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
			if (m_cudaRenderer) m_cudaRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
		}
		break;
	case 'K': // Press CTRL+K decrease samples per pixel
		if (m_bCtrlKeyIsPressed)
		{
			m_samplesPerPixel = std::max(1u, m_samplesPerPixel - 1);
			if (m_cpuRenderer) m_cpuRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
			if (m_cudaRenderer) m_cudaRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
		}
		break;

		// Instrument the Page UP key to change the scene rendering resolution 
		// to the next resolution option. 
	case VK_PRIOR:
		m_resolutionIndex = (m_resolutionIndex + 1) % m_resolutionOptionsCount;

		// Wait for the GPU to finish with the resources we're about to free.
		WaitForGpu();

		// Explicitly release old renderer resolution resources before recreating.
		if (m_stagingBuffer)
		{
			m_stagingBuffer->Unmap(0, nullptr);
			m_stagingMappedPtr = nullptr;
			m_stagingBuffer.Reset();
		}
		m_rendererBuffer.Reset();
		m_rendererBufferState = D3D12_RESOURCE_STATE_COMMON;
		if (m_cpuRenderer) m_cpuRenderer->ResetInit();
		if (m_cudaRenderer) m_cudaRenderer->ResetInit();

		// Update resources dependent on the scene rendering resolution.
		LoadResolutionDependentResources();
		break;
		// Instrument the Page Down key to change the scene rendering resolution 
		// to the previous resolution option.
	case VK_NEXT:
		if (m_resolutionIndex == 0)
		{
			m_resolutionIndex = m_resolutionOptionsCount - 1;
		}
		else
		{
			m_resolutionIndex--;
		}

		// Wait for the GPU to finish with the resources we're about to free.
		WaitForGpu();

		// Explicitly release old renderer resolution resources before recreating.
		if (m_stagingBuffer)
		{
			m_stagingBuffer->Unmap(0, nullptr);
			m_stagingMappedPtr = nullptr;
			m_stagingBuffer.Reset();
		}
		m_rendererBuffer.Reset();
		m_rendererBufferState = D3D12_RESOURCE_STATE_COMMON;
		if (m_cpuRenderer) m_cpuRenderer->ResetInit();
		if (m_cudaRenderer) m_cudaRenderer->ResetInit();

		// Update resources dependent on the scene rendering resolution.
		LoadResolutionDependentResources();
		break;
	case VK_CONTROL:
		m_bCtrlKeyIsPressed = true;
		break;
	default:
		break;
	}
}

void RayTracerDemo::OnKeyUp(UINT8 key)
{
	switch (key)
	{
	case VK_CONTROL:
		m_bCtrlKeyIsPressed = false;
		break;
	default:
		break;
	}
}

// Load the rendering pipeline dependencies.
void RayTracerDemo::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3d12Device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3d12Device)
		));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
	NAME_D3D12_OBJECT(m_commandQueue);

	// Create the GPU timer.
	m_gpuTimer = std::make_unique<GPUTimer>(m_d3d12Device.Get(), m_commandQueue.Get(), FrameCount);

	// Describe and create the swap chain.
	// The resolution of the swap chain buffers will match the resolution of the window, enabling the
	// app to enter iFlip when in fullscreen mode. We will also keep a separate buffer that is not part
	// of the swap chain as an intermediate render target, whose resolution will control the rendering
	// resolution of the scene.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	// It is recommended to always use the tearing flag when it is available.
	swapChainDesc.Flags = m_tearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	if (m_tearingSupport)
	{
		// When tearing support is enabled we will handle ALT+Enter key presses in the
		// window message loop rather than let DXGI handle it by calling SetFullscreenState.
		factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER);
	}

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		// Describe and create a shader resource view (SRV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_d3d12Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

		m_rtvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_srvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV and a command allocator for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_d3d12Device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);

			NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);

			rtvHandle.Offset(1, m_rtvDescriptorSize);

			ThrowIfFailed(m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
		}
	}
}

void RayTracerDemo::LoadAssets()
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	// Create the Root Signature.
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		// Descriptor table for SRV (t0).
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);

		// Root constant for BlitParams.
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

		rootParameters[1].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);


		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ThrowIfFailed(m_d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		UINT8* pVertexShaderData = nullptr;
		UINT8* pPixelShaderData = nullptr;
		UINT vertexShaderDataLength = 0;
		UINT pixelShaderDataLength = 0;

		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"FullscreenPassVS.cso").c_str(), &pVertexShaderData, &vertexShaderDataLength));
		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"FullscreenPassPS.cso").c_str(), &pPixelShaderData, &pixelShaderDataLength));

		if (!pVertexShaderData || vertexShaderDataLength == 0)
		{
			OutputDebugStringA("ERROR: Vertex shader failed to load or is empty!\n");
		}
		if (!pPixelShaderData || pixelShaderDataLength == 0)
		{
			OutputDebugStringA("ERROR: Pixel shader failed to load or is empty!\n");
		}

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
		NAME_D3D12_OBJECT(m_pipelineState);
	}

	// Single-use command allocator and command list for creating resources.
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;

	ThrowIfFailed(m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	ThrowIfFailed(m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

	// Create the command list.
	{
		ThrowIfFailed(m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

		// Command lists are created in the recording state, but there is nothing
		// to record yet. The main loop expects it to be closed, so close it now.
		ThrowIfFailed(m_commandList->Close());
	}

	LoadSizeDependentResources();
	LoadResolutionDependentResources();

	// Create the vertex buffer.
	ComPtr<ID3D12Resource> vertexBufferUpload;
	{
		// Define the geometry for a quad.
		/*
		v0 ─── v1
		│  \   │
		│   \  │
		v2 ─── v3
		*/

		Vertex quadVertices[] =
		{
			{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },   // Bottom left.
			{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },    // Top left.
			{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },    // Bottom right.
			{ { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } }      // Top right.
		};

		const UINT vertexBufferSize = sizeof(quadVertices);

		ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBufferUpload)));

		NAME_D3D12_OBJECT(m_vertexBuffer);

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(vertexBufferUpload->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, quadVertices, sizeof(quadVertices));
		vertexBufferUpload->Unmap(0, nullptr);

		commandList->CopyBufferRegion(m_vertexBuffer.Get(), 0, vertexBufferUpload.Get(), 0, vertexBufferSize);
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Close the resource creation command list and execute it to begin the vertex buffer copy into
	// the default heap.
	ThrowIfFailed(commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_d3d12Device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}
}

// Load resources that are dependent on the size of the main window.
void RayTracerDemo::LoadSizeDependentResources()
{
	UpdateBlitViewAndScissor();

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_d3d12Device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
			NAME_D3D12_OBJECT_INDEXED(m_renderTargets, n);
		}
	}

	if (m_enableUI)
	{
		if (!m_uiLayer)
		{
			m_uiLayer = std::make_unique<UILayer>(FrameCount, m_d3d12Device.Get(), m_commandQueue.Get());
		}
		m_uiLayer->Resize(m_renderTargets, m_width, m_height);
	}
}

void RayTracerDemo::LoadResolutionDependentResources()
{
	UpdateBlitViewAndScissor();

	const UINT render_width = m_resolutionOptions[m_resolutionIndex].Width;
	const UINT render_height = m_resolutionOptions[m_resolutionIndex].Height;

	DXGI_SWAP_CHAIN_DESC desc = {};
	m_swapChain->GetDesc(&desc);

	// Create the texture (default heap), initial state COPY_DEST.
	{
		CD3DX12_RESOURCE_DESC renderTargetDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			desc.BufferDesc.Format,
			render_width,
			render_height,
			1u, // Array
			1u, // Mips
			desc.SampleDesc.Count,
			desc.SampleDesc.Quality,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_TEXTURE_LAYOUT_UNKNOWN,
			0u);

		ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&renderTargetDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_rendererBuffer)));

		m_rendererBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
		NAME_D3D12_OBJECT(m_rendererBuffer);
	}

	// Create SRV.
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.BufferDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_d3d12Device->CreateShaderResourceView(
			m_rendererBuffer.Get(),
			&srvDesc,
			m_srvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// Query the GPU footprint & required size for uploading the texture.
	{
		D3D12_RESOURCE_DESC texDesc = m_rendererBuffer->GetDesc();
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		UINT64 requiredSize = 0;

		m_d3d12Device->GetCopyableFootprints(
			&texDesc,
			0,      // First subresource
			1,      // Num subresources
			0,      // Base offset
			&m_placedFootprint,
			&numRows,
			&rowSizeInBytes,
			&requiredSize);

		m_uploadRequiredSize = static_cast<UINT>(requiredSize);
		m_stagingRowPitchInBytes = static_cast<UINT>(m_placedFootprint.Footprint.RowPitch);
		m_stagingDataOffset = m_placedFootprint.Offset;
	}

	// Create the staging (upload) buffer sized to requiredSize.
	{
		CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

		// Use requiredSize (aligned) from GetCopyableFootprints.
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
			m_uploadRequiredSize,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_TEXTURE_LAYOUT_ROW_MAJOR);

		// Must be 0 or 65526.
		bufferDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

		ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_stagingBuffer)));

		// Map once and keep it mapped until release.
		ThrowIfFailed(m_stagingBuffer->Map(0, nullptr, &m_stagingMappedPtr));
	}
}

// Release resources that are dependent on the size of the main window.
void RayTracerDemo::ReleaseSizeDependentResources()
{
	if (m_stagingBuffer)
	{
		m_stagingBuffer->Unmap(0, nullptr);
		m_stagingBuffer.Reset();
		m_stagingMappedPtr = nullptr; 
	}

	m_rendererBufferState = D3D12_RESOURCE_STATE_COMMON;

	for (UINT i = 0; i < FrameCount; i++)
	{
		m_renderTargets[i].Reset();
	}

	if (m_enableUI)
	{
		m_uiLayer.reset();
	}
}

void RayTracerDemo::LoadRenderer()
{
	m_cpuRenderer.reset();
	m_cudaRenderer.reset();

	switch (m_rendererType)
	{
	case RendererType::CPU:
		m_cpuRenderer = std::make_unique<CPURenderer>();
		m_cpuRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
		break;
	case RendererType::CUDA:
		m_cudaRenderer = std::make_unique<CudaRenderer>();
		m_cudaRenderer->SetSamplesPerPixel(static_cast<int>(m_samplesPerPixel));
		break;

	default:
		break;
	}
}

void RayTracerDemo::LoadTelemetry()
{
	m_telemetry = std::make_unique<Telemetry>();
}

void RayTracerDemo::UpdateUI()
{
	std::vector<std::wstring> labels;
	{
		std::wstringstream wLabel;
		wLabel.precision(1);
		wLabel << std::fixed << L"FPS: " << m_avgFps << L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel.precision(0);
		wLabel << std::fixed << L"Frame Time: " << m_frameTimeMs << L" ms" << L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel.precision(1);
		wLabel << std::fixed << L"GPU Upload: " << m_gpuUploadMs << L" ms" << L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel << std::fixed << L"Vsync: " << (m_vsyncEnabled ? L"ON" : L"OFF") << L" [V]" << L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel << std::fixed << L"Resolution: "
			<< m_resolutionOptions[m_resolutionIndex].Width << L"x"
			<< m_resolutionOptions[m_resolutionIndex].Height << L" [pgUP/DN]"
			<< L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstring renderer;
		switch (m_rendererType) {
		case RendererType::CPU: renderer = L"CPU"; break;
		case RendererType::CUDA: renderer = L"CUDA"; break;
		}

		std::wstring cpuRendererMode;
		switch (m_cpuRendererMode) {
		case CPURendererMode::Sequential: cpuRendererMode = L"Sequential"; break;
		case CPURendererMode::Multithreaded: cpuRendererMode = L"Multithreaded"; break;
		}

		std::wstringstream wLabel;
		wLabel.precision(1);
		wLabel << std::fixed << L"Renderer: " << renderer << L" [R]" << L"\n";
		if (m_rendererType == RendererType::CPU) {
			wLabel << std::fixed
				<< L"Render time: " << m_cpuTimeMs << L" ms" << L"\n"
				<< L"Mode: " << cpuRendererMode << L" [CTRL+M]\n";
			if (m_cpuRendererMode == CPURendererMode::Multithreaded)
			{
				wLabel << L"CPU Thread Pool Size: " << m_cpuThreadPoolSize << L" [CTRL+I/D]\n"
					<< L"Prioritize P cores: " << (m_prioritizePCores ? L"ON" : L"OFF") << L"\n";
			}
		}
		else if (m_rendererType == RendererType::CUDA) {
			wLabel << std::fixed << L"Render time: " << m_cudaTimeMs << L" ms\n";
		}
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel << std::fixed << L"Samples per Pixel: " << m_samplesPerPixel << L" [CTRL+J/K]"
			<< L"\n";
		labels.push_back(wLabel.str());
	}

	{
		std::wstringstream wLabel;
		wLabel << std::fixed << L"Record telemetry: " << (m_telemetryEnabled ? L"ON" : L"OFF") << L" [L]"
			<< L"\n";
		labels.push_back(wLabel.str());
	}

	std::wstring uiText = L"";
	for (const auto& s : labels)
	{
		uiText += s;
	}

	m_uiLayer->UpdateLabels(uiText);
}

void RayTracerDemo::RenderScene()
{
	// Reset allocator and command list for the current frame.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()));

	// Resource state transitions.
	D3D12_RESOURCE_BARRIER barriers[2];
	INT barrierCount = 0;

	// Transition backbuffer from PRESENT -> RENDER_TARGET.
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	// For the very first frame, the buffer STATE is already in D3D12_RESOURCE_STATE_COPY_DEST
	// which is set manually at the buffer creation in LoadResolutionDependentResources().
	if (m_rendererBufferState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_rendererBuffer.Get(),
			m_rendererBufferState,
			D3D12_RESOURCE_STATE_COPY_DEST);
	}

	m_commandList->ResourceBarrier(barrierCount, barriers);

	auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_frameIndex,
		m_rtvDescriptorSize
	);

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	m_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

	const UINT rwidth = static_cast<UINT>(m_rendererBuffer->GetDesc().Width);
	const UINT rheight = static_cast<UINT>(m_rendererBuffer->GetDesc().Height);

	// Render the scene & fill the staging buffer.
	if (m_rendererType == RendererType::CPU)
	{
		// Configure CPU Renderer.
		m_cpuRenderer->Configure(m_cpuThreadPoolSize, m_prioritizePCores);

		if (m_cpuRendererMode == CPURendererMode::Multithreaded)
		{
			m_cpuRenderer->RenderFrame(m_stagingMappedPtr, m_stagingRowPitchInBytes, m_stagingDataOffset, rwidth, rheight, m_timer.GetTotalSeconds());
		}
		else
		{
			m_cpuRenderer->RenderFrameSequential(m_stagingMappedPtr, m_stagingRowPitchInBytes, m_stagingDataOffset, rwidth, rheight, m_timer.GetTotalSeconds());
		}

		m_cpuTimeMs = m_cpuRenderer->GetLastRenderTimeMs();
	}

	if (m_rendererType == RendererType::CUDA)
	{
		m_cudaRenderer->RenderFrame(m_stagingMappedPtr, m_stagingRowPitchInBytes, m_stagingDataOffset, rwidth, rheight, m_timer.GetTotalSeconds());
		m_cudaTimeMs = m_cudaRenderer->GetLastRenderTimeMs();
	}

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = m_stagingBuffer.Get();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = m_placedFootprint;

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = m_rendererBuffer.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	// Instrument GPU Upload time - Start the timer.
	if (m_gpuTimer)
		m_gpuTimer->Start(GPU_UPLOAD_TIMER, m_frameIndex, m_commandList.Get());

	// Record GPU Copy command.
	m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	// Stop the timer.
	if (m_gpuTimer)
		m_gpuTimer->Stop(GPU_UPLOAD_TIMER, m_frameIndex, m_commandList.Get());

	// Transition GPU buffer from COPY_DEST -> PIXEL_SHADER_RESOURCE.
	D3D12_RESOURCE_BARRIER srvBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_rendererBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_commandList->ResourceBarrier(1, &srvBarrier);
	m_rendererBufferState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// Blit fulscreen quad pass.
	{
		// Bind PSO, root signature, SRV heap.
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
		ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
		m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		// Bind SRV to root parameter 0.
		m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

		// Bind vertex buffer.
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

		// Set viewport & scissor.
		m_commandList->RSSetViewports(1, &m_blitViewport);
		m_commandList->RSSetScissorRects(1, &m_blitScissorRect);

		// Draw fullscreen quad (4 vertices: triangle strip)
		m_commandList->DrawInstanced(4, 1, 0, 0);
	}

	// Final transition RENDER_TARGET -> PRESENT.
	D3D12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);

	// Let UILayer handle the transition if enabled.
	if (!m_enableUI)
	{
		m_commandList->ResourceBarrier(1, &presentBarrier);
	}

	ThrowIfFailed(m_commandList->Close());

	// Execute the command lists.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

// Wait for pending GPU work to complete.
void RayTracerDemo::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void RayTracerDemo::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Count frames.
	++m_totalFrames;

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

void RayTracerDemo::UpdateSwapChainBuffer(UINT width, UINT height/*, DXGI_FORMAT format*/)
{
	if (!m_swapChain)
	{
		return;
	}

	// Flush all current GPU commands.
	WaitForGpu();

	// Release the resources holding references to the swap chain (requirement of
	// IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
	// current fence value.
	for (UINT n = 0; n < FrameCount; n++)
	{
		m_renderTargets[n].Reset();
		m_fenceValues[n] = m_fenceValues[m_frameIndex];
	}

	// Resize the swap chain to the desired dimensions.
	DXGI_SWAP_CHAIN_DESC desc = {};
	m_swapChain->GetDesc(&desc);
	ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, width, height, desc.BufferDesc.Format, desc.Flags));

	BOOL fullscreenState;
	ThrowIfFailed(m_swapChain->GetFullscreenState(&fullscreenState, nullptr));
	m_windowedMode = !fullscreenState;

	// Reset the frame index to the current back buffer index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Update the width, height, and aspect ratio member variables.
	UpdateForSizeChange(width, height);

	LoadSizeDependentResources();
}

void RayTracerDemo::UpdateBlitViewAndScissor()
{
	float viewWidthRatio = static_cast<float>(m_resolutionOptions[m_resolutionIndex].Width) / m_width;
	float viewHeightRatio = static_cast<float>(m_resolutionOptions[m_resolutionIndex].Height) / m_height;

	float x = 1.0f;
	float y = 1.0f;

	if (viewWidthRatio < viewHeightRatio)
	{
		// The scaled image's height will fit to the viewport's height and 
		// its width will be smaller than the viewport's width.
		x = viewWidthRatio / viewHeightRatio;
	}
	else
	{
		// The scaled image's width will fit to the viewport's width and 
		// its height may be smaller than the viewport's height.
		y = viewHeightRatio / viewWidthRatio;
	}

	m_blitViewport.TopLeftX = m_width * (1.0f - x) / 2.0f;
	m_blitViewport.TopLeftY = m_height * (1.0f - y) / 2.0f;
	m_blitViewport.Width = x * m_width;
	m_blitViewport.Height = y * m_height;

	m_blitScissorRect.left = static_cast<LONG>(m_blitViewport.TopLeftX);
	m_blitScissorRect.right = static_cast<LONG>(m_blitViewport.TopLeftX + m_blitViewport.Width);
	m_blitScissorRect.top = static_cast<LONG>(m_blitViewport.TopLeftY);
	m_blitScissorRect.bottom = static_cast<LONG>(m_blitViewport.TopLeftY + m_blitViewport.Height);
}

void RayTracerDemo::UpdateTitle()
{
	std::vector<std::wstring> labels;
	{
		std::wstringstream wLabel;
		wLabel << std::fixed << L"Rendering at " 
			<< m_resolutionOptions[m_resolutionIndex].Width 
			<< "x" 
			<< m_resolutionOptions[m_resolutionIndex].Height 
			<< L" scaled to "
			<< m_width
			<< L"x" <<
			m_height 
			<< L" (window size) ";
		labels.push_back(wLabel.str());
	}
	{
		if (!m_enableUI)
		{
			std::wstringstream wLabel;
			wLabel << std::fixed << L"| Press U to enable UI";
			labels.push_back(wLabel.str());
		}
	}

	std::wstring updatedTitle;
	for (const auto& s : labels) {
		updatedTitle += s;
	}

	SetCustomWindowText(updatedTitle.c_str());
}

// Code computes the average frames per second, and also the 
// average time it takes to render one frame.
void RayTracerDemo::CalculateFrameStats()
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0f;
	double totalTime = m_timer.GetTotalSeconds();
	frameCnt++;

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float diff = static_cast<float>(totalTime - elapsedTime);
		m_avgFps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;
	}

	// Compute frame time.
	double seconds = m_timer.GetElapsedSeconds();
	if (seconds > 0.0)
	{
		m_frameTimeMs = static_cast<float>(seconds * 1000.0);
	}
}

void RayTracerDemo::UpdateTelemetry()
{
	if (!m_telemetryEnabled || !m_telemetry || !m_telemetry->IsActive())
		return;

	float renderTimeMs = (m_rendererType == RendererType::CPU) ? m_cpuTimeMs : m_cudaTimeMs;
	UINT renderWidth = m_resolutionOptions[m_resolutionIndex].Width;
	UINT renderHeight = m_resolutionOptions[m_resolutionIndex].Height;
	UINT totalPixels = renderWidth * renderHeight;
	float timePerPixel = (renderTimeMs * 1000000) / totalPixels;

	FrameMetrics metrics;
	metrics.frameIndex = m_totalFrames;
	metrics.absoluteTimeSec = static_cast<double>(m_timer.GetTotalSeconds());
	metrics.rendererName = (m_rendererType == RendererType::CPU)
		? (m_cpuRendererMode == CPURendererMode::Multithreaded ? "CPU-MT" : "CPU-SQ")
		: "CUDA";
	metrics.renderWidth = renderWidth;
	metrics.renderHeight = renderHeight;
	metrics.totalPixels = totalPixels;
	metrics.samplesPerPixel = m_samplesPerPixel;
	metrics.timePerPixel = timePerPixel;
	metrics.frameTimeMs = m_frameTimeMs;
	metrics.renderTimeMs = renderTimeMs;
	metrics.gpuUploadMs = m_gpuUploadMs;
	metrics.vsyncEnabled = m_vsyncEnabled;
	metrics.cpuThreadPoolSize = m_cpuThreadPoolSize;
	metrics.cpuPrioritizePCores = m_prioritizePCores;

	m_telemetry->LogFrame(metrics);
}

void RayTracerDemo::ReleaseD3DResources()
{
	if (m_enableUI)
	{
		m_uiLayer.reset();
	}

	CloseHandle(m_fenceEvent);
	m_fenceEvent = nullptr;
	m_fence.Reset();

	ResetComPtrArray(&m_renderTargets);
	ResetComPtrArray(&m_commandAllocators);

	m_commandList.Reset();
	m_commandQueue.Reset();
	m_swapChain.Reset();
	m_d3d12Device.Reset();
}

void RayTracerDemo::RestoreD3DResources()
{
	// Give GPU a chance to finish its execution in progress.
	try
	{
		WaitForGpu();
	}
	catch (HrException&)
	{
		// Do nothing, currently attached adapter is unresponsive.
	}
	ReleaseD3DResources();
	OnInit();
}

void RayTracerDemo::ToggleVsync()
{
	m_vsyncEnabled = !m_vsyncEnabled;
}

void RayTracerDemo::ToggleRenderer()
{
	WaitForGpu();

	m_rendererType = static_cast<RendererType>(
		(static_cast<UINT>(m_rendererType) + 1) % m_rendererTypeCount);

	LoadRenderer();
}

void RayTracerDemo::ToggleCPURendererMode()
{
	m_cpuRendererMode = static_cast<CPURendererMode>(
		(static_cast<UINT>(m_cpuRendererMode) + 1) % m_cpuRendererModeCount);
}

void RayTracerDemo::TogglePrioritizePCores()
{
	m_prioritizePCores = !m_prioritizePCores;
}

void RayTracerDemo::ToggleUI()
{
	m_enableUI = !m_enableUI;
}

void RayTracerDemo::ToggleTelemetry()
{
	m_telemetryEnabled = !m_telemetryEnabled;

	if (!m_telemetryEnabled)
	{
		// When turned off, close current file.
		if (m_telemetry)
			m_telemetry->Stop();
	}
	else
	{
		// When turned on, create a new file.
		if (!m_telemetry)
			m_telemetry = std::make_unique<Telemetry>();

		using namespace std::chrono;
		auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		std::string fname = "RayTracerStats_" + std::to_string(ms) + ".csv";

		if (!m_telemetry->Start(fname))
		{
			OutputDebugStringA("WARNING: Failed to start writing the telemetry file.\n");
			m_telemetryEnabled = false; // Revert on failure.
		}
	}
}

void RayTracerDemo::UpdateCPUThreadPoolSize(UINT delta)
{
	// Get max availalble threads on the system.
	UINT hwConcurrency = std::thread::hardware_concurrency();

	if (hwConcurrency == 0) hwConcurrency = 1;

	// Reserve at least 2 threads for OS/UI
	m_cpuThreadPoolSize = std::min(hwConcurrency - 2, m_cpuThreadPoolSize + delta);
	m_cpuThreadPoolSize = std::max(1u, m_cpuThreadPoolSize);
}
