#include "fluidSimulation_wind.h"
#include "fluidSimulation.h"
#include "ImGUI\imgui.h"
#include "TextEncoding.h" // 追加
#include <cmath>

// 単純な UI を提供。速度ベクトルを直接セットする。
namespace FluidWindUI
{
    void DrawWindUI(FluidSimulation* sim)
    {
        if (!sim) return;

        static bool enableWind = false;
        static float windVec[3] = { 0.0f, 0.0f, 0.0f };
        static float scale = 1.0f;

        // 時変風用 UI state
        static bool enableGust = false;
        static float gustAmplitude = 0.0f; // 相対振幅（例 0.5）
        static float gustFrequency = 0.5f; // Hz

        ImGui::Separator();
        auto title = WideToUtf8(L"LBM 風設定");
        ImGui::TextUnformatted(title.c_str());

        auto lblEnableWind = WideToUtf8(L"風を有効にする");
        ImGui::Checkbox(lblEnableWind.c_str(), &enableWind);

        // 初期化（最初の表示時に sim の値を読み込む）
        static bool initialized = false;
        if (!initialized) {
            Vector3 b = sim->GetWind();
            windVec[0] = b.x;
            windVec[1] = b.y;
            windVec[2] = b.z;
            // 既存のベース風が同じ変数でない可能性があるため SetWindBase を活用する
            sim->SetWindBase(Vector3(windVec[0] * scale, windVec[1] * scale, windVec[2] * scale));
            initialized = true;
        }

        auto lblBaseWind = WideToUtf8(L"基準風 (x, y, z)");
        ImGui::DragFloat3(lblBaseWind.c_str(), windVec, 0.01f, -100.0f, 100.0f, "%.3f");

        auto lblScale = WideToUtf8(L"スケール");
        ImGui::DragFloat(lblScale.c_str(), &scale, 0.01f, 0.0f, 100.0f, "%.3f");

        auto lblEnableGust = WideToUtf8(L"ガストを有効にする（時間変動）");
        if (ImGui::Checkbox(lblEnableGust.c_str(), &enableGust)) {
            sim->EnableTimeVaryingWind(enableGust);
        }

        ImGui::SameLine();
        auto lblSine = WideToUtf8(L"（正弦）");
        ImGui::TextDisabled(lblSine.c_str());

        auto lblGustAmp = WideToUtf8(L"ガスト振幅（相対）");
        if (ImGui::DragFloat(lblGustAmp.c_str(), &gustAmplitude, 0.01f, 0.0f, 10.0f, "%.3f")) {
            sim->SetWindGustParams(gustAmplitude, gustFrequency);
        }
        auto lblGustFreq = WideToUtf8(L"ガスト周波数（Hz）");
        if (ImGui::DragFloat(lblGustFreq.c_str(), &gustFrequency, 0.01f, 0.0f, 10.0f, "%.3f")) {
            sim->SetWindGustParams(gustAmplitude, gustFrequency);
        }

        // Apply base wind immediately when enabled
        if (enableWind) {
            Vector3 v;
            v.x = windVec[0] * scale;
            v.y = windVec[1] * scale;
            v.z = windVec[2] * scale;
            sim->SetWindBase(v);
            // シンプル実装: Enable/Disable 時に時変風フラグも反映
            sim->EnableTimeVaryingWind(enableGust);
            sim->SetWindGustParams(gustAmplitude, gustFrequency);
        } else {
            // 風を無効にしたい場合はベース風をゼロにする
            sim->SetWindBase(Vector3(0.0f, 0.0f, 0.0f));
            sim->EnableTimeVaryingWind(false);
        }
    }
}

void FluidSimulation::SetWindBase(const Vector3& baseWind)
{
    mWindBase = baseWind;
    if (!mEnableTimeVaryingWind) {
        mWind = mWindBase;
    }
}

Vector3 FluidSimulation::GetWindBase() const
{
    return mWindBase;
}

