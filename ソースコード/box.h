#pragma once
#include "gameObject.h"

// OBB構造体
struct OBB
{
    Vector3 center;      // 中心座標
    Vector3 halfSize;    // 各軸の半分のサイズ
    Vector3 axes[3];     // 各軸の方向ベクトル（正規化済み）

    // 点がOBB内にあるかチェック
    bool Contains(const Vector3& point) const;
    
    // 他のOBBとの衝突判定
    bool Intersects(const OBB& other) const;
    
    // 球との衝突判定
    bool IntersectsSphere(const Vector3& sphereCenter, float sphereRadius) const;
    
    // 最近接点を取得
    Vector3 ClosestPoint(const Vector3& point) const;
};

class Box : public GameObject
{
public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    void SetSize(const Vector3& size) { mSize = size; }
    Vector3 GetSize() const { return mSize; }

    void SetColor(const Vector3& color);
    Vector3 GetColor() const { return mColor; }

    void SetWireframe(bool wireframe) { mWireframe = wireframe; }
    bool GetWireframe() const { return mWireframe; }

    // OBBを取得
    OBB GetOBB() const;

    // 他のBoxとの衝突判定
    bool CollidesWith(const Box& other) const;

    // 点との衝突判定
    bool Contains(const Vector3& point) const;

    // 球との衝突判定
    bool CollidesWithSphere(const Vector3& sphereCenter, float sphereRadius) const;

private:
    void CreateVertexBuffer();

    ID3D11Buffer* mVertexBuffer = nullptr;
    ID3D11Buffer* mIndexBuffer = nullptr;
    ID3D11InputLayout* mVertexLayout = nullptr;
    ID3D11VertexShader* mVertexShader = nullptr;
    ID3D11PixelShader* mPixelShader = nullptr;

    Vector3 mSize{ 1.0f, 1.0f, 1.0f };
    Vector3 mColor{ 1.0f, 1.0f, 1.0f };
    bool mWireframe = false;
    bool mNeedsRebuild = false;

    static const int VERTEX_COUNT = 24;
    static const int INDEX_COUNT = 36;
};