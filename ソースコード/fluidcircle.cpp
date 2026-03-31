#include "main.h"
#include "renderer.h"
#include "fluidcircle.h"
#include "manager.h"
#include "camera.h"
#include "scene.h"

// 円の分割数
const int kCircleSegments = 32;

void FluidCircle::Init()
{
    // インスタンシングで描画するため、個別のリソース作成は不要
}

void FluidCircle::Uninit()
{
    // インスタンシングで描画するため、個別のリソース解放は不要
}

void FluidCircle::Update()
{
    mFrameCount++;
}

void FluidCircle::Draw()
{
    // 描画は FluidSimulation::Draw() で一括して行う
}