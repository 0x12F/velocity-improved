module;

#include "platform/platform.h"

export module velocity:core.features.changer.weapon_cosmetics;

import <cstdint>;

export import :core.features.changer.cosmetic_configuration;

export {
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
}  // export
