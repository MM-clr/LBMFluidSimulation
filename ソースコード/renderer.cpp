#include "main.h"
#include "renderer.h"
#include <io.h>
#include <d3dcompiler.h> // D3DReadFileToBlob のために追加
#include <string>        // std::wstring のために追加
#include <assert.h>
#include <fstream>

// ImGui ヘッダ（コンパイルエラー: ImGuiIO 未定義 等の解決）
#include "ImGUI\imgui.h"
#include "ImGUI\imgui_impl_dx11.h"
#include "ImGUI\imgui_impl_win32.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

// --- 静的メンバー変数の実体を定義 ---
D3D_FEATURE_LEVEL       Renderer::m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;

ID3D11Device* Renderer::m_Device = nullptr;
ID3D11DeviceContext* Renderer::m_DeviceContext = nullptr;
IDXGISwapChain* Renderer::m_SwapChain = nullptr;
ID3D11RenderTargetView* Renderer::m_RenderTargetView = nullptr;
ID3D11DepthStencilView* Renderer::m_DepthStencilView = nullptr;

ID3D11Buffer* Renderer::m_WorldBuffer = nullptr;
ID3D11Buffer* Renderer::m_ViewBuffer = nullptr;
ID3D11Buffer* Renderer::m_ProjectionBuffer = nullptr;
ID3D11Buffer* Renderer::m_MaterialBuffer = nullptr;
ID3D11Buffer* Renderer::m_LightBuffer = nullptr;

ID3D11Buffer* Renderer::m_particleBufferRead = nullptr;
ID3D11Buffer* Renderer::m_particleBufferWrite = nullptr;
ID3D11ShaderResourceView* Renderer::m_particleSRV = nullptr;
ID3D11UnorderedAccessView* Renderer::m_particleUAV = nullptr;

ID3D11DepthStencilState* Renderer::m_DepthStateEnable = nullptr;
ID3D11DepthStencilState* Renderer::m_DepthStateDisable = nullptr;

ID3D11BlendState* Renderer::m_BlendState = nullptr;
ID3D11BlendState* Renderer::m_BlendStateATC = nullptr;
ID3D11RasterizerState* Renderer::m_RasterizerState = nullptr; // 追加
ID3D11SamplerState* Renderer::m_SamplerState = nullptr;       // 追加

// --- デフォルトシェーダ ---
ID3D11VertexShader* Renderer::m_DefaultVS = nullptr;
ID3D11InputLayout*  Renderer::m_DefaultInputLayout = nullptr;
ID3D11PixelShader*  Renderer::m_DefaultPS = nullptr;

void Renderer::Init()
{
	HRESULT hr = S_OK;

	// デバイス、スワップチェーン作成
	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	// 変更: ダブルバッファに変更
	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Width = SCREEN_WIDTH;
	swapChainDesc.BufferDesc.Height = SCREEN_HEIGHT;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = GetWindow();
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; // 明示

    UINT createFlags = 0;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDeviceAndSwapChain(NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        createFlags,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        &m_SwapChain,
        &m_Device,
        &m_FeatureLevel,
        &m_DeviceContext);

#if defined(_DEBUG)
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG))
    {
        OutputDebugStringA("Renderer::Init: D3D11_CREATE_DEVICE_DEBUG failed, retry without debug layer.\n");
        hr = D3D11CreateDeviceAndSwapChain(NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            0,
            NULL,
            0,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &m_SwapChain,
            &m_Device,
            &m_FeatureLevel,
            &m_DeviceContext);
    }
