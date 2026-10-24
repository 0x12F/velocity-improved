module;

#include "platform/platform.h"

module velocity;

namespace math {

Vector3 Vector3::operator+(const Vector3& v) const noexcept {
  return {x + v.x, y + v.y, z + v.z};
}
Vector3 Vector3::operator-(const Vector3& v) const noexcept {
  return {x - v.x, y - v.y, z - v.z};
}
Vector3 Vector3::operator*(float scalar) const noexcept {
  return {x * scalar, y * scalar, z * scalar};
}
Vector3 Vector3::operator/(float scalar) const noexcept {
  return {x / scalar, y / scalar, z / scalar};
}
Vector3 Vector3::operator-() const noexcept { return {-x, -y, -z}; }

Vector3& Vector3::operator*=(float scalar) noexcept {
  x *= scalar;
  y *= scalar;
  z *= scalar;
  return *this;
}
Vector3& Vector3::operator/=(float scalar) noexcept {
  x /= scalar;
  y /= scalar;
  z /= scalar;
  return *this;
}
Vector3& Vector3::operator+=(const Vector3& v) noexcept {
  x += v.x;
  y += v.y;
  z += v.z;
  return *this;
}
Vector3& Vector3::operator-=(const Vector3& v) noexcept {
  x -= v.x;
  y -= v.y;
  z -= v.z;
  return *this;
}

bool Vector3::operator==(const Vector3& v) const noexcept {
  return x == v.x && y == v.y && z == v.z;
}
bool Vector3::operator!=(const Vector3& v) const noexcept {
  return !(*this == v);
}

float Vector3::Dot(const Vector3& v) const noexcept {
  return x * v.x + y * v.y + z * v.z;
}
Vector3 Vector3::Cross(const Vector3& v) const noexcept {
  return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
}

float Vector3::LengthSqr() const noexcept { return x * x + y * y + z * z; }

float Vector3::Length() const noexcept {
  const auto len_sq = this->LengthSqr();
  if (len_sq == 0.0f) [[unlikely]] {
    return 0.0f;
  }

  return std::sqrtf(len_sq);
}

float Vector3::Length2d() const noexcept {
  const auto len_sq_2d = x * x + y * y;
  if (len_sq_2d == 0.0f) [[unlikely]] {
    return 0.0f;
  }

  return std::sqrtf(len_sq_2d);
}

Vector3& Vector3::Normalize() noexcept {
  const auto len = this->Length();
  if (len > 0.0f) [[likely]] {
    const float inv_len = 1.0f / len;
    *this *= inv_len;
  }

  return *this;
}

Vector3 Vector3::Normalized() const noexcept {
  auto vec = *this;
  vec.Normalize();
  return vec;
}

float Vector3::Normalize2d() noexcept {
  const auto len = this->Length2d();
  if (len > 0.0f) [[likely]] {
    const float inv_len = 1.0f / len;
    x *= inv_len;
    y *= inv_len;
  }

  z = 0.0f;
  return len;
}

float Vector3::Distance(const Vector3& v) const noexcept {
  return (*this - v).Length();
}
float Vector3::DistanceSqr(const Vector3& v) const noexcept {
  return (*this - v).LengthSqr();
}

Vector3 Vector3::ToRightVector() const noexcept {
  auto right = this->Cross(Vector3(0.0f, 0.0f, 1.0f));

  if (right.LengthSqr() < 1e-6f) {
    right = this->Cross(Vector3(0.0f, -1.0f, 0.0f));
  }

  return right.Normalize();
}

void Vector3::ToDirections(Vector3* forward, Vector3* right,
                           Vector3* up) const noexcept {
  constexpr auto kDegToRad = std::numbers::pi_v<float> / 180.0f;

  const auto sp = std::sinf(x * kDegToRad);
  const auto cp = std::cosf(x * kDegToRad);
  const auto sy = std::sinf(y * kDegToRad);
  const auto cy = std::cosf(y * kDegToRad);
  const auto sr = std::sinf(z * kDegToRad);
  const auto cr = std::cosf(z * kDegToRad);

  if (forward) {
    forward->x = cp * cy;
    forward->y = cp * sy;
    forward->z = -sp;
  }

  if (right) {
    right->x = -1.0f * sr * sp * cy + -1.0f * cr * -sy;
    right->y = -1.0f * sr * sp * sy + -1.0f * cr * cy;
    right->z = -1.0f * sr * cp;
  }

  if (up) {
    up->x = cr * sp * cy + -sr * -sy;
    up->y = cr * sp * sy + -sr * cy;
    up->z = cr * cp;
  }
}

Vector2 Vector2::operator+(const Vector2& v) const noexcept {
  return {x + v.x, y + v.y};
}
Vector2 Vector2::operator-(const Vector2& v) const noexcept {
  return {x - v.x, y - v.y};
}
Vector2 Vector2::operator*(float scalar) const noexcept {
  return {x * scalar, y * scalar};
}
Vector2 Vector2::operator/(float scalar) const noexcept {
  return {x / scalar, y / scalar};
}
Vector2 Vector2::operator-() const noexcept { return {-x, -y}; }

Vector2& Vector2::operator*=(float scalar) noexcept {
  x *= scalar;
  y *= scalar;
  return *this;
}
Vector2& Vector2::operator/=(float scalar) noexcept {
  x /= scalar;
  y /= scalar;
  return *this;
}
Vector2& Vector2::operator+=(const Vector2& v) noexcept {
  x += v.x;
  y += v.y;
  return *this;
}
Vector2& Vector2::operator-=(const Vector2& v) noexcept {
  x -= v.x;
  y -= v.y;
  return *this;
}

bool Vector2::operator==(const Vector2& v) const noexcept {
  return x == v.x && y == v.y;
}
bool Vector2::operator!=(const Vector2& v) const noexcept {
  return !(*this == v);
}

float Vector2::Dot(const Vector2& v) const noexcept {
  return x * v.x + y * v.y;
}
float Vector2::LengthSqr() const noexcept { return x * x + y * y; }

float Vector2::Length() const noexcept {
  const auto len_sq = LengthSqr();
  if (len_sq == 0.0f) [[unlikely]] {
    return 0.0f;
  }

  return std::sqrtf(len_sq);
}

Vector2& Vector2::Normalize() noexcept {
  const auto len = Length();
  if (len > 0.0f) [[likely]] {
    const float inv_len = 1.0f / len;
    *this *= inv_len;
  }

  return *this;
}

Vector2 Vector2::Normalized() const noexcept {
  auto vec = *this;
  vec.Normalize();
  return vec;
}

// -------------------------------- //

Quaternion::Quaternion() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
constexpr Quaternion::Quaternion(float x, float y, float z, float w) noexcept
    : x(x), y(y), z(z), w(w) {}

Quaternion Quaternion::FromEuler(const Vector3& euler) noexcept {
  const auto pitch = euler.x * (std::numbers::pi_v<float> / 180.0f);
  const auto yaw = euler.y * (std::numbers::pi_v<float> / 180.0f);
  const auto roll = euler.z * (std::numbers::pi_v<float> / 180.0f);

  const auto cy = std::cos(yaw * 0.5f);
  const auto sy = std::sin(yaw * 0.5f);
  const auto cp = std::cos(pitch * 0.5f);
  const auto sp = std::sin(pitch * 0.5f);
  const auto cr = std::cos(roll * 0.5f);
  const auto sr = std::sin(roll * 0.5f);

  Quaternion q;
  q.w = cr * cp * cy + sr * sp * sy;
  q.x = sr * cp * cy - cr * sp * sy;
  q.y = cr * sp * cy + sr * cp * sy;
  q.z = cr * cp * sy - sr * sp * cy;

  return q;
}

Vector3 Quaternion::RotateVector(const Vector3& v) const noexcept {
  const auto q_vec = Vector3(this->x, this->y, this->z);
  auto uv = q_vec.Cross(v);
  auto uuv = q_vec.Cross(uv);

  uv *= (2.0f * this->w);
  uuv *= 2.0f;

  return v + uv + uuv;
}

// -------------------------------- //

const float* Matrix3x4::operator[](int i) const noexcept { return mat[i]; }

float* Matrix3x4::operator[](int i) noexcept { return mat[i]; }

const float* Matrix4x4::operator[](int index) const noexcept {
  return mat[index];
}

float* Matrix4x4::operator[](int index) noexcept { return mat[index]; }

// -------------------------------- //

namespace helpers {

void AngleVectorsLeft(const Vector3& angles, Vector3* forward, Vector3* left,
                      Vector3* up) {
  const auto pitch = angles.x * (std::numbers::pi_v<float> / 180.0f);
  const auto yaw = angles.y * (std::numbers::pi_v<float> / 180.0f);

  const auto sin_pitch = std::sinf(pitch);
  const auto cos_pitch = std::cosf(pitch);
  const auto sin_yaw = std::sinf(yaw);
  const auto cos_yaw = std::cosf(yaw);

  if (forward) {
    forward->x = cos_pitch * cos_yaw;
    forward->y = cos_pitch * sin_yaw;
    forward->z = -sin_pitch;
  }

  if (left || up) {
    const auto roll = angles.z * (std::numbers::pi_v<float> / 180.0f);
    const auto sin_roll = std::sinf(roll);
    const auto cos_roll = std::cosf(roll);

    if (left) {
      left->x = -sin_roll * sin_pitch * cos_yaw + cos_roll * sin_yaw;
      left->y = -sin_roll * sin_pitch * sin_yaw - cos_roll * cos_yaw;
      left->z = -sin_roll * cos_pitch;
    }

    if (up) {
      up->x = cos_roll * sin_pitch * cos_yaw + sin_roll * sin_yaw;
      up->y = cos_roll * sin_pitch * sin_yaw - sin_roll * cos_yaw;
      up->z = cos_roll * cos_pitch;
    }
  }
}

void AngleVectors2d(float yaw, math::Vector3& forward, math::Vector3& right) {
  const auto rad = yaw * (std::numbers::pi_v<float> / 180.0f);
  const auto sy = std::sinf(rad);
  const auto cy = std::cosf(rad);

  forward = {cy, sy, 0.0f};
  right = {-sy, cy, 0.0f};
}

void NormalizeAngles(Vector3& angles) {
  while (angles.y > 180.0f) {
    angles.y -= 360.0f;
  }
  while (angles.y < -180.0f) {
    angles.y += 360.0f;
  }
  while (angles.x > 89.0f) {
    angles.x -= 180.0f;
  }
  while (angles.x < -89.0f) {
    angles.x += 180.0f;
  }

  angles.z = 0.0f;
}

void NormalizeAngle(float& angle) {
  while (angle > 180.0f) {
    angle -= 360.0f;
  }
  while (angle < -180.0f) {
    angle += 360.0f;
  }
}

Vector3 VectorToAngle(const Vector3& forward) {
  auto pitch = RadToDeg(std::atan2(-forward.z, forward.Length2d()));
  auto yaw = RadToDeg(std::atan2(forward.y, forward.x));
  return {pitch, yaw, 0.f};
}

Vector3 CalculateAngle(const Vector3& src, const Vector3& dst) {
  const auto delta = dst - src;
  const auto length = delta.Length2d();

  Vector3 angles;
  angles.x = RadToDeg(std::atan2f(-delta.z, length));
  angles.y = RadToDeg(std::atan2f(delta.y, delta.x));
  angles.z = 0.0f;

  return angles;
}

float AngleDistance(const Vector3& from, const Vector3& to) {
  const auto pitch = to.x - from.x;
  const auto yaw = NormalizeYaw(to.y - from.y);
  return std::sqrtf(pitch * pitch + yaw * yaw);
}

float DegToRad(float degrees) {
  return degrees * (std::numbers::pi_v<float> / 180.0f);
}

float RadToDeg(float radians) {
  return radians * (180.0f / std::numbers::pi_v<float>);
}

float NormalizeYaw(float yaw) {
  while (yaw > 180.0f) {
    yaw -= 360.0f;
  }
  while (yaw < -180.0f) {
    yaw += 360.0f;
  }
  return yaw;
}

}  // namespace helpers

}  // namespace math
