#pragma once

#include "gameObject.h"
#include <vector>
#include <functional>
#include <d3d11.h>
#include <DirectXMath.h>
#include "vector3.h"

// 前方宣言
class ThreadPool;
class Octree;
class Triangle;
class FluidCircle;
struct OBB;

#define M_PI 3.14159265358979323846


// GPU用障害物データ構造
struct GPUObstacle
{
    DirectX::XMFLOAT3 center;
    float pad0;
    DirectX::XMFLOAT3 halfSize;
    float pad1;
    DirectX::XMFLOAT3 axisX;
    float pad2;
    DirectX::XMFLOAT3 axisY;
    float pad3;
    DirectX::XMFLOAT3 axisZ;
    float pad4;
};

// 追加: LBM の局所風源（CPU 側レイアウト）
// HLSL 側 LocalWind とバイトレイアウトを一致させること
struct LocalWind
{
    DirectX::XMFLOAT3 center; // 格子座標系 (x,y,z)
    float             radius;
    DirectX::XMFLOAT3 direction;
    float             strength;
};

// 定数バッファ構造体（CPU側で更新してGPUに送るためヘッダに定義）
// LBM の定数バッファ（D3Q19 用）
// 注: HLSL 側の cbuffer レイアウトとバイト整合性を保つこと
struct LBMConstants
{
    DirectX::XMFLOAT4 weights[19];    // 重み（x のみ使用）
    DirectX::XMINT4   dirs[19];       // 方向ベクトル（int3 + pad）
    float             tau;
    DirectX::XMFLOAT3 gravity;
    DirectX::XMUINT3  gridSize;
    float             pad1;
    DirectX::XMFLOAT3 wind;
    float             pad2;
    float             forceScale;
    float             maxLbmSpeed;
    UINT              boundaryMode; // 0=periodic (wrap), 1=wall (bounce-back)
    // 追加: HLSL 側と完全一致させるためのパディング
    float             pad3;
    float             pad4;
    // Smagorinsky 用パラメータを追加（HLSL と一致させる）
    float             Cs;
    float             delta;
    float             tau_min;
    float             tau_max;
    // padding to make size multiple of 16 bytes
    float             pad_tail0;
    float             pad_tail1;
    float             pad_tail2;
};
static_assert(sizeof(LBMConstants) % 16 == 0, "LBMConstants must be 16-byte aligned");

// Advect（粒子移流）用定数バッファ
struct AdvectConstants
{
    UINT              numParticles;
    float             timeStep;
    float             boundaryDamping;
    UINT              numObstacles;
    DirectX::XMFLOAT3 boundsMin;
    float             pad1;
    DirectX::XMFLOAT3 boundsMax;
    float             pad2;
    DirectX::XMFLOAT3 boundsSize;
    float             pad3;
    DirectX::XMUINT3  gridSize;
    float             pad4;
    DirectX::XMFLOAT3 initialVelocity;
    float             pad5;
    DirectX::XMFLOAT3 wind;    // 追加: 動的な風ベクトル
    float             pad6;
};

 // インスタンス生成用定数バッファ（CS に渡す）
struct BuildInstanceConstants
{
    UINT              numParticles;
    DirectX::XMFLOAT3 pad0;         // alignment
    DirectX::XMFLOAT4 color_water;
    DirectX::XMFLOAT4 color_air;
    DirectX::XMMATRIX invView;
    DirectX::XMFLOAT4 pad1;         // padding to 16-byte boundary
};

// 流体の種類
enum class FluidType {
    Air,
    Water,
    Count
};

// 流体の物理的特性
struct FluidProperties {
    float restDensity;
    float viscosity;
    Vector3 color;
};

// 三角形インスタンスデータ
struct TriangleInstanceData {
    DirectX::XMFLOAT3 v1;
    DirectX::XMFLOAT3 v2;
    DirectX::XMFLOAT3 v3;
    DirectX::XMFLOAT4 color;
};

// パーティクルデータ（SoA: Structure of Arrays）
struct ParticleSoA {
    std::vector<Vector3> positions;
    std::vector<Vector3> velocities;
    std::vector<Vector3> forces;
    std::vector<float> densities;
    std::vector<float> pressures;
    std::vector<float> temperatures;
    std::vector<FluidType> fluidTypes;
    size_t count = 0;

