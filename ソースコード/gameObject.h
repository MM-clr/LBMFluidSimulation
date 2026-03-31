#pragma once
#include "main.h" // <--- この行を追加
#include "vector3.h" // <--- vector3.h は main.h の後でインクルード
#include <typeinfo>
#include <DirectXMath.h>

using namespace DirectX;

class GameObject
{
protected:	
	Vector3 mPosition{ 0.0f,0.0f,0.0f };
	Vector3 mRotation{ 0.0f,0.0f,0.0f };
	Vector3 mScale{ 1.0f,1.0f,1.0f };
	bool mDestroy = false;
	bool m_Active = true; // アクティブ状態を管理するメンバー変数を追加

public:
	GameObject() {};
	virtual ~GameObject() {};

	virtual void Init()=0;
	virtual void Uninit()=0;
	virtual void Update()=0;
	virtual void Draw() = 0;

	void SetDestroy() { mDestroy = true; }
	bool Destroy() { 
		if (mDestroy) 
		{
			Uninit();
			delete this;
			return true;
		}
		else
		{
			return false;
		}
	}

	// --- アクティブ状態を操作するメソッドを追加 ---
	void SetActive(bool active) { m_Active = active; }
	bool GetActive() const { return m_Active; }

	Vector3 GetPosition() const { return mPosition; }
	void SetPosition(const Vector3& position) { mPosition = position; }
	Vector3 GetRotation() const { return mRotation; }
	void SetRotation(const Vector3& rotation) { mRotation = rotation; }
	Vector3 GetScale() const { return mScale; }
	void SetScale(const Vector3& scale) { mScale = scale; }

	Vector3 GetRight() const 
	{
		XMMATRIX mat = XMMatrixRotationRollPitchYaw(mRotation.x, mRotation.y, mRotation.z);
		Vector3 right;
		XMStoreFloat3((XMFLOAT3*)&right, mat.r[0]);
		return right;
	}

	Vector3 GetUp() const 
	{
		XMMATRIX mat = XMMatrixRotationRollPitchYaw(mRotation.x, mRotation.y, mRotation.z);
		Vector3 up;
		XMStoreFloat3((XMFLOAT3*)&up, mat.r[1]);
		return up;
	}
	Vector3 GetForward() const 
	{
		XMMATRIX mat = XMMatrixRotationRollPitchYaw(mRotation.x, mRotation.y, mRotation.z);
		Vector3 forward;
		XMStoreFloat3((XMFLOAT3*)&forward, mat.r[2]);
		return forward;
	}

	float GetDistance(const Vector3& Position) const
	{
		return (mPosition - Position).length();
	}
};

