#pragma once
#include "gameObject.h"
#include <d3d11.h>
#include <DirectXMath.h>

using namespace DirectX;

class FluidCircle : public GameObject
{
public:
	void Init() override;
	void Uninit() override;
	void Update() override;
	void Draw() override;

	void SetColor(const XMFLOAT4& color) { mColor = color; }
    const XMFLOAT4& GetColor() const { return mColor; }

private:
	XMFLOAT4 mColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	int mFrameCount = 0;

    // インスタンシング移行により、以下のメンバーはFluidSimulationクラスで一元管理
    // ID3D11Buffer* mVertexBuffer = nullptr;
    // ID3D11VertexShader* mVertexShader = nullptr;
    // ID3D11PixelShader* mPixelShader = nullptr;
    // ID3D11InputLayout* mVertexLayout = nullptr;
};