    void resize(size_t n) {
        positions.resize(n);
        velocities.resize(n);
        forces.resize(n);
        densities.resize(n);
        pressures.resize(n);
        temperatures.resize(n);
        fluidTypes.resize(n);
        count = n;
    }

    void clear() {
        positions.clear();
        velocities.clear();
        forces.clear();
        densities.clear();
        pressures.clear();
        temperatures.clear();
        fluidTypes.clear();
        count = 0;
    }
};

// 追加: GPU 衝突解決用定数バッファレイアウト
// shader\particle_collide.hlsl の cbuffer レイアウトとバイト単位で一致させる
struct CollideConstants
{
    UINT                numParticles;   // b0.x
    DirectX::XMFLOAT3   boundsMin;      // b0.yzw
    float               pad0;           // b1.x (padding)

    DirectX::XMFLOAT3   boundsSize;     // b1.yzw
    float               pad1;           // b2.x (padding)

    DirectX::XMUINT3    gridDim;        // b2.yzw
    UINT                maxPerCell;     // b3.x

    float               cellSize;       // b3.y
    float               pad2[2];        // b3.zw + padding to 16 bytes
};
static_assert(sizeof(CollideConstants) == 64, "CollideConstants must be 64 bytes (16-byte aligned)");

class FluidSimulation : public GameObject {
public:
    // --- 定数 ---
    static const int NX = 64;
    static const int NY = 64;
    static const int NZ = 64;
    static const int Q = 19;

    // --- 初期化・解放 ---
    void Init() override;
    void Init(int particleCount);
    void Uninit() override;

    // --- 更新・描画 ---
    void Update() override;
    void Draw() override;  // ★追加

    // --- パラメータ設定 ---
    void SetSmoothingRadius(float smoothingRadius);
    void SetBounds(const Vector3& minBounds, const Vector3& maxBounds);
    void SetBoundsSize(const Vector3& size);
    void SetBoundsCenter(const Vector3& center);
    void SetInitialVelocity(const Vector3& velocity);
    void SetInitialVelocityRandom(float maxSpeed);
    void SetMovingObject(const Vector3& position, const Vector3& velocity, float radius);
    void ClearMovingObjects();
    void SetWind(const Vector3& wind);  // 既存: 即時に現在風をセット（上書き）

    // --- Gravity API (追加) ---
    void SetGravity(const Vector3& gravity);
    Vector3 GetGravity() const;
    void SetEnableGravity(bool enable);
    bool IsGravityEnabled() const;

    // --- 時変一様風 API ---
    void SetWindBase(const Vector3& baseWind); // ベース風（振幅基準）
    Vector3 GetWindBase() const;
    void EnableTimeVaryingWind(bool enable);
    bool IsTimeVaryingWindEnabled() const;
    void SetWindGustParams(float amplitude, float frequency); // amplitude: 相対振幅（例 0.5）、frequency: Hz
    void GetWindGustParams(float& amplitude, float& frequency) const;

    // 追加: 2点間を一定速度で流れる風（segment wind）
    // p0 -> p1 を speed (world units/sec) で移動する「局所風源」を内部で生成して GPU に流す
    // radius: world 単位の影響半径、strength: 0..1 相対強度、loop: 区間折り返しなしでループするか
    void SetSegmentWind(const Vector3& p0, const Vector3& p1, float speed, float radius = 1.0f, float strength = 1.0f, bool loop = true);
    void ClearSegmentWind();
    void EnableSegmentWind(bool enable);
    bool IsSegmentWindEnabled() const;

    // デバッグ: 粒子当たり判定可視化の切替
    void SetDebugVisualizeCollisions(bool enable); // ← 追加

    // 衝突セルスケールを設定（ヘッダに宣言）
    void SetCollideScale(float scale);

    // 衝突セルの絶対サイズを直接設定（cpp に実装あり）
    void SetCollideCellSize(float size);

    void VisualizeLBMSlice(UINT sliceZ);

