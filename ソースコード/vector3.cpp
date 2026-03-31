#include "vector3.h"
#include <cmath>

// --- コンストラクタ ---
Vector3::Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
Vector3::Vector3(const Vector3& a) : x(a.x), y(a.y), z(a.z) {}
Vector3::Vector3(float nx, float ny, float nz) : x(nx), y(ny), z(nz) {}

// --- 演算子 ---
Vector3& Vector3::operator=(const Vector3& a) {
	x = a.x; y = a.y; z = a.z;
	return *this;
}

bool Vector3::operator==(const Vector3& a) const {
	return x == a.x && y == a.y && z == a.z;
}

bool Vector3::operator!=(const Vector3& a) const {
	return x != a.x || y != a.y || z != a.z;
}

Vector3 Vector3::operator-() const {
	return Vector3(-x, -y, -z);
}

Vector3 Vector3::operator+(const Vector3& a) const {
	return Vector3(x + a.x, y + a.y, z + a.z);
}

Vector3 Vector3::operator-(const Vector3& a) const {
	return Vector3(x - a.x, y - a.y, z - a.z);
}

Vector3 Vector3::operator*(float a) const {
	return Vector3(x * a, y * a, z * a);
}

Vector3 Vector3::operator/(float a) const {
	float oneOverA = 1.0f / a;
	return Vector3(x * oneOverA, y * oneOverA, z * oneOverA);
}

Vector3& Vector3::operator+=(const Vector3& a) {
	x += a.x; y += a.y; z += a.z;
	return *this;
}

Vector3& Vector3::operator-=(const Vector3& a) {
	x -= a.x; y -= a.y; z -= a.z;
	return *this;
}

Vector3& Vector3::operator*=(float a) {
	x *= a; y *= a; z *= a;
	return *this;
}

Vector3& Vector3::operator/=(float a) {
	float oneOverA = 1.0f / a;
	x *= oneOverA; y *= oneOverA; z *= oneOverA;
	return *this;
}

// --- メンバー関数 ---
void Vector3::zero() {
	x = y = z = 0.0f;
}

void Vector3::normalize() {
	float magSq = x * x + y * y + z * z;
	if (magSq > 0.0f) {
		float oneOverMag = 1.0f / sqrtf(magSq);
		x *= oneOverMag;
		y *= oneOverMag;
		z *= oneOverMag;
	}
}

Vector3 Vector3::normalized() const {
	float magSq = x * x + y * y + z * z;
	if (magSq > 0.0f) {
		float oneOverMag = 1.0f / sqrtf(magSq);
		return Vector3(x * oneOverMag, y * oneOverMag, z * oneOverMag);
	}
	return Vector3(0.0f, 0.0f, 0.0f);
}

float Vector3::length() const {
	return sqrtf(x * x + y * y + z * z);
}

float Vector3::lengthSq() const {
	return x * x + y * y + z * z;
}

float Vector3::dot(const Vector3& other) const {
	return x * other.x + y * other.y + z * other.z;
}

Vector3 Vector3::cross(const Vector3& other) const {
	return Vector3(
		y * other.z - z * other.y,
		z * other.x - x * other.z,
		x * other.y - y * other.x
	);
}

// --- 静的関数 ---
float Vector3::dot(const Vector3& a, const Vector3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Vector3::cross(const Vector3& a, const Vector3& b) {
	return Vector3(
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	);
}

// --- グローバル演算子 ---
Vector3 operator*(float a, const Vector3& v) {
	return Vector3(v.x * a, v.y * a, v.z * a);
}

// 追加実装は不要（vector3.hでインライン定義済み）