#pragma once
#include "gameObject.h" // GameObject.h をインクルード
class camera :public GameObject
{
public:
	void Init()override;
	void Uninit()override;
	void Update()override;
	void Draw()override; // Draw は空でも良い

	XMMATRIX GetProjectionMatrix() const { return mProjection; }
	XMMATRIX GetViewMatrix()  { return mView; }

	// 追加: near を取得して描画側と厳密に同期するためのアクセサ
	float GetNear() const { return mNear; }
	// 追加: far を取得
	float GetFar() const { return mFar; }

	// 追加: near/far セッタ（簡易バリデーション付）
	void SetNear(float nearVal) {
		const float kMinNear = 1e-4f;
		if (nearVal < kMinNear) nearVal = kMinNear;
		// near must be strictly less than far
		if (nearVal >= mFar) mFar = nearVal * 10.0f;
		mNear = nearVal;
	}
	void SetFar(float farVal) {
		const float kMinFar = 1e-3f;
		if (farVal < kMinFar) farVal = kMinFar;
		// far must be greater than near
		if (farVal <= mNear) mNear = farVal * 0.1f;
		mFar = farVal;
	}
	void SetNearFar(float nearVal, float farVal) {
		const float kMinNear = 1e-4f;
		if (nearVal < kMinNear) nearVal = kMinNear;
		if (farVal <= nearVal) farVal = nearVal * 1000.0f;
		mNear = nearVal;
		mFar  = farVal;
	}

private:
	XMMATRIX mProjection;
	XMMATRIX mView;
	Vector3 mTarget{ 0.0f,0.0f,0.0f };

	// 追加: projection の near / far を明示的に保持
	float mNear = 1.0f;
	float mFar  = 1000.0f;
};

