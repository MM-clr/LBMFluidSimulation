#pragma once
#include <cmath> // C++標準の <cmath> を使用

class Vector3
{
public:
	float x, y, z;

	// --- コンストラクタ ---
	Vector3();
	Vector3(const Vector3& a);
	Vector3(float nx, float ny, float nz);

	// --- 演算子 ---
	Vector3& operator=(const Vector3& a);
	bool operator==(const Vector3& a) const;
	bool operator!=(const Vector3& a) const;

	Vector3 operator-() const;
	Vector3 operator+(const Vector3& a) const;
	Vector3 operator-(const Vector3& a) const;
	Vector3 operator*(float a) const;
	Vector3 operator/(float a) const;

	Vector3& operator+=(const Vector3& a);
	Vector3& operator-=(const Vector3& a);
	Vector3& operator*=(float a);
	Vector3& operator/=(float a);

	// --- メンバー関数 ---
	void zero();
	void normalize();
	Vector3 normalized() const;
	float length() const;
	float lengthSq() const;

	float dot(const Vector3& other) const;
	Vector3 cross(const Vector3& other) const;

	// --- 静的関数 ---
	static float dot(const Vector3& a, const Vector3& b);
	static Vector3 cross(const Vector3& a, const Vector3& b);

    float Dot(const Vector3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
    Vector3 Cross(const Vector3& rhs) const {
        return Vector3(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }
    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }
    Vector3 Normalized() const {
        float len = Length();
        return (len > 1e-6f) ? (*this) / len : Vector3(0,0,0);
    }
};

// --- グローバル演算子 ---
Vector3 operator*(float a, const Vector3& v);