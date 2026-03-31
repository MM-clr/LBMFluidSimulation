#include "fluidSimulation.h"
#include "fluidSimulation_wind.h"
#include "renderer.h"
#include "ImGUI\imgui.h"
#include "TextEncoding.h"
#include "ImGuiManager.h"
#include <unordered_map>
#include <string>

// ラベルを一度だけ変換してキャッシュするヘルパ
static const std::string& LocalLabel(const wchar_t* w)
{
    static std::unordered_map<std::wstring, std::string> cache;
    std::wstring key(w);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::string utf = WideToUtf8(w);
    auto res = cache.emplace(std::move(key), std::move(utf));
    return res.first->second;
}

void FluidSimulation::DrawDebugUI()
{
    // シーン描画中に直接 ImGui を呼ばないよう、コールバックを登録する。
    // 登録されたコールバックは ImGuiManager::NewFrame() 後、ImGuiManager::Render() の内部で実行される。
    ImGuiManager::EnqueueGui([this]() {
        // ウィンドウ開始
        ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
        ImGui::Begin(LocalLabel(L"流体シミュレーション").c_str());

        // 粒子表示
        bool showParticles = GetShowParticles();
        if (ImGui::Checkbox(LocalLabel(L"粒子表示").c_str(), &showParticles)) {
            SetShowParticles(showParticles);
        }

        // 衝突可視化
        bool visColl = mDebugVisualizeCollisions;
        if (ImGui::Checkbox(LocalLabel(L"衝突可視化").c_str(), &visColl)) {
            SetDebugVisualizeCollisions(visColl);
        }

        // LBM スライス表示
        bool showSlice = GetShowLbmSlice();
        if (ImGui::Checkbox(LocalLabel(L"LBM スライス表示").c_str(), &showSlice)) {
            SetShowLbmSlice(showSlice);
        }

        // スライス軸選択（X/Y/Z）とスライスインデックス
        {
            int axis = static_cast<int>(GetLbmSliceAxis());
            const char* axisItems[] = {
                LocalLabel(L"X 平面").c_str(),
                LocalLabel(L"Y 平面").c_str(),
                LocalLabel(L"Z 平面").c_str()
            };
            if (ImGui::Combo(LocalLabel(L"スライス軸").c_str(), &axis, axisItems, IM_ARRAYSIZE(axisItems))) {
                SetLbmSliceAxis(static_cast<UINT>(axis));
            }

            int maxIndex = 0;
            if (axis == FluidSimulation::AxisX) maxIndex = static_cast<int>(FluidSimulation::NX) - 1;
            else if (axis == FluidSimulation::AxisY) maxIndex = static_cast<int>(FluidSimulation::NY) - 1;
            else maxIndex = static_cast<int>(FluidSimulation::NZ) - 1;
            if (maxIndex < 0) maxIndex = 0;

            const char* sliderLabels[] = {
                LocalLabel(L"LBM スライス (X)").c_str(),
                LocalLabel(L"LBM スライス (Y)").c_str(),
                LocalLabel(L"LBM スライス (Z)").c_str()
            };

            int slice = static_cast<int>(GetLbmSliceIndex());
            if (slice < 0) slice = 0;
            if (slice > maxIndex) slice = maxIndex;

            if (ImGui::SliderInt(sliderLabels[axis], &slice, 0, maxIndex)) {
                SetLbmSliceIndex(slice >= 0 ? static_cast<UINT>(slice) : 0u);
            }

            // 追加: 可視化モード選択 (密度 / 速度大きさ / 速度ベクトル)
            int vizMode = static_cast<int>(GetLbmVizMode());
            const char* vizItems[] = {
                LocalLabel(L"密度 (rho)").c_str(),
                LocalLabel(L"速度大きさ").c_str(),
                LocalLabel(L"速度ベクトル").c_str()
            };
            if (ImGui::Combo(LocalLabel(L"可視化モード").c_str(), &vizMode, vizItems, IM_ARRAYSIZE(vizItems))) {
                SetLbmVizMode(static_cast<UINT>(vizMode));
            }

            // 追加: 粒子をLBM速度で駆動するためのブレンド設定とボタン
            static float driveBlend = 1.0f;
            if (ImGui::SliderFloat(LocalLabel(L"粒子駆動ブレンド (0..1)").c_str(), &driveBlend, 0.0f, 1.0f, "%.3f")) {
                // 値はボタンで適用
            }

            ImGui::BeginGroup();
            if (ImGui::Button(LocalLabel(L"可視化実行").c_str())) {
                // デバッグログ：ボタン押下を確実に確認する
                {
                    char dbg[256];
                    sprintf_s(dbg, sizeof(dbg), "ImGui: Visualize button clicked. slice=%u axis=%u mode=%u\n", GetLbmSliceIndex(), (unsigned)mLbmSliceAxis, (unsigned)mLbmVizMode);
                    OutputDebugStringA(dbg);
                }

                // 可視化を実行（GPU に仕事を投げる）
                VisualizeLBMSlice(GetLbmSliceIndex());

                // GPU のコマンドを確実にフラッシュして ImGui プレビューが即座に更新されるようにする
                ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
                if (ctx) ctx->Flush();

                // プレビュー表示がオフなら自動でオンにして即座にプレビューを見せる
                if (!GetShowLbmSlice()) {
                    SetShowLbmSlice(true);
                }

                // 追加デバッグ：現在の m_pLbmVizSRV とサイズを出力
                {
                    char dbg2[256];
                    sprintf_s(dbg2, sizeof(dbg2), "ImGui: after VisualizeLBMSlice m_pLbmVizSRV=%p size=%u x %u mode=%u\n",
                              (void*)m_pLbmVizSRV, (unsigned)m_vizWidth, (unsigned)m_vizHeight, (unsigned)mLbmVizMode);
                    OutputDebugStringA(dbg2);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(LocalLabel(L"粒子を駆動 (CPU)").c_str())) {
                DriveParticlesWithLbmVel(driveBlend);
            }
            ImGui::SameLine();
            if (ImGui::Button(LocalLabel(L"粒子を駆動 (GPU)").c_str())) {
                DriveParticlesWithLbmVelGPU(driveBlend);
            }
            ImGui::EndGroup();
        }

        ImGui::Separator();

        // 風設定（既存の Wind UI を呼ぶ）
        if (ImGui::CollapsingHeader(LocalLabel(L"風設定").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild("##WindControlsChild", ImVec2(0, 140), true, ImGuiWindowFlags_NoMove);
            FluidWindUI::DrawWindUI(this);
            ImGui::EndChild();

            // --- 追加: セグメント風 (2点間を一定速度で移動する風) の GUI 編集 ---
            ImGui::Separator();
            ImGui::TextUnformatted(LocalLabel(L"セグメント風 (Segment Wind)").c_str());
            bool segEnabled = IsSegmentWindEnabled();
            if (ImGui::Checkbox(LocalLabel(L"セグメント風を有効にする").c_str(), &segEnabled)) {
                EnableSegmentWind(segEnabled);
            }

            float p0arr[3] = { mSegmentWindP0.x, mSegmentWindP0.y, mSegmentWindP0.z };
            if (ImGui::InputFloat3(LocalLabel(L"P0 (ワールド座標)").c_str(), p0arr, "%.3f")) {
                mSegmentWindP0.x = p0arr[0];
                mSegmentWindP0.y = p0arr[1];
                mSegmentWindP0.z = p0arr[2];
            }
            float p1arr[3] = { mSegmentWindP1.x, mSegmentWindP1.y, mSegmentWindP1.z };
            if (ImGui::InputFloat3(LocalLabel(L"P1 (ワールド座標)").c_str(), p1arr, "%.3f")) {
                mSegmentWindP1.x = p1arr[0];
                mSegmentWindP1.y = p1arr[1];
                mSegmentWindP1.z = p1arr[2];
            }

            float speed = mSegmentWindSpeed;
            if (ImGui::DragFloat(LocalLabel(L"速度 (world units/sec)").c_str(), &speed, 0.01f, 0.0f, 1000.0f, "%.3f")) {
                mSegmentWindSpeed = speed;
            }

            float radius = mSegmentWindRadius;
            if (ImGui::DragFloat(LocalLabel(L"影響半径 (world)").c_str(), &radius, 0.01f, 0.0f, 1000.0f, "%.3f")) {
                mSegmentWindRadius = radius;
            }

            float strength = mSegmentWindStrength;
            if (ImGui::DragFloat(LocalLabel(L"強度 (0..1 を想定)").c_str(), &strength, 0.001f, -10.0f, 10.0f, "%.4f")) {
                mSegmentWindStrength = strength;
            }

            bool loop = mSegmentWindLoop;
            if (ImGui::Checkbox(LocalLabel(L"ループ再生 (折り返し)").c_str(), &loop)) {
                mSegmentWindLoop = loop;
            }

            float prog = mSegmentWindProgress;
            if (ImGui::SliderFloat(LocalLabel(L"進行度 (0..1)").c_str(), &prog, 0.0f, 1.0f, "%.3f")) {
                mSegmentWindProgress = prog < 0.0f ? 0.0f : (prog > 1.0f ? 1.0f : prog);
            }

            ImGui::BeginGroup();
            if (ImGui::Button(LocalLabel(L"適用 (SetSegmentWind)").c_str())) {
                SetSegmentWind(mSegmentWindP0, mSegmentWindP1, mSegmentWindSpeed, mSegmentWindRadius, mSegmentWindStrength, mSegmentWindLoop);
                if (!segEnabled) EnableSegmentWind(false);
            }
            ImGui::SameLine();
            if (ImGui::Button(LocalLabel(L"クリア (Clear)").c_str())) {
                ClearSegmentWind();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(LocalLabel(L"開始/停止切替").c_str())) {
                EnableSegmentWind(!IsSegmentWindEnabled());
            }
            ImGui::EndGroup();

            ImGui::TextUnformatted(LocalLabel(L"注: P0/P1 はワールド座標で指定します。進行度は 0..1。").c_str());
            ImGui::Separator();
        }

        ImGui::Separator();

        // LBM Force Settings
        if (ImGui::CollapsingHeader(LocalLabel(L"LBM 力設定").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            float forceScale = GetForceScale();
            if (ImGui::DragFloat(LocalLabel(L"力のスケール").c_str(), &forceScale, 0.1f, 0.0f, 100.0f, "%.3f")) {
                SetForceScale(forceScale);
            }

            float maxLbmSpeed = GetMaxLbmSpeed();
            if (ImGui::DragFloat(LocalLabel(L"最大 LBM 速度").c_str(), &maxLbmSpeed, 0.001f, 0.0f, 1.0f, "%.4f")) {
                SetMaxLbmSpeed(maxLbmSpeed);
            }

            bool enableGravity = IsGravityEnabled();
            if (ImGui::Checkbox(LocalLabel(L"重力を有効にする").c_str(), &enableGravity)) {
                SetEnableGravity(enableGravity);
            }

            Vector3 g = GetGravity();
            float gravArr[3] = { g.x, g.y, g.z };
            if (ImGui::DragFloat3(LocalLabel(L"重力ベクトル (x, y, z)").c_str(), gravArr, 0.1f, -100.0f, 100.0f, "%.3f")) {
                Vector3 newG(gravArr[0], gravArr[1], gravArr[2]);
                SetGravity(newG);
            }

            // 境界モードの選択 (periodic / wall)
            int bmode = static_cast<int>(GetBoundaryMode());
            if (bmode > 1) {
                bmode = 1;
                SetBoundaryModeAndRecreate(static_cast<UINT>(bmode));
            }
            const std::string bmodeItemWrap = LocalLabel(L"周期境界 (wrap)");
            const std::string bmodeItemWall = LocalLabel(L"壁: バウンスバック (wall)");
            const char* bmodeItems[] = {
                bmodeItemWrap.c_str(),
                bmodeItemWall.c_str()
            };
            if (ImGui::Combo(LocalLabel(L"LBM 境界モード").c_str(), &bmode, bmodeItems, IM_ARRAYSIZE(bmodeItems))) {
                // Use SetBoundaryModeAndRecreate to force the LBM CS to be reloaded (runtime compile path)
                SetBoundaryModeAndRecreate(static_cast<UINT>(bmode));
                // Debug log to ensure GUI change reaches simulation
                char lb[128]; sprintf_s(lb, sizeof(lb), "ImGui: SetBoundaryModeAndRecreate -> %d\n", bmode);
                OutputDebugStringA(lb);
            }
        }

        if (ImGui::CollapsingHeader(LocalLabel(L"Smagorinsky (SGS)").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            float Cs = 0.0f, delta = 0.0f, tauMin = 0.0f, tauMax = 0.0f;
            GetSmagorinskyParams(Cs, delta, tauMin, tauMax);

            bool changed = false;
            if (ImGui::DragFloat(LocalLabel(L"Smagorinsky 定数 (Cs)").c_str(), &Cs, 0.01f, 0.0f, 1.0f, "%.3f")) {
                changed = true;
            }
            if (ImGui::DragFloat(LocalLabel(L"フィルタ幅 (delta)").c_str(), &delta, 0.1f, 1e-6f, 100.0f, "%.4f")) {
                changed = true;
            }
            if (ImGui::DragFloat(LocalLabel(L"tau 粘性 下限").c_str(), &tauMin, 0.001f, 0.5001f, 100.0f, "%.4f")) {
                changed = true;
            }
            if (ImGui::DragFloat(LocalLabel(L"tau 粘性 上限").c_str(), &tauMax, 0.1f, 0.501f, 1e6f, "%.4f")) {
                changed = true;
            }

            if (tauMin < 0.5001f) tauMin = 0.5001f;
            if (tauMax < tauMin) tauMax = tauMin;

            if (changed) {
                SetSmagorinskyParams(Cs, delta, tauMin, tauMax);
            }

            ImGui::TextUnformatted(LocalLabel(L"注: パラメータ変更は安定性に影響します。小さめの Cs と tau 下限を推奨。").c_str());
        }

        ImGui::Separator();

        // 局所風源 (Local Winds) 編集 UI
        if (ImGui::CollapsingHeader(LocalLabel(L"局所風源 (Local Winds)").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            {
                int mode = static_cast<int>(GetLocalWindMode());
                const char* modeItems[] = {
                    LocalLabel(L"線形 (linear)").c_str(),
                    LocalLabel(L"smoothstep").c_str(),
                    LocalLabel(L"ガウス (gaussian)").c_str()
                };
                if (ImGui::Combo(LocalLabel(L"減衰モード").c_str(), &mode, modeItems, IM_ARRAYSIZE(modeItems))) {
                    SetLocalWindMode(static_cast<UINT>(mode));
                }

                float sigma = GetLocalWindSigma();
                if (ImGui::DragFloat(LocalLabel(L"ガウス σ (grid 単位)").c_str(), &sigma, 0.1f, 0.001f, 100.0f, "%.3f")) {
                    if (sigma < 1e-4f) sigma = 1e-4f;
                    SetLocalWindSigma(sigma);
                }

                ImGui::TextUnformatted(LocalLabel(L"注: ガウス選択時は σ が減衰幅に影響します。small → 急峻").c_str());
                ImGui::Separator();
            }

            ImGui::PushID("NewLocalWind");
            static float inp_center[3] = { FluidSimulation::NX * 0.5f, FluidSimulation::NY * 0.5f, FluidSimulation::NZ * 0.5f };
            static float inp_radius = 5.0f;
            static float inp_dir[3] = { 1.0f, 0.0f, 0.0f };
            static float inp_strength = 0.01f;

            ImGui::TextUnformatted(LocalLabel(L"新規局所風源を追加").c_str());
            ImGui::InputFloat3(LocalLabel(L"中心 (格子座標)").c_str(), inp_center, "%.3f");
            ImGui::InputFloat(LocalLabel(L"影響半径 (grid)").c_str(), &inp_radius, 0.1f, 1.0f, "%.3f");
            ImGui::InputFloat3(LocalLabel(L"方向ベクトル").c_str(), inp_dir, "%.3f");
            ImGui::InputFloat(LocalLabel(L"強度 (strength)").c_str(), &inp_strength, 0.001f, 0.01f, "%.6f");

            ImGui::TextUnformatted(LocalLabel(L"注: center は LBM グリッド座標系で指定してください。").c_str());

            if (ImGui::Button(LocalLabel(L"局所風源を追加").c_str())) {
                LocalWind lw;
                lw.center.x = inp_center[0];
                lw.center.y = inp_center[1];
                lw.center.z = inp_center[2];
                lw.radius = inp_radius > 0.0f ? inp_radius : 0.0f;
                float dx = inp_dir[0], dy = inp_dir[1], dz = inp_dir[2];
                float len = sqrtf(dx*dx + dy*dy + dz*dz);
                if (len < 1e-6f) { lw.direction.x = 1.0f; lw.direction.y = 0.0f; lw.direction.z = 0.0f; }
                else { lw.direction.x = dx/len; lw.direction.y = dy/len; lw.direction.z = dz/len; }
                lw.strength = inp_strength;

                if (mLocalWinds.size() < static_cast<size_t>(MAX_LOCAL_WINDS)) {
                    mLocalWinds.push_back(lw);
                    mNumLocalWinds = static_cast<UINT>(mLocalWinds.size());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(LocalLabel(L"全てクリア").c_str())) {
                ClearLocalWinds();
                mLocalWinds.clear();
                mNumLocalWinds = 0;
            }
            ImGui::PopID();

            ImGui::Separator();

            ImGui::TextUnformatted(LocalLabel(L"登録済み局所風源").c_str());
            static int selectedLocalWind = -1;
            if (mLocalWinds.empty()) {
                ImGui::TextUnformatted(LocalLabel(L"（なし）").c_str());
                selectedLocalWind = -1;
            } else {
                ImGui::BeginChild("##LocalWindList", ImVec2(0, 150), true);
                for (int i = 0; i < (int)mLocalWinds.size(); ++i)
                {
                    char label[128];
                    const LocalWind& lw = mLocalWinds[i];
                    sprintf_s(label, sizeof(label), "ID %d: C=(%.1f,%.1f,%.1f) R=%.2f str=%.5f", i, lw.center.x, lw.center.y, lw.center.z, lw.radius, lw.strength);
                    if (ImGui::Selectable(label, selectedLocalWind == i)) {
                        selectedLocalWind = i;
                    }
                    ImGui::SameLine();
                    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                    char rmLabel[32];
                    sprintf_s(rmLabel, sizeof(rmLabel), "削除##lw%d", i);
                    if (ImGui::SmallButton(rmLabel)) {
                        mLocalWinds.erase(mLocalWinds.begin() + i);
                        mNumLocalWinds = static_cast<UINT>(mLocalWinds.size());
                        if (selectedLocalWind == i) selectedLocalWind = -1;
                        else if (selectedLocalWind > i) --selectedLocalWind;
                        --i;
                    }
                }
                ImGui::EndChild();
            }

            if (selectedLocalWind >= 0 && selectedLocalWind < (int)mLocalWinds.size()) {
                ImGui::Separator();
                ImGui::TextUnformatted(LocalLabel(L"選択中の局所風源編集").c_str());
                ImGui::PushID(selectedLocalWind);
                LocalWind& edit = mLocalWinds[selectedLocalWind];

                float centerArr[3] = { edit.center.x, edit.center.y, edit.center.z };
                if (ImGui::InputFloat3(LocalLabel(L"中心 (格子座標)").c_str(), centerArr, "%.3f")) {
                    edit.center.x = centerArr[0];
                    edit.center.y = centerArr[1];
                    edit.center.z = centerArr[2];
                }

                float radiusVal = edit.radius;
                if (ImGui::DragFloat(LocalLabel(L"影響半径 (grid)").c_str(), &radiusVal, 0.1f, 0.0f, 1000.0f, "%.3f")) {
                    edit.radius = radiusVal < 0.0f ? 0.0f : radiusVal;
                }

                float dirArr[3] = { edit.direction.x, edit.direction.y, edit.direction.z };
                if (ImGui::InputFloat3(LocalLabel(L"方向ベクトル").c_str(), dirArr, "%.3f")) {
                    float dx = dirArr[0], dy = dirArr[1], dz = dirArr[2];
                    float len = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (len < 1e-6f) { edit.direction.x = 1.0f; edit.direction.y = 0.0f; edit.direction.z = 0.0f; }
                    else { edit.direction.x = dx/len; edit.direction.y = dy/len; edit.direction.z = dz/len; }
                }

                float strengthVal = edit.strength;
                if (ImGui::DragFloat(LocalLabel(L"強度 (strength)").c_str(), &strengthVal, 0.0001f, -10.0f, 10.0f, "%.6f")) {
                    edit.strength = strengthVal;
                }

                ImGui::Separator();
                if (ImGui::Button(LocalLabel(L"コピー").c_str())) {
                    if (mLocalWinds.size() < static_cast<size_t>(MAX_LOCAL_WINDS)) {
                        mLocalWinds.push_back(edit);
                        mNumLocalWinds = static_cast<UINT>(mLocalWinds.size());
                        selectedLocalWind = static_cast<int>(mLocalWinds.size()) - 1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(LocalLabel(L"中心をグリッド中央へ").c_str())) {
                    edit.center.x = FluidSimulation::NX * 0.5f;
                    edit.center.y = FluidSimulation::NY * 0.5f;
                    edit.center.z = FluidSimulation::NZ * 0.5f;
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        // LBM スライス可視化プレビュー（上下反転の切替を追加）
        {
            static bool previewFlipY = false; // persistent across frames
            ImGui::Checkbox(LocalLabel(L"プレビュー Y 反転").c_str(), &previewFlipY);

            int previewW = 0, previewH = 0;
            if (m_pLbmVizSRV && m_vizWidth > 0 && m_vizHeight > 0) {
                previewW = (int)m_vizWidth;
                previewH = (int)m_vizHeight;
                ImVec2 uv0 = previewFlipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                ImVec2 uv1 = previewFlipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                ImGui::Image((void*)m_pLbmVizSRV, ImVec2((float)previewW, (float)previewH), uv0, uv1);
            } else {
                ImGui::TextUnformatted(LocalLabel(L"プレビューサイズが無効です。").c_str());
            }
        }

        ImGui::End();
    });
}