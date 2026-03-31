#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>
#include "gameObject.h"
#include "box.h" // OBB を参照するため追加

#pragma comment (lib, "d3d11.lib")

using namespace DirectX;

// 頂点構造体
struct VERTEX_3D
{
	XMFLOAT3 Position;
	XMFLOAT3 Normal;
	XMFLOAT4 Diffuse;
	XMFLOAT2 TexCoord;
};

// マテリアル構造体
struct MATERIAL
{
	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;
	XMFLOAT4 Emission;
	float    Shininess;
	BOOL     TextureEnable;
	float    Dummy[2];
};

// ライト構造体
struct LIGHT
{
	BOOL     Enable;
	float    pad[3];
	XMFLOAT4 Direction;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Ambient;
};

class Renderer
{
private:
	// --- 静的メンバー変数の実体宣言 ---
	static D3D_FEATURE_LEVEL       m_FeatureLevel;

	// --- D3D11コアオブジェクト ---
	static ID3D11Device* m_Device;
	static ID3D11DeviceContext* m_DeviceContext;
	static IDXGISwapChain* m_SwapChain;
	static ID3D11RenderTargetView* m_RenderTargetView;
	static ID3D11DepthStencilView* m_DepthStencilView;

	// --- 定数バッファ ---
	static ID3D11Buffer* m_WorldBuffer;
	static ID3D11Buffer* m_ViewBuffer;
	static ID3D11Buffer* m_ProjectionBuffer;
	static ID3D11Buffer* m_MaterialBuffer;
	static ID3D11Buffer* m_LightBuffer;

	// --- コンピュートシェーダー関連リソース ---
	static ID3D11Buffer* m_particleBufferRead;
	static ID3D11Buffer* m_particleBufferWrite;
	static ID3D11ShaderResourceView* m_particleSRV;
	static ID3D11UnorderedAccessView* m_particleUAV;

	// --- ステートオブジェクト ---
	static ID3D11DepthStencilState* m_DepthStateEnable;
	static ID3D11DepthStencilState* m_DepthStateDisable;
	static ID3D11BlendState* m_BlendState;
	static ID3D11BlendState* m_BlendStateATC;
	static ID3D11RasterizerState* m_RasterizerState; // 追加
	static ID3D11SamplerState* m_SamplerState;       // 追加

	// --- デフォルト（フォールバック）シェーダ ---
	static ID3D11VertexShader* m_DefaultVS;
	static ID3D11InputLayout*  m_DefaultInputLayout;
	static ID3D11PixelShader*  m_DefaultPS;

public:
	// --- 初期化・解放 ---
	static void Init();
	static void Uninit();

	// --- 描画 ---
	static void Begin();
	static void End();

	static void SetWorldMatrix(const XMMATRIX& WorldMatrix);
	static void SetViewMatrix(const XMMATRIX& ViewMatrix);
	static void SetProjectionMatrix(const XMMATRIX& ProjectionMatrix);
	static void SetMaterial(const MATERIAL& material);
	static void SetLight(const LIGHT& light);

	static void SetDepthEnable(bool Enable);
	static void SetATCEnable(bool Enable);
	static void SetWorldViewProjection2D();

	// --- シェーダー作成ヘルパー ---
	static void CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName);
	static void CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName, const D3D11_INPUT_ELEMENT_DESC* layout, UINT numElements);
	static void CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);
	static void CreateComputeShader(ID3D11ComputeShader** ppCS, const char* pFileName);

	// --- コンピュートシェーダー関連メソッド ---
	static bool CreateParticleBuffers(size_t particleCount, size_t particleStride, void* initialData);
	static void ReleaseParticleBuffers();
	static void DispatchComputeShader(const char* shaderFileName, UINT x, UINT y, UINT z);

	// --- ゲッター ---
	static ID3D11Device* GetDevice() { return m_Device; }
	static ID3D11DeviceContext* GetDeviceContext() { return m_DeviceContext; }
	static ID3D11Buffer* GetParticleBuffer() { return m_particleBufferRead; }

	// リサイズ対応：バックバッファ/DSV を再作成する
	static void Resize(UINT width, UINT height);

	// 追加: すべてのシェーダリソース / UAV をヌルバインドするユーティリティ
	static void UnbindAllShaderResources();

	// デバッグ: OBB をワイヤーで描画する (色はRGBA)
	static void DrawWireOBB(const OBB& obb, const XMFLOAT4& color);
	// フルスクリーンに SRV を描画するユーティリティ（簡易実装）
	static void DrawFullScreenTexture(ID3D11ShaderResourceView* srv);

	// フレーム内で作成したデフォルトシェーダ／入力レイアウトを再バインドするユーティリティ
	static void BindDefaultShaders();
	public:
    static void DebugCheckSRVStride(ID3D11ShaderResourceView* srv, const char* label, UINT expectedStride);

	// 追加: Renderer クラスの public セクション内に宣言を挿入してください
	static void DrawTextureRect(ID3D11ShaderResourceView* srv, int px, int py, int pw, int ph, bool flipY = true);

	// 追加（Renderer クラスの public セクションに入れてください）
	static void InitImGuiFonts(); // ImGui コンテキスト初期化後にフォントを登録する（Windows の日本語フォントを探して登録）
};