#endif

    if (FAILED(hr) || !m_SwapChain || !m_Device || !m_DeviceContext)
    {
        OutputDebugStringA("Renderer::Init: D3D11CreateDeviceAndSwapChain failed.\n");
        assert(false && "Renderer::Init: D3D11CreateDeviceAndSwapChain failed.");
        return;
    }

	// レンダーターゲットビュー作成
	ID3D11Texture2D* renderTarget{};
	m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&renderTarget);
	m_Device->CreateRenderTargetView(renderTarget, NULL, &m_RenderTargetView);
	renderTarget->Release();

	// デプスステンシルバッファ作成
	ID3D11Texture2D* depthStencile{};
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = swapChainDesc.BufferDesc.Width;
	textureDesc.Height = swapChainDesc.BufferDesc.Height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマットを変更
	textureDesc.SampleDesc = swapChainDesc.SampleDesc;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;
	m_Device->CreateTexture2D(&textureDesc, NULL, &depthStencile);

	// デプスステンシルビュー作成
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = textureDesc.Format;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Flags = 0;
	m_Device->CreateDepthStencilView(depthStencile, &depthStencilViewDesc, &m_DepthStencilView);
	depthStencile->Release();

	m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, m_DepthStencilView);

	// ビューポート設定
	D3D11_VIEWPORT viewport;
	viewport.Width = (FLOAT)SCREEN_WIDTH;
	viewport.Height = (FLOAT)SCREEN_HEIGHT;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	m_DeviceContext->RSSetViewports(1, &viewport);

	// ラスタライザステート設定
	D3D11_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.DepthClipEnable = TRUE;
	rasterizerDesc.MultisampleEnable = FALSE;

	m_Device->CreateRasterizerState(&rasterizerDesc, &m_RasterizerState); // メンバー変数に格納

	m_DeviceContext->RSSetState(m_RasterizerState); // 正しいステートを設定

	// ブレンドステート設定
	D3D11_BLEND_DESC blendDesc{};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	m_Device->CreateBlendState(&blendDesc, &m_BlendState);

	blendDesc.AlphaToCoverageEnable = TRUE;
	m_Device->CreateBlendState(&blendDesc, &m_BlendStateATC);

	float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_DeviceContext->OMSetBlendState(m_BlendState, blendFactor, 0xffffffff);

	// デプスステンシルステート設定
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	m_Device->CreateDepthStencilState(&depthStencilDesc, &m_DepthStateEnable); //深度有効ステート

	// 深度無効ステートを正しく作成（DepthEnable を FALSE にする）
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	m_Device->CreateDepthStencilState(&depthStencilDesc, &m_DepthStateDisable); //深度無効ステート

	m_DeviceContext->OMSetDepthStencilState(m_DepthStateEnable, NULL);

	// サンプラーステート設定
	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // 線形フィルタで滑らかに
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;   // 端をクランプ
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;   // 端をクランプ
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;   // 端をクランプ
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	m_Device->CreateSamplerState(&samplerDesc, &m_SamplerState); // メンバー変数に格納

	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerState); // 正しいステートを設定

	// 定数バッファ生成
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = sizeof(XMFLOAT4X4);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = sizeof(float);

	m_Device->CreateBuffer(&bufferDesc, NULL, &m_WorldBuffer);
	m_DeviceContext->VSSetConstantBuffers(0, 1, &m_WorldBuffer);

	m_Device->CreateBuffer(&bufferDesc, NULL, &m_ViewBuffer);
	m_DeviceContext->VSSetConstantBuffers(1, 1, &m_ViewBuffer);

	m_Device->CreateBuffer(&bufferDesc, NULL, &m_ProjectionBuffer);
	m_DeviceContext->VSSetConstantBuffers(2, 1, &m_ProjectionBuffer);

	bufferDesc.ByteWidth = sizeof(MATERIAL);

	m_Device->CreateBuffer(&bufferDesc, NULL, &m_MaterialBuffer);
	m_DeviceContext->VSSetConstantBuffers(3, 1, &m_MaterialBuffer);
	m_DeviceContext->PSSetConstantBuffers(3, 1, &m_MaterialBuffer);

	bufferDesc.ByteWidth = sizeof(LIGHT);

	m_Device->CreateBuffer(&bufferDesc, NULL, &m_LightBuffer);
	m_DeviceContext->VSSetConstantBuffers(4, 1, &m_LightBuffer);
	m_DeviceContext->PSSetConstantBuffers(4, 1, &m_LightBuffer);

	// ライト初期化
	LIGHT light{};
	light.Enable = true;
	light.Direction = XMFLOAT4(1.0f, -1.0f, 0.5f, 0.0f);
	light.Ambient = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
	light.Diffuse = XMFLOAT4(1.5f, 1.5f, 1.5f, 1.0f);
	SetLight(light);

	// マテリアル初期化
	MATERIAL material{};
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.TextureEnable = true; // ← 追加
	SetMaterial(material);

	// 既存の Init 処理の終盤にデフォルトシェーダを生成しておく
	// CreateVertexShader のオーバーロードでデフォルトレイアウトを作れるためそれを利用
	CreateVertexShader(&m_DefaultVS, &m_DefaultInputLayout, "unlitTextureVS.cso");
	CreatePixelShader(&m_DefaultPS, "unlitTexturePS.cso");

	// ImGui 初期化済みならフォント読み込みを行う、未ならスキップして後で InitImGuiFonts() を使う
	if (ImGui::GetCurrentContext()) {
	    ImGuiIO& io = ImGui::GetIO();
	    const char* paths[] = {
	        "C:\\Windows\\Fonts\\meiryo.ttc",
	        "C:\\Windows\\Fonts\\meiryo.ttf",
	        "C:\\Windows\\Fonts\\msgothic.ttc"
	    };
	    float fontSize = 16.0f;
	    bool loaded = false;
	    for (auto p : paths) {
	        std::ifstream f(p, std::ios::binary);
	        if (f.good()) {
#if (IMGUI_VERSION_NUM >= 17701)
	            io.Fonts->AddFontFromFileTTF(p, fontSize, nullptr, io.Fonts->GetGlyphRangesJapanese());
#else
	            io.Fonts->AddFontFromFileTTF(p, fontSize);
#endif
	            loaded = true;
	            break;
	        }
	    }
	    if (!loaded) io.Fonts->AddFontDefault();
	    // フォントテクスチャを作成（DX11 バックエンドが初期化済みであることが前提）
	    ImGui_ImplDX11_CreateDeviceObjects();
	} else {
	    OutputDebugStringA("Renderer::Init: ImGui context not ready ? call Renderer::InitImGuiFonts() after ImGui initialization.\n");
	}
}