void FluidSimulation::EnableTimeVaryingWind(bool enable)
{
    mEnableTimeVaryingWind = enable;
    if (!mEnableTimeVaryingWind) {
        mWind = mWindBase;
    }
}

bool FluidSimulation::IsTimeVaryingWindEnabled() const
{
    return mEnableTimeVaryingWind;
}

void FluidSimulation::SetWindGustParams(float amplitude, float frequency)
{
    mWindGustAmplitude = amplitude;
    mWindGustFrequency = frequency;
}

void FluidSimulation::GetWindGustParams(float& amplitude, float& frequency) const
{
    amplitude = mWindGustAmplitude;
    frequency = mWindGustFrequency;
}

void FluidSimulation::UpdateWind(float dt)
{
    if (dt <= 0.0f) return;
    mWindTime += dt;

    if (mSegmentWindEnabled) {
        Vector3 segDir = mSegmentWindP1 - mSegmentWindP0;
        float len = sqrtf(segDir.x*segDir.x + segDir.y*segDir.y + segDir.z*segDir.z);
        if (len > 1e-6f) {
            float deltaT = (mSegmentWindSpeed * dt) / len;
            mSegmentWindProgress += deltaT;
            if (mSegmentWindProgress > 1.0f) {
                if (mSegmentWindLoop) {
                    mSegmentWindProgress = fmodf(mSegmentWindProgress, 1.0f);
                } else {
                    mSegmentWindProgress = 1.0f;
                    mSegmentWindEnabled = false;
                }
            }
        }
    }

    if (!mEnableTimeVaryingWind) {
        mWind = mWindBase;
        return;
    }

    float omega = 2.0f * (float)M_PI * mWindGustFrequency;
    float s = sinf(omega * mWindTime);
    Vector3 base = mWindBase;
    float baseMag = sqrtf(base.x * base.x + base.y * base.y + base.z * base.z);
    if (baseMag > 1e-6f) {
        Vector3 baseDir = Vector3(base.x / baseMag, base.y / baseMag, base.z / baseMag);
        float factor = 1.0f + mWindGustAmplitude * s;
        if (factor < 0.0f) factor = 0.0f;
        mWind = baseDir * (baseMag * factor);
    } else {
        float mag = mWindGustAmplitude * s;
        mWind = Vector3(mag, 0.0f, 0.0f);
    }
}

void FluidSimulation::AddLocalWind(const LocalWind& lw)
{
    if (mLocalWinds.size() >= static_cast<size_t>(MAX_LOCAL_WINDS)) return;
    mLocalWinds.push_back(lw);
    mNumLocalWinds = static_cast<UINT>(mLocalWinds.size());
}

void FluidSimulation::ClearLocalWinds()
{
    mLocalWinds.clear();
    mNumLocalWinds = 0;
}

void FluidSimulation::SetLocalWindMode(UINT mode)
{
    mLocalWindMode = mode;
}

UINT FluidSimulation::GetLocalWindMode() const
{
    return mLocalWindMode;
}

void FluidSimulation::SetLocalWindSigma(float sigma)
{
    mLocalWindSigma = sigma;
}

float FluidSimulation::GetLocalWindSigma() const
{
    return mLocalWindSigma;
}

void FluidSimulation::SetSegmentWind(const Vector3& p0, const Vector3& p1, float speed, float radius, float strength, bool loop)
{
    mSegmentWindP0 = p0;
    mSegmentWindP1 = p1;
    mSegmentWindSpeed = speed;
    mSegmentWindRadius = radius;
    mSegmentWindStrength = strength;
    mSegmentWindLoop = loop;
    mSegmentWindProgress = 0.0f;
    mSegmentWindEnabled = true;
}

void FluidSimulation::ClearSegmentWind()
{
    mSegmentWindEnabled = false;
    mSegmentWindProgress = 0.0f;
}

void FluidSimulation::EnableSegmentWind(bool enable)
{
    mSegmentWindEnabled = enable;
    if (!enable) mSegmentWindProgress = 0.0f;
}

bool FluidSimulation::IsSegmentWindEnabled() const
{
    return mSegmentWindEnabled;
}
