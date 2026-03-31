#include "main.h"
#include "renderer.h"
#include "camera.h"
#include "manager.h"
#include"input.h"
#include "scene.h"
#include "fluidSimulation.h"
#include "fluidcircle.h"
#include <vector>
#include <list>
#include <iterator>

float y = 2.0f;

float d = 7.0f;

void camera::Init()
{
    // 初期化処理
    mPosition = Vector3(0.0f, 1.0f, -5.0f);
    mRotation = Vector3(0.0f, 0.0f, 0.0f);
    mTarget = Vector3(0.0f, 0.0f, 0.0f);

    // 安全な near/far の既定値（near は 0 より大きく十分小さめに）
    mNear = 0.1f;
    mFar  = 100000.0f;
}

void camera::Uninit()
{
    // 終了処理
}

void camera::Update()
{
    // 更新処理
    if (Input::GetKeyPress(VK_LEFT))
    {
        mRotation.y += 0.1f;
    }
    if (Input::GetKeyPress(VK_RIGHT))
    {
        mRotation.y -= 0.1f;
    }
    if (Input::GetKeyPress(VK_UP))
    {
        d -= 0.1f;
    }
    if (Input::GetKeyPress(VK_DOWN))
    {
        d += 0.1f;
	}
    if (Input::GetKeyPress(VK_SPACE))
    {
        y += 0.1f;
    }
    if (Input::GetKeyPress(VK_LSHIFT))
    {
        y -= 0.1f;
    }
    FluidSimulation* fluidSimulation = Manager::GetScene()->GetGameObject<FluidSimulation>();
    FluidCircle* fluidCircle = Manager::GetScene()->GetGameObject<FluidCircle>();
    std::list<FluidCircle*> fluidCircles = Manager::GetScene()->GetGameObjects<FluidCircle>();
    
    //mTarget = fluidSimulation->GetBoundsCenter();
    //mTarget = fluidCircle->GetPosition();
	mTarget = { 0.0f,y,0.0f };
    mPosition = mTarget + Vector3(-sinf(mRotation.y), 0.0f, -cosf(mRotation.y)) * d;
    mPosition.y = y;
}

void camera::Draw()
{
    // Projection を camera の near/far で作成して Renderer に渡す
    mProjection = XMMatrixPerspectiveFovLH(1.0f, (float)SCREEN_WIDTH / SCREEN_HEIGHT, mNear, mFar);
    Renderer::SetProjectionMatrix(mProjection);

    // View 行列を計算して必ずセット（未セットだと描画で NaN が発生しやすい）
    XMFLOAT3 up = XMFLOAT3(0.0f, 1.0f, 0.0f);
    mView = XMMatrixLookAtLH(XMLoadFloat3((XMFLOAT3*)&mPosition), XMLoadFloat3((XMFLOAT3*)&mTarget), XMLoadFloat3(&up));
    Renderer::SetViewMatrix(mView);

    // デバッグログ: カメラ位置と near/far を確認
    {
        XMFLOAT4X4 projf;
        XMStoreFloat4x4(&projf, XMMatrixTranspose(mProjection));
        char buf[256];
        sprintf_s(buf, sizeof(buf), "Camera pos=(%f,%f,%f) near=%f far=%f proj[0][0]=%f\n", mPosition.x, mPosition.y, mPosition.z, mNear, mFar, projf._11);
        OutputDebugStringA(buf);
    }
    
}