void Renderer::Uninit()
{
	ReleaseParticleBuffers();

	if (m_WorldBuffer) m_WorldBuffer->Release();
	if (m_ViewBuffer) m_ViewBuffer->Release();
	if (m_ProjectionBuffer) m_ProjectionBuffer->Release();
	if (m_LightBuffer) m_LightBuffer->Release();
	if (m_MaterialBuffer) m_MaterialBuffer->Release();

	if (m_DepthStateEnable) m_DepthStateEnable->Release();
	if (m_DepthStateDisable) m_DepthStateDisable->Release();
	if (m_BlendState) m_BlendState->Release();
	if (m_BlendStateATC) m_BlendStateATC->Release();
	if (m_RasterizerState) m_RasterizerState->Release(); // 解放処理を追加
	if (m_SamplerState) m_SamplerState->Release();       // 解放処理を追加

	// デフォルトシェーダ解放
	if (m_DefaultVS) { m_DefaultVS->Release(); m_DefaultVS = nullptr; }
	if (m_DefaultInputLayout) { m_DefaultInputLayout->Release(); m_DefaultInputLayout = nullptr; }
	if (m_DefaultPS) { m_DefaultPS->Release(); m_DefaultPS = nullptr; }

	if (m_DeviceContext) m_DeviceContext->ClearState();
	if (m_RenderTargetView) m_RenderTargetView->Release();
	if (m_SwapChain) m_SwapChain->Release();
	if (m_DeviceContext) m_DeviceContext->Release();
	if (m_Device) m_Device->Release();
}

void Renderer::Begin()
{
	float clearColor[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
	m_DeviceContext->ClearRenderTargetView(m_RenderTargetView, clearColor);
	m_DeviceContext->ClearDepthStencilView(m_DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// フレーム開始時に最低限のシェーダをセットしておく（Draw 呼び出し前の未設定を防ぐ）
	if (m_DefaultVS) m_DeviceContext->VSSetShader(m_DefaultVS, nullptr, 0);
	if (m_DefaultPS) m_DeviceContext->PSSetShader(m_DefaultPS, nullptr, 0);
	if (m_DefaultInputLayout) m_DeviceContext->IASetInputLayout(m_DefaultInputLayout);

	// 追加: ピクセルシェーダがサンプラを期待するため、フレーム開始時にスロット0へ明示的にバインドする
	// （UnbindAllShaderResources 等で解除されている可能性があるため）
	if (m_SamplerState) m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerState);
}

void Renderer::End()
{
	// Present (VSync enabled)
	m_SwapChain->Present(1, 0);
}

// ???: ?E?B???h?E???T?C?Y????o?b?N?o?b?t?@ / DSV ????????
void Renderer::Resize(UINT width, UINT height)
{
    if (!m_SwapChain || !m_Device || !m_DeviceContext)
        return;

    // Release existing views
    if (m_RenderTargetView) { m_DeviceContext->OMSetRenderTargets(0, nullptr, nullptr); m_RenderTargetView->Release(); m_RenderTargetView = nullptr; }
    if (m_DepthStencilView) { m_DepthStencilView->Release(); m_DepthStencilView = nullptr; }

    // Resize swapchain buffers
    HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        // Resize ????s???????O
        OutputDebugStringA("Renderer::Resize: ResizeBuffers failed\n");
        return;
    }

    // Recreate render target view
    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
    if (FAILED(hr) || !backBuffer) {
        OutputDebugStringA("Renderer::Resize: GetBuffer failed\n");
        return;
    }
    hr = m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) {
        OutputDebugStringA("Renderer::Resize: CreateRenderTargetView failed\n");
        return;
    }

    // Recreate depth stencil texture & view
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.CPUAccessFlags = 0;
    depthDesc.MiscFlags = 0;

    ID3D11Texture2D* depthTex = nullptr;
    hr = m_Device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    if (FAILED(hr) || !depthTex) {
        OutputDebugStringA("Renderer::Resize: CreateTexture2D(depth) failed\n");
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = depthDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = 0;
    hr = m_Device->CreateDepthStencilView(depthTex, &dsvDesc, &m_DepthStencilView);
    depthTex->Release();
    if (FAILED(hr)) {
        OutputDebugStringA("Renderer::Resize: CreateDepthStencilView failed\n");
        return;
    }

    // Bind targets and update viewport
    m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, m_DepthStencilView);

    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_DeviceContext->RSSetViewports(1, &vp);
}

