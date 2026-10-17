#ifndef VELOCITY_CORE_FEATURES_CHANGER_WEAPON_COSMETICS_H_
#define VELOCITY_CORE_FEATURES_CHANGER_WEAPON_COSMETICS_H_

#include <cstdint>

#include "core/features/changer/cosmetic_configuration.h"

namespace features::changer::weapon_cosmetics {
struct AppliedKeychainState {
  bool effective = false;
  int id = 0;
  int seed = 0;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
  float offset_z = 0.0f;
};

[[nodiscard]] bool HasAny(const cosmetic_config::ItemSkinT &config);
[[nodiscard]] bool HasCustomConfiguration(
    const cosmetic_config::ItemSkinT &config);
[[nodiscard]] std::uint64_t ComputeConfigHash(
    const cosmetic_config::ItemSkinT &config);
[[nodiscard]] AppliedKeychainState GetDesiredKeychainState(
    const cosmetic_config::ItemSkinT &config);
[[nodiscard]] bool KeychainStatesEqual(const AppliedKeychainState &lhs,
                                       const AppliedKeychainState &rhs);
}  // namespace features::changer::weapon_cosmetics

#endif  // VELOCITY_CORE_FEATURES_CHANGER_WEAPON_COSMETICS_H_
