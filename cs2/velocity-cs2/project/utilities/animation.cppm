module;

#include "platform/platform.h"

#include <external/xdraw/xdraw.hpp>

export module velocity:utilities.animation;

import <cmath>;
import <cstdint>;

export {
namespace animation {

enum class Easing : std::uint8_t { kLinear, kEaseIn, kEaseOut, kEaseInOut };

class Tween {
 public:
  void Start(float from, float to, float duration,
             Easing ease = Easing::kEaseOut) {
    this->m_from_ = from;
    this->m_to_ = to;
    this->m_value_ = from;
    this->m_duration_ = duration;
    this->m_elapsed_ = 0.0f;
    this->m_easing_ = ease;
    this->m_finished_ = false;
  }

  void Update() {
    if (this->m_finished_) {
      return;
    }

    const auto dt = xdraw::delta_time();

    this->m_elapsed_ += dt;

    if (this->m_elapsed_ >= this->m_duration_) {
      this->m_value_ = this->m_to_;
      this->m_finished_ = true;
      return;
    }

    const auto t = this->ApplyEasing(this->m_elapsed_ / this->m_duration_);
    this->m_value_ = this->m_from_ + (this->m_to_ - this->m_from_) * t;
  }

  [[nodiscard]] float Value() const { return this->m_value_; }
  [[nodiscard]] bool Finished() const { return this->m_finished_; }

  void Reset() {
    this->m_value_ = this->m_from_;
    this->m_elapsed_ = 0.0f;
    this->m_finished_ = true;
  }

 private:
  [[nodiscard]] float ApplyEasing(float t) const {
    switch (this->m_easing_) {
      case Easing::kEaseIn:
        return t * t;

      case Easing::kEaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);

      case Easing::kEaseInOut:
        return t < 0.5f ? 2.0f * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;

      default:
        return t;
    }
  }

  float m_from_{0.0f};
  float m_to_{0.0f};
  float m_value_{0.0f};
  float m_duration_{0.0f};
  float m_elapsed_{0.0f};
  Easing m_easing_{Easing::kLinear};
  bool m_finished_{true};
};

class Tween2d {
 public:
  void Start(float from_x, float from_y, float to_x, float to_y, float duration,
             Easing ease = Easing::kEaseOut) {
    this->m_x_.Start(from_x, to_x, duration, ease);
    this->m_y_.Start(from_y, to_y, duration, ease);
  }

  void Update() {
    this->m_x_.Update();
    this->m_y_.Update();
  }

  [[nodiscard]] float X() const { return this->m_x_.Value(); }
  [[nodiscard]] float Y() const { return this->m_y_.Value(); }
  [[nodiscard]] bool Finished() const {
    return this->m_x_.Finished() && this->m_y_.Finished();
  }

  void Reset() {
    this->m_x_.Reset();
    this->m_y_.Reset();
  }

 private:
  Tween m_x_{};
  Tween m_y_{};
};

class Spring {
 public:
  void SetTarget(float target) { this->m_target_ = target; }

  void Update() {
    const auto dt = xdraw::delta_time();

    const auto diff = this->m_target_ - this->m_value_;
    const auto accel =
        diff * this->m_stiffness_ - this->m_velocity_ * this->m_damping_;

    this->m_velocity_ += accel * dt;
    this->m_value_ += this->m_velocity_ * dt;
  }

  [[nodiscard]] float Value() const { return this->m_value_; }

  [[nodiscard]] bool Settled() const {
    return std::abs(this->m_target_ - this->m_value_) < 0.001f &&
           std::abs(this->m_velocity_) < 0.001f;
  }

  void SetStiffness(float stiffness) { this->m_stiffness_ = stiffness; }
  void SetDamping(float damping) { this->m_damping_ = damping; }

  void Snap(float value) {
    this->m_value_ = value;
    this->m_target_ = value;
    this->m_velocity_ = 0.0f;
  }

 private:
  float m_value_{0.0f};
  float m_velocity_{0.0f};
  float m_target_{0.0f};
  float m_stiffness_{200.0f};
  float m_damping_{20.0f};
};

class Spring2d {
 public:
  void SetTarget(float x, float y) {
    this->m_x_.SetTarget(x);
    this->m_y_.SetTarget(y);
  }

  void Update() {
    this->m_x_.Update();
    this->m_y_.Update();
  }

  [[nodiscard]] float X() const { return this->m_x_.Value(); }
  [[nodiscard]] float Y() const { return this->m_y_.Value(); }
  [[nodiscard]] bool Settled() const {
    return this->m_x_.Settled() && this->m_y_.Settled();
  }

  void SetStiffness(float stiffness) {
    this->m_x_.SetStiffness(stiffness);
    this->m_y_.SetStiffness(stiffness);
  }

  void SetDamping(float damping) {
    this->m_x_.SetDamping(damping);
    this->m_y_.SetDamping(damping);
  }

  void Snap(float x, float y) {
    this->m_x_.Snap(x);
    this->m_y_.Snap(y);
  }

 private:
  Spring m_x_{};
  Spring m_y_{};
};

class Progress {
 public:
  void Set(float target, float duration = 0.3f) {
    this->m_tween_.Start(this->m_tween_.Value(), target, duration,
                         Easing::kEaseOut);
    this->m_target_ = target;
  }

  void Update() { this->m_tween_.Update(); }

  [[nodiscard]] float Value() const { return this->m_tween_.Value(); }
  [[nodiscard]] float Target() const { return this->m_target_; }
  [[nodiscard]] bool Finished() const { return this->m_tween_.Finished(); }

 private:
  Tween m_tween_{};
  float m_target_{0.0f};
};

class Fade {
 public:
  void FadeIn(float duration = 0.2f) {
    this->m_tween_.Start(this->m_tween_.Value(), 1.0f, duration,
                         Easing::kEaseOut);
    this->m_alpha_target_ = 1.0f;
  }

  void FadeOut(float duration = 0.2f) {
    this->m_tween_.Start(this->m_tween_.Value(), 0.0f, duration,
                         Easing::kEaseOut);
    this->m_alpha_target_ = 0.0f;
  }

  void Update() { this->m_tween_.Update(); }

  [[nodiscard]] float Alpha() const { return this->m_tween_.Value(); }
  [[nodiscard]] bool Visible() const {
    return this->m_alpha_target_ > 0.0f || !this->m_tween_.Finished();
  }
  [[nodiscard]] bool Finished() const { return this->m_tween_.Finished(); }

 private:
  Tween m_tween_{};
  float m_alpha_target_{0.0f};
};

}  // namespace animation
}  // export