void Renderer::SetDepthEnable(bool Enable)
{
	if (Enable)
		m_DeviceContext->OMSetDepthStencilState(m_DepthStateEnable, NULL);
	else
		m_DeviceContext->OMSetDepthStencilState(m_DepthStateDisable, NULL);
}

void Renderer::SetATCEnable(bool Enable)
{
	float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (Enable)
		m_DeviceContext->OMSetBlendState(m_BlendStateATC, blendFactor, 0xffffffff);
	else
		m_DeviceContext->OMSetBlendState(m_BlendState, blendFactor, 0xffffffff);
}

void Renderer::SetWorldViewProjection2D()
{
	SetWorldMatrix(XMMatrixIdentity());
	SetViewMatrix(XMMatrixIdentity());

	XMMATRIX projection;
	projection = XMMatrixOrthographicOffCenterLH(0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f);
	SetProjectionMatrix(projection);
}

void Renderer::SetWorldMatrix(const XMMATRIX& WorldMatrix)
{
	XMFLOAT4X4 worldf;
	XMStoreFloat4x4(&worldf, XMMatrixTranspose(WorldMatrix));
	m_DeviceContext->UpdateSubresource(m_WorldBuffer, 0, NULL, &worldf, 0, 0);
}

void Renderer::SetViewMatrix(const XMMATRIX& ViewMatrix)
{
	XMFLOAT4X4 viewf;
	XMStoreFloat4x4(&viewf, XMMatrixTranspose(ViewMatrix));
	m_DeviceContext->UpdateSubresource(m_ViewBuffer, 0, NULL, &viewf, 0, 0);
}

void Renderer::SetProjectionMatrix(const XMMATRIX& ProjectionMatrix)
{
	XMFLOAT4X4 projectionf;
	XMStoreFloat4x4(&projectionf, XMMatrixTranspose(ProjectionMatrix));
	m_DeviceContext->UpdateSubresource(m_ProjectionBuffer, 0, NULL, &projectionf, 0, 0);
}

void Renderer::SetMaterial(const MATERIAL& Material)
{
	m_DeviceContext->UpdateSubresource(m_MaterialBuffer, 0, NULL, &Material, 0, 0);
}

void Renderer::SetLight(const LIGHT& Light)
{
	m_DeviceContext->UpdateSubresource(m_LightBuffer, 0, NULL, &Light, 0, 0);
}

void Renderer::CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName)
{
	// 頂点レイアウト
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE(layout);
	CreateVertexShader(VertexShader, VertexLayout, FileName, layout, numElements);
}

void Renderer::CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName, const D3D11_INPUT_ELEMENT_DESC* layout, UINT numElements)
{
	ID3DBlob* pVSBlob = nullptr;

	// try given path, then fallback to shader\ subfolder
	std::wstring wFileName(FileName, FileName + strlen(FileName));
	HRESULT hr = D3DReadFileToBlob(wFileName.c_str(), &pVSBlob);
	if (FAILED(hr)) {
		std::string alt = std::string("shader\\") + FileName;
		std::wstring wAlt(alt.begin(), alt.end());
		hr = D3DReadFileToBlob(wAlt.c_str(), &pVSBlob);
	}

	if (FAILED(hr) || !pVSBlob)
	{
		// 明確なデバッグメッセージを残してリターン（呼び出し側で nullptr チェックを行う）
		assert(false && "CreateVertexShader: VS .cso not found. Check path or build shaders.");
		return;
	}

	hr = m_Device->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, VertexShader);
	assert(SUCCEEDED(hr));

	hr = m_Device->CreateInputLayout(layout,
		numElements,
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		VertexLayout);
	assert(SUCCEEDED(hr));

	pVSBlob->Release();
}


void Renderer::CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName)
{
	ID3DBlob* pPSBlob = nullptr;

	std::wstring wFileName(FileName, FileName + strlen(FileName));
	HRESULT hr = D3DReadFileToBlob(wFileName.c_str(), &pPSBlob);
	if (FAILED(hr)) {
		std::string alt = std::string("shader\\") + FileName;
		std::wstring wAlt(alt.begin(), alt.end());
		hr = D3DReadFileToBlob(wAlt.c_str(), &pPSBlob);
	}

	if (FAILED(hr) || !pPSBlob)
	{
		assert(false && "CreatePixelShader: PS .cso not found. Check path or build shaders.");
		return;
	}

	hr = m_Device->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, PixelShader);
	assert(SUCCEEDED(hr));

	pPSBlob->Release();
}