    // 単一セル可視化テスト（手動確認用ヘルパ）
    // axis: 0=X,1=Y,2=Z
    // slice: スライスインデックス、x/y/z: テストセルのグリッド座標
    // testValue: rho に設定するテスト値（デフォルト 10.0f）
    // 戻り値: true = ピクセルが期待値（非ゼロ）を返した、false = 失敗
    bool TestVisualizeSingleCell(UINT axis, UINT slice, UINT x, UINT y, UINT z, float testValue = 10.0f);

    // --- 追加: スライス軸制御 ---
    enum LbmSliceAxis : UINT { AxisX = 0, AxisY = 1, AxisZ = 2 };
    void SetLbmSliceAxis(UINT axis) { mLbmSliceAxis = (axis <= 2u) ? axis : AxisZ; }
    UINT GetLbmSliceAxis() const { return mLbmSliceAxis; }

    // --- 追加: 可視化モード ---
    enum LbmVizMode : UINT { VizRho = 0, VizVelMag = 1, VizVelVec = 2 };
    // ImGui 等から参照/変更可能
    void SetLbmVizMode(UINT mode) { mLbmVizMode = (mode <= 2u) ? mode : VizRho; }
    UINT GetLbmVizMode() const { return mLbmVizMode; }

    // 障害物関連
    void AddObstacle(const OBB& obb);
    void ClearObstacles();
    void SetObstaclesFromScene();

    // --- ゲッター ---
    float GetSmoothingRadius() const { return mSmoothingRadius; }
    const Vector3& GetMinBounds() const { return mMinBounds; }
    const Vector3& GetMaxBounds() const { return mMaxBounds; }
    const Vector3& GetBoundsSize() const { return mBoundsSize; }
    const Vector3& GetBoundsCenter() const { return mBoundsCenter; }
    const Vector3& GetWind() const { return mWind; }  // 現在の風（ガスト合成後）

    // Force / stability control for LBM
    void SetForceScale(float fs);
    float GetForceScale() const;
    void SetMaxLbmSpeed(float s);
    float GetMaxLbmSpeed() const;
    // Boundary mode for LBM: 0 = periodic wrap, 1 = wall (bounce-back)
    void SetBoundaryMode(UINT mode);
    UINT GetBoundaryMode() const;
    // Set boundary mode and recreate LBM compute shader (runtime compile path)
    void SetBoundaryModeAndRecreate(UINT mode);

    // 追加公開メソッド（cpp で実装したもの)
    void AdvectCombine(); // GPU/CPU を組合せた移流+衝突パイプライン

    // 追加: public セクションに宣言
    void SetParticleCount(size_t newCount);

    // 新: 粒子表示切り替え
    void SetShowParticles(bool show);
    bool GetShowParticles() const;

    // 追加: LBMSlice の表示切り替え
    void SetShowLbmSlice(bool show);
    void SetLbmSliceIndex(UINT slice);
    bool GetShowLbmSlice() const;
    void DrawDebugUI();
    UINT GetLbmSliceIndex() const;

    // 粒子を LBM の速度場で駆動する（blend: 0..1）
    void DriveParticlesWithLbmVel(float blend = 1.0f);
    void DriveParticlesWithLbmVelGPU(float blend = 1.0f);

    // Smagorinsky パラメータ API
    void SetSmagorinskyParams(float Cs, float delta, float tauMin, float tauMax);
    void GetSmagorinskyParams(float& Cs, float& delta, float& tauMin, float& tauMax) const;

    // 追加: 局所風源関連
    void AddLocalWind(const LocalWind& lw);
    void ClearLocalWinds();

    // 追加: LocalWind モード/σ の API 宣言（既存の局所風源宣言の直後に挿入）
    void SetLocalWindMode(UINT mode);    // 0=linear, 1=smoothstep, 2=gaussian
    UINT GetLocalWindMode() const;
    void SetLocalWindSigma(float sigma); // gaussian 用 sigma（grid 単位）
    float GetLocalWindSigma() const;

private:
    // --- メンバー変数 ---
    ParticleSoA mParticles;
    std::vector<Vector3> mPreviousPositions;
    std::vector<FluidProperties> mFluidProperties;
    Octree* mOctree = nullptr;
    bool mOctreeNeedsRebuild = false;
    ThreadPool* mThreadPool = nullptr;

