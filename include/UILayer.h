#pragma once

using namespace DirectX;
using namespace Microsoft::WRL;

class UILayer 
{
public:
	UILayer(UINT frameCount, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue);

	void UpdateLabels(const std::wstring& uiText);
	void Render(UINT frameIndex);
	void Resize(ComPtr<ID3D12Resource>* ppRenderTargets, UINT width, UINT height);
	void ReleaseResources();

private:
	UINT FrameCount() { return static_cast<UINT>(m_wrappedRenderTargets.size()); }

	// Render target dimensions.
	float m_width;
	float m_height;

	struct TextBlock
	{
		std::wstring text;
		D2D1_RECT_F layout;
		IDWriteTextFormat* pFormat;
	};

	ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
	ComPtr<ID3D11On12Device> m_d3d11On12Device;
	ComPtr<IDWriteFactory> m_dwriteFactory;
	ComPtr<ID2D1Factory3> m_d2dFactory;
	ComPtr<ID2D1Device2> m_d2dDevice;
	ComPtr<ID2D1DeviceContext2> m_d2dDeviceContext;
	std::vector<ComPtr<ID3D11Resource>> m_wrappedRenderTargets;
	std::vector<ComPtr<ID2D1Bitmap1>> m_d2dRenderTargets;
	ComPtr<ID2D1SolidColorBrush> m_textBrush;
	ComPtr<IDWriteTextFormat> m_textFormat;
	std::vector<TextBlock> m_textBlocks;

	void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue);
};