// --- コンピュートシェーダー関連メソッドの実装 ---

bool Renderer::CreateParticleBuffers(size_t particleCount, size_t particleStride, void* initialData)
{
	ReleaseParticleBuffers();

	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = (UINT)(particleCount * particleStride);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = (UINT)particleStride;

	D3D11_SUBRESOURCE_DATA subData{};
	subData.pSysMem = initialData;

	if (FAILED(m_Device->CreateBuffer(&desc, &subData, &m_particleBufferRead)))
	{
		return false;
	}

	if (FAILED(m_Device->CreateBuffer(&desc, nullptr, &m_particleBufferWrite)))
	{
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = (UINT)particleCount;
	if (FAILED(m_Device->CreateUnorderedAccessView(m_particleBufferWrite, &uavDesc, &m_particleUAV)))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = (UINT)particleCount;
	if (FAILED(m_Device->CreateShaderResourceView(m_particleBufferRead, &srvDesc, &m_particleSRV)))
	{
		return false;
	}

	return true;
}

void Renderer::ReleaseParticleBuffers()
{
	if (m_particleSRV) { m_particleSRV->Release(); m_particleSRV = nullptr; }
	if (m_particleUAV) { m_particleUAV->Release(); m_particleUAV = nullptr; }
	if (m_particleBufferRead) { m_particleBufferRead->Release(); m_particleBufferRead = nullptr; }
	if (m_particleBufferWrite) { m_particleBufferWrite->Release(); m_particleBufferWrite = nullptr; }
}

void Renderer::DispatchComputeShader(const char* shaderFileName, UINT x, UINT y, UINT z)
{
	ID3D11ComputeShader* cs = nullptr;
	ID3DBlob* blob = nullptr;

	std::wstring wShaderFileName(shaderFileName, shaderFileName + strlen(shaderFileName));
	D3DReadFileToBlob(wShaderFileName.c_str(), &blob);
	m_Device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs);

	m_DeviceContext->CSSetShader(cs, nullptr, 0);
	m_DeviceContext->CSSetUnorderedAccessViews(0, 1, &m_particleUAV, nullptr);
	m_DeviceContext->CSSetShaderResources(0, 1, &m_particleSRV);

	m_DeviceContext->Dispatch(x, y, z);

	ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
	m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
	ID3D11ShaderResourceView* nullSRV[] = { nullptr };
	m_DeviceContext->CSSetShaderResources(0, 1, nullSRV);

	std::swap(m_particleBufferRead, m_particleBufferWrite);

	if (cs) cs->Release();
	if (blob) blob->Release();
}

void Renderer::CreateComputeShader(ID3D11ComputeShader** ppCS, const char* pFileName)
{
    assert(m_Device);
    assert(ppCS);

    ID3DBlob* blob = nullptr;
    // Try to compile .hlsl first (developer-friendly). Derive base name from given filename.
    std::string srcName(pFileName);
    size_t dot = srcName.find_last_of('.');
    std::string base = (dot != std::string::npos) ? srcName.substr(0, dot) : srcName;
    std::vector<std::wstring> tryHlsl;
    // try <base>.hlsl and shader\<base>.hlsl
    std::wstring wSame = std::wstring(base.begin(), base.end()); wSame += L".hlsl"; tryHlsl.push_back(wSame);
    std::string alt = std::string("shader\\") + base + ".hlsl"; std::wstring wAlt(alt.begin(), alt.end()); tryHlsl.push_back(wAlt);

    ID3DBlob* compileBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    for (auto &wp : tryHlsl) {
        HRESULT ch = D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", compileFlags, 0, &compileBlob, &errorBlob);
        if (SUCCEEDED(ch) && compileBlob) {
            blob = compileBlob;
            break;
        }
        if (errorBlob) {
            char* msg = (char*)errorBlob->GetBufferPointer();
            if (msg) { OutputDebugStringA(msg); OutputDebugStringA("\n"); }
            errorBlob->Release(); errorBlob = nullptr;
        }
        if (compileBlob) { compileBlob->Release(); compileBlob = nullptr; }
    }

    // If HLSL compile failed, try to read precompiled .cso blob
    if (!blob) {
        std::wstring wFileName(pFileName, pFileName + strlen(pFileName));
        HRESULT hr = D3DReadFileToBlob(wFileName.c_str(), &blob);
        if (FAILED(hr)) {
            std::string altc = std::string("shader\\") + pFileName;
            std::wstring wAltC(altc.begin(), altc.end());
            hr = D3DReadFileToBlob(wAltC.c_str(), &blob);
            if (FAILED(hr)) {
                // nothing found
                assert(false && "CreateComputeShader: shader .cso not found and .hlsl compile failed.");
                return;
            }
        }
    }

    HRESULT hr2 = m_Device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, ppCS);
    assert(SUCCEEDED(hr2));

    // 追加: どのファイルを読み込んだかと blob サイズ、生成された CS ポインタをログに出す
    {
        char out[512];
        size_t blobSize = blob ? blob->GetBufferSize() : 0;
        sprintf_s(out, sizeof(out), "CreateComputeShader: Loaded '%s' (blobSize=%zu) -> CS ptr=%p\n",
                  pFileName, blobSize, (void*)(*ppCS));
        OutputDebugStringA(out);
    }

    if (blob) blob->Release();
}