    // デバッグフラグ（粒子衝突可視化）
    bool mDebugVisualizeCollisions = false; // ← 追加

    // パーティクル表示フラグ（新）
//     bool mShowParticles = false;
    bool mShowParticles = true; // デフォルト表示ON

    // LBM スライス表示フラグ
    bool mShowLbmSlice = false;
    UINT mLbmSliceIndex = 0;
    UINT mLbmSliceAxis = AxisZ; // 追加: デフォルトは Z 面

    // LBM 可視化モード
    UINT mLbmVizMode = VizRho;  // 0=Rho(default),1=VelocityMagnitude,2=VelocityVector

    // --- SPHパラメータ ---
    float mSmoothingRadius = 1.0f;
    float mSR2 = 1.0f;
    float mSR6 = 1.0f;
    float mSR9 = 1.0f;
    float mPoly6Factor = 0.0f;
    float mSpikyFactor = 0.0f;
    float mViscosityFactor = 0.0f;

    // --- 新規: 衝突セル幅をポリゴンより小さくするための設定 ---
    float mCollideCellScale = 0.3f; // scale < 1.0 reduces collision cell size relative to smoothing radius
    float mCollideCellSize = 1.0f;  // 実際に使用するセル幅（SetSmoothingRadius で更新される）

    // --- シミュレーションパラメータ ---
    float mTimeStep = 0.01f;
    Vector3 mGravity{ 0.0f, -9.8f, 0.0f };
    bool    mEnableGravity = true; // 追加: GUI で切替可能に
    Vector3 mWind{ 0.0f, 0.0f, 0.0f };  // 現在の風（ガスト合成後）
    float mGasConstant = 2000.0f;
    float mBoundaryDamping = -0.5f;
    UINT mBoundaryMode = 0u; // 0=periodic,1=wall
    Vector3 mInitialVelocity{ 0.0f, 0.0f, 0.0f };

    // --- 時変一様風用メンバー ---
    Vector3 mWindBase{ 0.0f, 0.0f, 0.0f }; // ユーザ設定のベース風（スケール適用後）
    bool    mEnableTimeVaryingWind = false;
    float   mWindGustAmplitude = 0.0f; // 相対振幅（例 0.5 はベース風の 50%）
    float   mWindGustFrequency = 0.0f; // Hz
    float   mWindTime = 0.0f;          // 内部時間（秒）

    // --- 追加: セグメント風 (segment wind) ---
    bool    mSegmentWindEnabled = false;
    Vector3 mSegmentWindP0{0.0f, 0.0f, 0.0f};
    Vector3 mSegmentWindP1{0.0f, 0.0f, 0.0f};
    float   mSegmentWindSpeed = 1.0f;    // world units / sec
    float   mSegmentWindRadius = 1.0f;   // world units
    float   mSegmentWindStrength = 1.0f; // relative strength for LocalWind.strength
    bool    mSegmentWindLoop = true;
    float   mSegmentWindProgress = 0.0f; // 0..1 along p0->p1

    // LBM force control members
    float mForceScale = 10.0f;
    float mMaxLbmSpeed = 0.1f;

    // --- 境界 ---
    Vector3 mMinBounds;
    Vector3 mMaxBounds;
    Vector3 mBoundsSize;
    Vector3 mBoundsCenter;

    // --- LBM パラメータ ---
    float mWeights[Q];
    int mDirs[Q][3];
    float mTau = 0.6f;

    // Smagorinsky (運動論的SGS) パラメータ（CPU 側）
    float mSmagorinskyCs = 0.20f;
    float mSmagorinskyDelta = 1.0f;
    float mTauMin = 0.501f;
    float mTauMax = 20.0f;

    // --- LBM データ (CPU側) ---
    std::vector<float> mF;
    std::vector<float> mF_new;
    std::vector<float> mLbmRho;
    std::vector<Vector3> mLbmVel;
    std::vector<int> mIsSolid;

