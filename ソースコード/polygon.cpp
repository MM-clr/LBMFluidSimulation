
#include "main.h"
#include "renderer.h"
#include "polygon.h"
#include"input.h"
#include "texture.h"

void Polygon2D::Init(float x, float y, float Width, float Height, const char* FileName)
{
	VERTEX_3D vertex[4];

	vertex[0].Position = XMFLOAT3(x, y, 0.0f);
	vertex[0].Normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	vertex[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	vertex[0].TexCoord = XMFLOAT2(0.0f, 0.0f);

	vertex[1].Position = XMFLOAT3(Width, y, 0.0f);
	vertex[1].Normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	vertex[1].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	vertex[1].TexCoord = XMFLOAT2(1.0f, 0.0f);

	vertex[2].Position = XMFLOAT3(x, Height, 0.0f);
	vertex[2].Normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	vertex[2].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	vertex[2].TexCoord = XMFLOAT2(0.0, 1.0f);

	vertex[3].Position = XMFLOAT3(Width, Height, 0.0f);
	vertex[3].Normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	vertex[3].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	vertex[3].TexCoord = XMFLOAT2(1.0f, 1.0f);

	D3D11_BUFFER_DESC bd{};
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(VERTEX_3D) * 4;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = vertex;

	Renderer::GetDevice()->CreateBuffer(&bd, &sd, &mVertexBuffer);

	mTexture = Texture::Load(FileName);

	Renderer::CreateVertexShader(&mVertexShader, &mVertexLayout, "shaderunlitTextureVS.cso");

	Renderer::CreatePixelShader(&mPixelShader, "shaderunlitTexturePS.cso");


}

void Polygon2D::Uninit()
{

	if (mVertexBuffer) {
		mVertexBuffer->Release();
		mVertexBuffer = nullptr;
	}

	if (mVertexShader) {
		mVertexShader->Release();
		mVertexShader = nullptr;
	}
	if (mPixelShader) {
		mPixelShader->Release();
		mPixelShader = nullptr;
	}
	if (mVertexLayout) {
		mVertexLayout->Release();
		mVertexLayout = nullptr;
	}
}

void Polygon2D::Update()
{

}

void Polygon2D::Draw()
{
	//いる
	Renderer::GetDeviceContext()->IASetInputLayout(mVertexLayout);

	if (!mVertexShader)
	{
		Renderer::CreateVertexShader(&mVertexShader, &mVertexLayout, "shaderunlitTextureVS.cso");
	}
	if (!mPixelShader)
	{
		Renderer::CreatePixelShader(&mPixelShader, "shaderunlitTexturePS.cso");
	}

	Renderer::GetDeviceContext()->VSSetShader(mVertexShader, NULL, 0);
	Renderer::GetDeviceContext()->PSSetShader(mPixelShader, NULL, 0);

	Renderer::SetWorldViewProjection2D();
	XMMATRIX world, scale, rot, trans;
	scale = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	rot = XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f);
	trans = XMMatrixTranslation(0.0f, 0.0f, 0.0);
	world = scale * rot * trans;
	Renderer::SetWorldMatrix(world);
	MATERIAL mat;
	mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	mat.TextureEnable = true;
	Renderer::SetMaterial(mat);
	// 頂点バッファ設定
	UINT stride = sizeof(VERTEX_3D);
	UINT offset = 0;
	Renderer::GetDeviceContext()->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
	Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &mTexture);
	Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);


	Renderer::GetDeviceContext()->Draw(4, 0);

}