void Renderer::DrawWireOBB(const OBB& obb, const XMFLOAT4& color)
{
    // 8頂点をワールド座標で計算
    DirectX::XMFLOAT3 corners[8];

    float hx = obb.halfSize.x;
    float hy = obb.halfSize.y;
    float hz = obb.halfSize.z;

    DirectX::XMFLOAT3 A0 = { obb.axes[0].x, obb.axes[0].y, obb.axes[0].z };
    DirectX::XMFLOAT3 A1 = { obb.axes[1].x, obb.axes[1].y, obb.axes[1].z };
    DirectX::XMFLOAT3 A2 = { obb.axes[2].x, obb.axes[2].y, obb.axes[2].z };

    auto madd = [](const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float s) {
        return DirectX::XMFLOAT3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s);
    };

    DirectX::XMFLOAT3 C = { obb.center.x, obb.center.y, obb.center.z };

    for (int i = 0; i < 8; ++i)
    {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sy = (i & 2) ? 1.0f : -1.0f;
        float sz = (i & 4) ? 1.0f : -1.0f;
        DirectX::XMFLOAT3 p = C;
        p = madd(p, A0, hx * sx);
        p = madd(p, A1, hy * sy);
        p = madd(p, A2, hz * sz);
        corners[i] = p;
    }

    VERTEX_3D v[8];
    for (int i = 0; i < 8; ++i)
    {
        v[i].Position = corners[i];
        v[i].Normal = { 0.0f, 0.0f, 0.0f };
        v[i].Diffuse = color;
        v[i].TexCoord = { 0.0f, 0.0f };
    }

    // --- ここで idx を定義 ---
    WORD idx[] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };

    // 頂点バッファが未生成なら新たに作成
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* ctx = GetDeviceContext();

    // --- シェーダ・レイアウト・ラスタライザステート保存 ---
    ID3D11VertexShader* prevVS = nullptr;
    ID3D11PixelShader* prevPS = nullptr;
    ID3D11InputLayout* prevLayout = nullptr;
    ctx->VSGetShader(&prevVS, nullptr, nullptr);
    ctx->PSGetShader(&prevPS, nullptr, nullptr);
    ctx->IAGetInputLayout(&prevLayout);

    ID3D11RasterizerState* prevRS = nullptr;
    ctx->RSGetState(&prevRS);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_WIREFRAME;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState* wireRS = nullptr;
    device->CreateRasterizerState(&rd, &wireRS);
    ctx->RSSetState(wireRS);

    // シェーダ・レイアウトセット
    ID3D11VertexShader* vs = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11PixelShader* ps = nullptr;
    CreateVertexShader(&vs, &layout, "unlitTextureVS.cso");
    CreatePixelShader(&ps, "unlitTexturePS.cso");

    ctx->IASetInputLayout(layout);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);

    // 頂点バッファ生成
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(v);
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { v, 0, 0 };
    ID3D11Buffer* vb = nullptr;
    device->CreateBuffer(&vbDesc, &vbData, &vb);

    // インデックスバッファ生成
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.ByteWidth = sizeof(idx);
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = { idx, 0, 0 };
    ID3D11Buffer* ib = nullptr;
    device->CreateBuffer(&ibDesc, &ibData, &ib);

    // パイプラインセット
    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

    ctx->DrawIndexed(_countof(idx), 0, 0);

    // 復元
    ctx->VSSetShader(prevVS, nullptr, 0);
    ctx->PSSetShader(prevPS, nullptr, 0);
    ctx->IASetInputLayout(prevLayout);
    ctx->RSSetState(prevRS);

    // 解放
    if (prevVS) prevVS->Release();
    if (prevPS) prevPS->Release();
    if (prevLayout) prevLayout->Release();
    if (prevRS) prevRS->Release();
    if (ib) { ib->Release(); ib = nullptr; }
    if (vb) { vb->Release(); vb = nullptr; }
    if (wireRS) { wireRS->Release(); wireRS = nullptr; }
    if (vs) { vs->Release(); vs = nullptr; }
    if (ps) { ps->Release(); ps = nullptr; }
    if (layout) { layout->Release(); layout = nullptr; }
}