    // --- LBM用GPUリソース ---
    ID3D11ComputeShader* m_pLBM_CS = nullptr;
    ID3D11Buffer* m_pLBM_F_Buffer[2] = { nullptr, nullptr };
    ID3D11ShaderResourceView* m_pLBM_F_SRV[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* m_pLBM_F_UAV[2] = { nullptr, nullptr };
    ID3D11Buffer* m_pLBM_Rho_Buffer = nullptr;
    // SRV を追加（可視化用に必要）
    ID3D11ShaderResourceView* m_pLBM_Rho_SRV = nullptr;
    ID3D11UnorderedAccessView* m_pLBM_Rho_UAV = nullptr;
    ID3D11Buffer* m_pLBM_Vel_Buffer = nullptr;
    ID3D11ShaderResourceView* m_pLBM_Vel_SRV = nullptr;
    ID3D11UnorderedAccessView* m_pLBM_Vel_UAV = nullptr;
    ID3D11Buffer* m_pLBM_IsSolid_Buffer = nullptr;
    ID3D11ShaderResourceView* m_pLBM_IsSolid_SRV = nullptr;
    ID3D11Buffer* m_pLBM_ConstantBuffer = nullptr;
    int m_LBM_PingPong_Index = 0;
    // 可視化用 ComputeShader / 定数バッファ
    ID3D11ComputeShader* m_pVisualizeSlice_CS = nullptr;
    ID3D11Buffer* m_pVizCB = nullptr;
    // LBM 可視化出力（CPU/GPU で参照可能に保持）
    ID3D11Texture2D* m_pLbmVizTexture = nullptr;
    ID3D11UnorderedAccessView* m_pLbmVizUAV = nullptr;
    ID3D11ShaderResourceView* m_pLbmVizSRV = nullptr;
    UINT m_vizWidth = 0;
    UINT m_vizHeight = 0;

    // --- パーティクル移流用GPUリソース ---
    ID3D11ComputeShader* m_pAdvectParticles_CS = nullptr;
    ID3D11Buffer* m_pParticlePos_Buffer[2] = { nullptr, nullptr };
    ID3D11ShaderResourceView* m_pParticlePos_SRV[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* m_pParticlePos_UAV[2] = { nullptr, nullptr };
    ID3D11Buffer* m_pParticleVel_Buffer[2] = { nullptr, nullptr };
    ID3D11ShaderResourceView* m_pParticleVel_SRV[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* m_pParticleVel_UAV[2] = { nullptr, nullptr };
    ID3D11Buffer* m_pParticleType_Buffer = nullptr;
    ID3D11ShaderResourceView* m_pParticleType_SRV = nullptr;
    ID3D11Buffer* m_pAdvect_ConstantBuffer = nullptr;
    int m_Particle_PingPong_Index = 0;

    // 追加: DriveParticles GPU リソース（ComputeShader + ConstantBuffer）
    ID3D11ComputeShader* m_pDriveParticles_CS = nullptr;
    ID3D11Buffer* m_pDriveParticles_CB = nullptr;

    // --- インスタンス生成用GPUリソース ---
    ID3D11ComputeShader* m_pBuildInstance_CS = nullptr;
    ID3D11Buffer* m_pBuildInstance_ConstantBuffer = nullptr;
    ID3D11Buffer* m_pCircleInstanceBuffer_Structured = nullptr;
    ID3D11UnorderedAccessView* m_pCircleInstanceBuffer_UAV = nullptr;

    // --- 描画オブジェクト ---
    std::vector<TriangleInstanceData> mTriangleInstanceData;
    std::vector<FluidCircle*> mCircles;
    std::vector<class Triangle*> mTriangles;

    // --- インスタンシング描画リソース ---
    ID3D11Buffer* mCircleVertexBuffer = nullptr;
    ID3D11Buffer* mCircleInstanceBuffer = nullptr;
    ID3D11VertexShader* mCircleVertexShader = nullptr;
    ID3D11PixelShader* mCirclePixelShader = nullptr;
    ID3D11InputLayout* mCircleInputLayout = nullptr;
    UINT mCircleVertexCount = 0;

    // --- プライベートメソッド ---
    void LBM_Init();  // ★追加
    void LBM_Step();
    void AdvectParticles();
    void ComputeDensityAndPressure();      // ★追加
    void ComputeForces();                   // ★追加
    void Integrate();                       // ★追加
    void BuildOctree();                     // ★追加
    void ComputeHeatTransfer();             // ★追加
    void EnforceBoundaryConditions();       // ★追加
    void ParallelFor(const std::function<void(size_t, size_t)>& loopBody);  // ★追加
    Vector3 SurfaceTensionKernel(const Vector3& pi, const Vector3& pj, float distance) const;  // ★追加
    void UpdateVectorGraphics(size_t particleIndex, const Vector3& vector, const Vector3& color, size_t objectOffset);  // ★追加
    void SetInitialParticlePositions(); // 追加: private メソッド宣言エリアに入れる

    // --- 追加: 粒子間衝突解決（CPU） ---
    void ResolveParticleCollisionsCPU(float radius, float restitution);

    // --- カーネル関数 ---
    float Kernel(float distanceSq) const;
    Vector3 GradientKernel(float distance, const Vector3& diff) const;
    float LaplacianKernel(float distance) const;

    // --- インスタンシング関連 ---
    void InitInstancing();           // ★追加
    void UninitInstancing();         // ★追加
    void DrawInstancing();           // ★追加
    void InitTriangleInstancing();   // ★追加
    void UninitTriangleInstancing(); // ★追加
    void DrawTriangleInstancing();   // ★追加

    // --- 新規: 境界グリッド描画 ---
    void DrawBoundsGrid(int divisions = 16, DirectX::XMFLOAT4 color = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));

    // --- 新規（private）: 衝突可視化描画ヘルパ ---
    void DrawCollisionDebug(float radius, size_t maxPairs = 10000);

    // --- 移動するオブジェクト ---
    struct MovingObject {
        Vector3 position;
        Vector3 velocity;
        float radius;
    };
    std::vector<MovingObject> mMovingObjects;

    // --- 障害物関連 ---
    std::vector<GPUObstacle> mObstacles;
    static const int MAX_OBSTACLES = 16;
    ID3D11Buffer* m_pObstacle_Buffer = nullptr;
    ID3D11ShaderResourceView* m_pObstacle_SRV = nullptr;

    // --- 新規: GPU 衝突解決リソース ---
    ID3D11ComputeShader* m_pParticleCollideBuild_CS = nullptr;
    ID3D11ComputeShader* m_pParticleCollideResolve_CS = nullptr;
    ID3D11Buffer* m_pCellCountBuffer = nullptr;      // uint per cell
    ID3D11UnorderedAccessView* m_pCellCount_UAV = nullptr;
    ID3D11ShaderResourceView* m_pCellCount_SRV = nullptr;
    ID3D11Buffer* m_pCellListBuffer = nullptr;       // uint per cell * MAX_PER_CELL
    ID3D11UnorderedAccessView* m_pCellList_UAV = nullptr;
    ID3D11ShaderResourceView* m_pCellList_SRV = nullptr;
    ID3D11Buffer* m_pCollide_ConstantBuffer = nullptr;
    // 特性
    int mCollideGridX = 0, mCollideGridY = 0, mCollideGridZ = 0;
    int mCollideMaxPerCell = 64; // 調整可

    // --- 追加: 局所風源関連 ---
    static const int MAX_LOCAL_WINDS = 32; // 必要に応じて調整

    // GPU リソース
    ID3D11Buffer* m_pLocalWindBuffer = nullptr;
    ID3D11ShaderResourceView* m_pLocalWind_SRV = nullptr;
    ID3D11Buffer* m_pLocalWindCountCB = nullptr; // CB for gNumLocalWinds

    std::vector<LocalWind> mLocalWinds;
    UINT mNumLocalWinds = 0;

    // 局所風源モード/σ（デフォルト: smoothstep, sigma = 2.0）
    UINT mLocalWindMode = 1u;   // 0=linear, 1=smoothstep, 2=gaussian
    float mLocalWindSigma = 2.0f;

    // LBMスライスの自動可視化フラグ（ImGuiボタンや自動更新用）
    bool mAutoVisualizeLBM = true;

    // Auto-perform the F1 reset behavior once at startup to avoid needing manual F1
    bool mPerformedInitialReset = false;
private:
    // 追加: 衝突グリッド用 GPU リソースを再作成する内部ヘルパ（cpp に実装あり）
    void RecreateCollideGridResources();

    // --- 時変風更新ヘルパ ---
    void UpdateWind(float dt);
};