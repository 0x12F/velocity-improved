#pragma once
#include "cosmetic_configuration.hpp"

namespace features::changer::weapon_cosmetics
{
    struct AppliedKeychainState
    {
        bool effective = false;
        int id = 0;
        int seed = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
    };

    [[nodiscard]] bool HasAny(const cosmetic_config::item_skin_t &config);
    [[nodiscard]] bool HasCustomConfiguration(const cosmetic_config::item_skin_t &config);
    [[nodiscard]] std::uint64_t ComputeConfigHash(const cosmetic_config::item_skin_t &config);
    [[nodiscard]] AppliedKeychainState GetDesiredKeychainState(const cosmetic_config::item_skin_t &config);
    [[nodiscard]] bool KeychainStatesEqual(const AppliedKeychainState &lhs, const AppliedKeychainState &rhs);
} // namespace features::changer::weapon_cosmetics