// 簡易フルスクリーン描画（デバッグ用途）
// SRV を PS のスロット0 にバインドして、画面全体に貼る
void Renderer::DrawFullScreenTexture(ID3D11ShaderResourceView* srv)
{
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* ctx = GetDeviceContext();

    // 簡易頂点（NDC座標） 2 三角形でクアッド
    VERTEX_3D quad[6] = {
        { {-1.0f,  1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {0.0f, 0.0f} },
        { { 1.0f,  1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {1.0f, 0.0f} },
        { {-1.0f, -1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {0.0f, 1.0f} },
        { { 1.0f,  1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {1.0f, 0.0f} },
        { { 1.0f, -1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {1.0f, 1.0f} },
        { {-1.0f, -1.0f, 0.0f}, {0,0,0}, {1,1,1,1}, {0.0f, 1.0f} },
    };

    // 頂点バッファを作成（短時間のみ）
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(quad);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = { quad, 0, 0 };
    ID3D11Buffer* vb = nullptr;
    device->CreateBuffer(&bd, &sd, &vb);

    // シェーダーを作成している前提で unlitTextureVS/PS を利用
    ID3D11VertexShader* vs = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11PixelShader* ps = nullptr;
    CreateVertexShader(&vs, &layout, "unlitTextureVS.cso");
    CreatePixelShader(&ps, "unlitTexturePS.cso");

    // ここが重要：頂点は既に NDC なので行列は単位行列にしてパススルー扱いにする
    SetWorldMatrix(XMMatrixIdentity());
    SetViewMatrix(XMMatrixIdentity());
    SetProjectionMatrix(XMMatrixIdentity());

    // --- 深度ステートを一時的に無効化（元を保存して復元） ---
    ID3D11DepthStencilState* prevDS = nullptr;
    UINT prevRef = 0;
    ctx->OMGetDepthStencilState(&prevDS, &prevRef);
    ctx->OMSetDepthStencilState(m_DepthStateDisable, 0);

    // パイプラインセット
    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &srv);

    ctx->Draw(6, 0);

    // 後片付け：SRV を解除
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    ctx->PSSetShaderResources(0, 1, nullSRV);

    // 深度ステート復元
    ctx->OMSetDepthStencilState(prevDS, prevRef);
    if (prevDS) prevDS->Release();

    if (vb) vb->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (layout) layout->Release();
}

void Renderer::DrawTextureRect(ID3D11ShaderResourceView* srv, int px, int py, int pw, int ph, bool flipY)
{
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* ctx = GetDeviceContext();
    if (!device || !ctx || !srv) return;

    // ピクセル座標 -> NDC に変換
    float l = 2.0f * (float)px / (float)SCREEN_WIDTH - 1.0f;
    float r = 2.0f * (float)(px + pw) / (float)SCREEN_WIDTH - 1.0f;
    float t = 1.0f - 2.0f * (float)py / (float)SCREEN_HEIGHT;
    float b = 1.0f - 2.0f * (float)(py + ph) / (float)SCREEN_HEIGHT;

    // テクスチャ座標 Y を flipY フラグで反転する
    float vt = flipY ? 1.0f : 0.0f; // top texcoord
    float vb_tex = flipY ? 0.0f : 1.0f; // bottom texcoord (名前衝突を避けるため変更)

    VERTEX_3D quad[6] = {
        { { l,  t, 0.0f }, {0,0,0}, {1,1,1,1}, { 0.0f, vt } },
        { { r,  t, 0.0f }, {0,0,0}, {1,1,1,1}, { 1.0f, vt } },
        { { l,  b, 0.0f }, {0,0,0}, {1,1,1,1}, { 0.0f, vb_tex } },
        { { r,  t, 0.0f }, {0,0,0}, {1,1,1,1}, { 1.0f, vt } },
        { { r,  b, 0.0f }, {0,0,0}, {1,1,1,1}, { 1.0f, vb_tex } },
        { { l,  b, 0.0f }, {0,0,0}, {1,1,1,1}, { 0.0f, vb_tex } },
    };

    // 頂点バッファ作成
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = (UINT)sizeof(quad);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{ quad, 0, 0 };
    ID3D11Buffer* vb = nullptr;
    if (FAILED(device->CreateBuffer(&bd, &sd, &vb)) || !vb) return;

    // シェーダ取得（既存の unlitTextureVS/PS を利用）
    ID3D11VertexShader* vs = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11PixelShader* ps = nullptr;
    CreateVertexShader(&vs, &layout, "unlitTextureVS.cso");
    CreatePixelShader(&ps, "unlitTexturePS.cso");

    // 頂点は既に NDC なので行列は単位にする
    SetWorldMatrix(XMMatrixIdentity());
    SetViewMatrix(XMMatrixIdentity());
    SetProjectionMatrix(XMMatrixIdentity());

    // 深度無効化（オーバーレイ）
    ID3D11DepthStencilState* prevDS = nullptr;
    UINT prevRef = 0;
    ctx->OMGetDepthStencilState(&prevDS, &prevRef);
    ctx->OMSetDepthStencilState(m_DepthStateDisable, 0);

    // パイプラインセット
    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &srv);

    ctx->Draw(6, 0);

    // 後片付け
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    ctx->PSSetShaderResources(0, 1, nullSRV);
    ctx->OMSetDepthStencilState(prevDS, prevRef);
    if (prevDS) prevDS->Release();
    if (vb) vb->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (layout) layout->Release();
}


// ImGui フォント初期化
void Renderer::InitImGuiFonts()
{
    if (!ImGui::GetCurrentContext()) {
        OutputDebugStringA("Renderer::InitImGuiFonts: ImGui context is not created. Call ImGui::CreateContext() and backend Init first.\n");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const char* paths[] = {
        "C:\\Windows\\Fonts\\meiryo.ttc",
        "C:\\Windows\\Fonts\\meiryo.ttf",
        "C:\\Windows\\Fonts\\msgothic.ttc"
    };
    float fontSize = 16.0f;
    bool loaded = false;
    for (auto p : paths) {
        std::ifstream f(p, std::ios::binary);
        if (f.good()) {
#if (IMGUI_VERSION_NUM >= 17701)
            io.Fonts->AddFontFromFileTTF(p, fontSize, nullptr, io.Fonts->GetGlyphRangesJapanese());
#else
            io.Fonts->AddFontFromFileTTF(p, fontSize);
#endif
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        io.Fonts->AddFontDefault();
    }

    // DX11 バックエンド経由でフォントテクスチャを作成
    ImGui_ImplDX11_CreateDeviceObjects();
    OutputDebugStringA("Renderer::InitImGuiFonts: fonts initialized.\n");
}

// 追加実装: 未定義だったユーティリティ関数群

void Renderer::UnbindAllShaderResources()
{
	ID3D11DeviceContext* ctx = GetDeviceContext();
	if (!ctx) return;

	// SRV を複数スロットで解除（PS/VS/GS/CS）
	const UINT SRV_SLOTS = 16;
	ID3D11ShaderResourceView* nullSRVs[SRV_SLOTS] = {};
	ctx->PSSetShaderResources(0, SRV_SLOTS, nullSRVs);
	ctx->VSSetShaderResources(0, SRV_SLOTS, nullSRVs);
	ctx->GSSetShaderResources(0, SRV_SLOTS, nullSRVs);
	ctx->CSSetShaderResources(0, SRV_SLOTS, nullSRVs);

	// CS の UAV を解除
	const UINT UAV_SLOTS = 8;
	ID3D11UnorderedAccessView* nullUAVs[UAV_SLOTS] = {};
	ctx->CSSetUnorderedAccessViews(0, UAV_SLOTS, nullUAVs, nullptr);

	// PS サンプラー解除（念のため）
	ID3D11SamplerState* nullSamplers[SRV_SLOTS] = {};
	ctx->PSSetSamplers(0, SRV_SLOTS, nullSamplers);

	// シェーダ解除（念のため）
	ctx->PSSetShader(nullptr, nullptr, 0);
	ctx->VSSetShader(nullptr, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->CSSetShader(nullptr, nullptr, 0);
}

void Renderer::DebugCheckSRVStride(ID3D11ShaderResourceView* srv, const char* label, UINT expectedStride)
{
    char buf[512];
    if (!srv) {
        sprintf_s(buf, sizeof(buf), "DebugCheckSRVStride: %s -> NULL SRV (expected stride=%u)\n", label, expectedStride);
        OutputDebugStringA(buf);
        return;
    }

    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    if (!res) {
        sprintf_s(buf, sizeof(buf), "DebugCheckSRVStride: %s -> GetResource returned NULL (expected stride=%u)\n", label, expectedStride);
        OutputDebugStringA(buf);
        return;
    }

    ID3D11Buffer* bufRes = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Buffer), (void**)&bufRes);
    res->Release();
    if (FAILED(hr) || !bufRes) {
        sprintf_s(buf, sizeof(buf), "DebugCheckSRVStride: %s -> not a buffer resource (expected stride=%u)\n", label, expectedStride);
        OutputDebugStringA(buf);
        return;
    }

    D3D11_BUFFER_DESC desc;
    bufRes->GetDesc(&desc);
    bufRes->Release();

    sprintf_s(buf, sizeof(buf), "DebugCheckSRVStride: %s -> Stride=%u, ByteWidth=%u (expected %u)\n",
              label, desc.StructureByteStride, desc.ByteWidth, expectedStride);
    OutputDebugStringA(buf);
}