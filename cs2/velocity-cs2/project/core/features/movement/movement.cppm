module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.movement.movement;

import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string_view>;

export import :core.systems.systems;
export import :utilities.math.math;
export import :utilities.proto.proto;

export {
namespace features::movement {

class Bhop {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  [[nodiscard]] bool LandingJumpThisTick() const {
    return this->m_landing_jump_this_tick_;
  }
  [[nodiscard]] float LandingFraction() const {
    return this->m_landing_fraction_;
  }

 private:
  bool m_landing_jump_this_tick_{false};
  float m_landing_fraction_{1.0f};
};

class Airstrafe {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  void StoreAngles();

 private:
  void CheckButton(std::uintptr_t current_buttons, std::uintptr_t button);
  void RotateMovement(proto::BaseUsercmdPb* base, float target_yaw,
                      float view_yaw) const;
  void RotateToStop(proto::BaseUsercmdPb* base,
                    const math::Vector3& velocity) const;

  std::uintptr_t m_last_buttons_{};
  std::uintptr_t m_last_pressed_{};
  bool m_side_switch_{};
  math::Vector3 m_angles_{};
};

class Jumpbug {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  [[nodiscard]] bool ActiveThisTick() const {
    return this->m_active_this_tick_;
  }
  [[nodiscard]] float LandingFraction() const {
    return this->m_landing_fraction_;
  }

 private:
  [[nodiscard]] float GetImpulseMul(std::uintptr_t local_pawn) const;

  float m_landing_fraction_{1.0f};
  bool m_active_this_tick_{false};
};

class Fastladder {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd) const;
};

class Edgejump {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd) const;
};

class Edgestop {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd) const;
};

class Edgebug {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  void OnRender(xdraw::draw_list& draw_list);

  [[nodiscard]] bool ActiveThisTick() const {
    return this->m_active_this_tick_;
  }

 private:
  bool m_active_this_tick_{false};
};

class Slowwalk {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd) const;
};

class TestStrafer {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  [[nodiscard]] bool IsActive() const;
  [[nodiscard]] bool HandledThisTick() const {
    return this->m_handled_this_tick_;
  }

 private:
  void QuantizedPath(systems::Input::Usercmd* cmd);
  [[nodiscard]] bool ApplyYawSubtick(proto::BaseUsercmdPb* base, float when,
                                     float yaw_delta) const;
  void CheckButton(std::uintptr_t current_buttons, std::uintptr_t button);
  [[nodiscard]] static math::Vector2 MovementFromButtons(
      std::uintptr_t pressed);

  std::uintptr_t m_last_buttons_{};
  std::uintptr_t m_last_pressed_{};
  int m_substep_counter_{};
  bool m_handled_this_tick_{};
};

}  // namespace features::movement
}  // export
