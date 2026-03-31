#include "main.h"
#include "box.h"
#include "renderer.h"
#include <cmath>
#include <algorithm>

// === OBB実装 ===

bool OBB::Contains(const Vector3& point) const
{
    Vector3 d = point - center;
    
    for (int i = 0; i < 3; ++i)
    {
        float dist = d.x * axes[i].x + d.y * axes[i].y + d.z * axes[i].z;
        float halfSize_i = (i == 0) ? halfSize.x : (i == 1) ? halfSize.y : halfSize.z;
        
        if (std::abs(dist) > halfSize_i)
            return false;
    }
    return true;
}

Vector3 OBB::ClosestPoint(const Vector3& point) const
{
    Vector3 d = point - center;
    Vector3 result = center;
    
    float halfSizes[3] = { halfSize.x, halfSize.y, halfSize.z };
    
    for (int i = 0; i < 3; ++i)
    {
        float dist = d.x * axes[i].x + d.y * axes[i].y + d.z * axes[i].z;
        dist = std::max(-halfSizes[i], std::min(dist, halfSizes[i]));
        result.x += dist * axes[i].x;
        result.y += dist * axes[i].y;
        result.z += dist * axes[i].z;
    }
    
    return result;
}

bool OBB::IntersectsSphere(const Vector3& sphereCenter, float sphereRadius) const
{
    Vector3 closest = ClosestPoint(sphereCenter);
    Vector3 diff = sphereCenter - closest;
    float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    return distSq <= sphereRadius * sphereRadius;
}

// 分離軸定理によるOBB同士の衝突判定
bool OBB::Intersects(const OBB& other) const
{
    // 15本の分離軸をテスト
    Vector3 testAxes[15];
    
    // 各OBBの3軸
    for (int i = 0; i < 3; ++i)
    {
        testAxes[i] = axes[i];
        testAxes[i + 3] = other.axes[i];
    }
    
    // 軸の外積（9本）
    int idx = 6;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            testAxes[idx].x = axes[i].y * other.axes[j].z - axes[i].z * other.axes[j].y;
            testAxes[idx].y = axes[i].z * other.axes[j].x - axes[i].x * other.axes[j].z;
            testAxes[idx].z = axes[i].x * other.axes[j].y - axes[i].y * other.axes[j].x;
            ++idx;
        }
    }
    
    Vector3 d = other.center - center;
    float thisHalfSizes[3] = { halfSize.x, halfSize.y, halfSize.z };
    float otherHalfSizes[3] = { other.halfSize.x, other.halfSize.y, other.halfSize.z };
    
    for (int i = 0; i < 15; ++i)
    {
        Vector3& axis = testAxes[i];
        
        // 軸の長さが0に近い場合はスキップ
        float axisLenSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (axisLenSq < 0.0001f)
            continue;
        
        // 軸を正規化
        float axisLen = std::sqrt(axisLenSq);
        axis.x /= axisLen;
        axis.y /= axisLen;
        axis.z /= axisLen;
        
        // 各OBBの投影半径を計算
        float ra = 0.0f;
        float rb = 0.0f;
        
        for (int j = 0; j < 3; ++j)
        {
            float dotA = std::abs(axes[j].x * axis.x + axes[j].y * axis.y + axes[j].z * axis.z);
            ra += thisHalfSizes[j] * dotA;
            
            float dotB = std::abs(other.axes[j].x * axis.x + other.axes[j].y * axis.y + other.axes[j].z * axis.z);
            rb += otherHalfSizes[j] * dotB;
        }
        
        // 中心間の距離を軸に投影
        float dist = std::abs(d.x * axis.x + d.y * axis.y + d.z * axis.z);
        
        // 分離軸が見つかった場合、衝突していない
        if (dist > ra + rb)
            return false;
    }
    
    // すべての軸で重なっている = 衝突
    return true;
}

// === Box実装 ===

void Box::Init()
{
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    Renderer::CreateVertexShader(&mVertexShader, &mVertexLayout, "unlitTextureVS.cso", layout, ARRAYSIZE(layout));
    Renderer::CreatePixelShader(&mPixelShader, "unlitTexturePS.cso");

    CreateVertexBuffer();
}

