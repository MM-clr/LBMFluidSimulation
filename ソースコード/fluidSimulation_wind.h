#pragma once
#include "vector3.h"

class FluidSimulation;

namespace FluidWindUI
{
    // ImGui で風を調整して FluidSimulation に反映するヘルパ
    // 呼び出し: FluidWindUI::DrawWindUI(simPtr)  （simPtr は FluidSimulation*）
    void DrawWindUI(FluidSimulation* sim);
}