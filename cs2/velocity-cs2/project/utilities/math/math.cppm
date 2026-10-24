module;

#include "platform/platform.h"

export module velocity:utilities.math.math;

export {
namespace math {

class Vector2 {
 public:
  float x, y;

  Vector2() noexcept : x(0.0f), y(0.0f) {}
  constexpr Vector2(float x, float y) noexcept : x(x), y(y) {}

  [[nodiscard]] Vector2 operator+(const Vector2& v) const noexcept;
  [[nodiscard]] Vector2 operator-(const Vector2& v) const noexcept;
  [[nodiscard]] Vector2 operator*(float scalar) const noexcept;
  [[nodiscard]] Vector2 operator/(float scalar) const noexcept;
  [[nodiscard]] Vector2 operator-() const noexcept;

  Vector2& operator*=(float scalar) noexcept;
  Vector2& operator/=(float scalar) noexcept;
  Vector2& operator+=(const Vector2& v) noexcept;
  Vector2& operator-=(const Vector2& v) noexcept;

  [[nodiscard]] bool operator==(const Vector2& v) const noexcept;
  [[nodiscard]] bool operator!=(const Vector2& v) const noexcept;

  [[nodiscard]] float Dot(const Vector2& v) const noexcept;
  [[nodiscard]] float LengthSqr() const noexcept;
  [[nodiscard]] float Length() const noexcept;

  Vector2& Normalize() noexcept;
  [[nodiscard]] Vector2 Normalized() const noexcept;
};

class Vector4 {
 public:
  float x, y, z, w;
};

class Vector3 {
 public:
  float x, y, z;

  Vector3() noexcept : x(0.0f), y(0.0f), z(0.0f) {}
  constexpr Vector3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}

  [[nodiscard]] Vector3 operator+(const Vector3& v) const noexcept;
  [[nodiscard]] Vector3 operator-(const Vector3& v) const noexcept;
  [[nodiscard]] Vector3 operator*(float scalar) const noexcept;
  [[nodiscard]] Vector3 operator/(float scalar) const noexcept;
  [[nodiscard]] Vector3 operator-() const noexcept;

  Vector3& operator*=(float scalar) noexcept;
  Vector3& operator/=(float scalar) noexcept;
  Vector3& operator+=(const Vector3& v) noexcept;
  Vector3& operator-=(const Vector3& v) noexcept;

  [[nodiscard]] bool operator==(const Vector3& v) const noexcept;
  [[nodiscard]] bool operator!=(const Vector3& v) const noexcept;

  [[nodiscard]] float Dot(const Vector3& v) const noexcept;
  [[nodiscard]] Vector3 Cross(const Vector3& v) const noexcept;

  [[nodiscard]] float LengthSqr() const noexcept;
  [[nodiscard]] float Length() const noexcept;
  [[nodiscard]] float Length2d() const noexcept;

  Vector3& Normalize() noexcept;
  [[nodiscard]] Vector3 Normalized() const noexcept;
  [[nodiscard]] float Normalize2d() noexcept;
  [[nodiscard]] float Distance(const Vector3& v) const noexcept;
  [[nodiscard]] float DistanceSqr(const Vector3& v) const noexcept;
  [[nodiscard]] Vector3 ToRightVector() const noexcept;
  void ToDirections(Vector3* forward, Vector3* right,
                    Vector3* up) const noexcept;
};

class Quaternion {
 public:
  float x, y, z, w;

  Quaternion() noexcept;
  constexpr Quaternion(float x, float y, float z, float w) noexcept;

  [[nodiscard]] static Quaternion FromEuler(const Vector3& euler) noexcept;
  [[nodiscard]] Vector3 RotateVector(const Vector3& v) const noexcept;
};

class Matrix3x4 {
 public:
  float mat[3][4];

  [[nodiscard]] const float* operator[](int i) const noexcept;
  [[nodiscard]] float* operator[](int i) noexcept;
};

class Matrix4x4 {
 public:
  float mat[4][4];

  [[nodiscard]] const float* operator[](int index) const noexcept;
  [[nodiscard]] float* operator[](int index) noexcept;
};

namespace helpers {

void AngleVectorsLeft(const Vector3& angles, Vector3* forward = nullptr,
                      Vector3* left = nullptr, Vector3* up = nullptr);
void AngleVectors2d(float yaw, math::Vector3& forward, math::Vector3& right);
void NormalizeAngles(Vector3& angles);
void NormalizeAngle(float& angle);

[[nodiscard]] Vector3 VectorToAngle(const Vector3& forward);
[[nodiscard]] Vector3 CalculateAngle(const Vector3& src, const Vector3& dst);
[[nodiscard]] float AngleDistance(const Vector3& from, const Vector3& to);
[[nodiscard]] float DegToRad(float degrees);
[[nodiscard]] float RadToDeg(float radians);
[[nodiscard]] float NormalizeYaw(float yaw);

}  // namespace helpers

}  // namespace math
}  // export