void Box::Uninit()
{
    if (mVertexBuffer) { mVertexBuffer->Release(); mVertexBuffer = nullptr; }
    if (mIndexBuffer) { mIndexBuffer->Release(); mIndexBuffer = nullptr; }
    if (mVertexLayout) { mVertexLayout->Release(); mVertexLayout = nullptr; }
    if (mVertexShader) { mVertexShader->Release(); mVertexShader = nullptr; }
    if (mPixelShader) { mPixelShader->Release(); mPixelShader = nullptr; }
}

void Box::Update()
{
    if (mNeedsRebuild)
    {
        if (mVertexBuffer) { mVertexBuffer->Release(); mVertexBuffer = nullptr; }
        if (mIndexBuffer) { mIndexBuffer->Release(); mIndexBuffer = nullptr; }
        CreateVertexBuffer();
        mNeedsRebuild = false;
    }
}

void Box::Draw()
{
    DirectX::XMMATRIX world = DirectX::XMMatrixScaling(mScale.x * mSize.x, mScale.y * mSize.y, mScale.z * mSize.z);
    world *= DirectX::XMMatrixRotationRollPitchYaw(mRotation.x, mRotation.y, mRotation.z);
    world *= DirectX::XMMatrixTranslation(mPosition.x, mPosition.y, mPosition.z);
    Renderer::SetWorldMatrix(world);

    ID3D11DeviceContext* dc = Renderer::GetDeviceContext();
    dc->IASetInputLayout(mVertexLayout);
    dc->VSSetShader(mVertexShader, nullptr, 0);
    dc->PSSetShader(mPixelShader, nullptr, 0);

    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
    dc->IASetIndexBuffer(mIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    MATERIAL mat;
    mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    mat.TextureEnable = false;
    Renderer::SetMaterial(mat);

    if (mWireframe)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_WIREFRAME;
        rd.CullMode = D3D11_CULL_NONE;
        ID3D11RasterizerState* rs = nullptr;
        Renderer::GetDevice()->CreateRasterizerState(&rd, &rs);
        dc->RSSetState(rs);
        
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(INDEX_COUNT, 0, 0);
        
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        Renderer::GetDevice()->CreateRasterizerState(&rd, &rs);
        dc->RSSetState(rs);
        rs->Release();
    }
    else
    {
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(INDEX_COUNT, 0, 0);
    }

    // 頂点シェーダー・入力レイアウト・ピクセルシェーダーを必ず再セット
    Renderer::GetDeviceContext()->IASetInputLayout(mVertexLayout);
    Renderer::GetDeviceContext()->VSSetShader(mVertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(mPixelShader, NULL, 0);

    Renderer::DrawWireOBB(GetOBB(), XMFLOAT4(0.3f, 0.3f, 0.8f, 1.0f));
}

void Box::SetColor(const Vector3& color)
{
    mColor = color;
    mNeedsRebuild = true;
}

OBB Box::GetOBB() const
{
    OBB obb;
    obb.center = mPosition;
    obb.halfSize = Vector3(
        mSize.x * mScale.x * 0.5f,
        mSize.y * mScale.y * 0.5f,
        mSize.z * mScale.z * 0.5f
    );
    
    // 回転行列から軸を取得
    DirectX::XMMATRIX rotMat = DirectX::XMMatrixRotationRollPitchYaw(mRotation.x, mRotation.y, mRotation.z);
    
    DirectX::XMFLOAT3 right, up, forward;
    DirectX::XMStoreFloat3(&right, rotMat.r[0]);
    DirectX::XMStoreFloat3(&up, rotMat.r[1]);
    DirectX::XMStoreFloat3(&forward, rotMat.r[2]);
    
    obb.axes[0] = Vector3(right.x, right.y, right.z);
    obb.axes[1] = Vector3(up.x, up.y, up.z);
    obb.axes[2] = Vector3(forward.x, forward.y, forward.z);
    
    return obb;
}

bool Box::CollidesWith(const Box& other) const
{
    return GetOBB().Intersects(other.GetOBB());
}

bool Box::Contains(const Vector3& point) const
{
    return GetOBB().Contains(point);
}

bool Box::CollidesWithSphere(const Vector3& sphereCenter, float sphereRadius) const
{
    return GetOBB().IntersectsSphere(sphereCenter, sphereRadius);
}

void Box::CreateVertexBuffer()
{
    ID3D11Device* device = Renderer::GetDevice();

    VERTEX_3D vertices[VERTEX_COUNT] = {};
    DirectX::XMFLOAT4 color = { mColor.x, mColor.y, mColor.z, 1.0f };

    // 前面 (Z-)
    vertices[0] = { {-0.5f, -0.5f, -0.5f}, {0, 0, -1}, color, {0, 1} };
    vertices[1] = { {-0.5f,  0.5f, -0.5f}, {0, 0, -1}, color, {0, 0} };
    vertices[2] = { { 0.5f,  0.5f, -0.5f}, {0, 0, -1}, color, {1, 0} };
    vertices[3] = { { 0.5f, -0.5f, -0.5f}, {0, 0, -1}, color, {1, 1} };

    // 後面 (Z+)
    vertices[4] = { { 0.5f, -0.5f,  0.5f}, {0, 0, 1}, color, {0, 1} };
    vertices[5] = { { 0.5f,  0.5f,  0.5f}, {0, 0, 1}, color, {0, 0} };
    vertices[6] = { {-0.5f,  0.5f,  0.5f}, {0, 0, 1}, color, {1, 0} };
    vertices[7] = { {-0.5f, -0.5f,  0.5f}, {0, 0, 1}, color, {1, 1} };

    // 上面 (Y+)
    vertices[8]  = { {-0.5f,  0.5f, -0.5f}, {0, 1, 0}, color, {0, 1} };
    vertices[9]  = { {-0.5f,  0.5f,  0.5f}, {0, 1, 0}, color, {0, 0} };
    vertices[10] = { { 0.5f,  0.5f,  0.5f}, {0, 1, 0}, color, {1, 0} };
    vertices[11] = { { 0.5f,  0.5f, -0.5f}, {0, 1, 0}, color, {1, 1} };

    // 下面 (Y-)
    vertices[12] = { {-0.5f, -0.5f,  0.5f}, {0, -1, 0}, color, {0, 1} };
    vertices[13] = { {-0.5f, -0.5f, -0.5f}, {0, -1, 0}, color, {0, 0} };
    vertices[14] = { { 0.5f, -0.5f, -0.5f}, {0, -1, 0}, color, {1, 0} };
    vertices[15] = { { 0.5f, -0.5f,  0.5f}, {0, -1, 0}, color, {1, 1} };

    // 左面 (X-)
    vertices[16] = { {-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, color, {0, 1} };
    vertices[17] = { {-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, color, {0, 0} };
    vertices[18] = { {-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, color, {1, 0} };
    vertices[19] = { {-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, color, {1, 1} };

    // 右面 (X+)
    vertices[20] = { { 0.5f, -0.5f, -0.5f}, {1, 0, 0}, color, {0, 1} };
    vertices[21] = { { 0.5f,  0.5f, -0.5f}, {1, 0, 0}, color, {0, 0} };
    vertices[22] = { { 0.5f,  0.5f,  0.5f}, {1, 0, 0}, color, {1, 0} };
    vertices[23] = { { 0.5f, -0.5f,  0.5f}, {1, 0, 0}, color, {1, 1} };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(VERTEX_3D) * VERTEX_COUNT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = vertices;
    device->CreateBuffer(&bd, &sd, &mVertexBuffer);

    WORD indices[INDEX_COUNT] = {
        0, 1, 2,  0, 2, 3,
        4, 5, 6,  4, 6, 7,
        8, 9, 10,  8, 10, 11,
        12, 13, 14,  12, 14, 15,
        16, 17, 18,  16, 18, 19,
        20, 21, 22,  20, 22, 23,
    };

    bd.ByteWidth = sizeof(WORD) * INDEX_COUNT;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = indices;
    device->CreateBuffer(&bd, &sd, &mIndexBuffer);
}