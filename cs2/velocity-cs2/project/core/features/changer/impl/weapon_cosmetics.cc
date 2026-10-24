module;

#include "platform/platform.h"

module velocity;

import <algorithm>;
import <bit>;
import <cstddef>;

namespace features::changer::weapon_cosmetics {
namespace {
void HashByte(std::uint64_t &hash, std::uint8_t value) {
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  hash ^= value;
  hash *= kFnvPrime;
}

template <typename T>
void HashPod(std::uint64_t &hash, const T &value) {
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);

  for (std::size_t i = 0; i < sizeof(T); ++i) HashByte(hash, bytes[i]);
}

void HashFloat(std::uint64_t &hash, float value) {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  HashPod(hash, bits);
}

}  // namespace

bool HasAny(const cosmetic_config::ItemSkinT &config) {
  for (const auto &sticker : config.stickers) {
    if (sticker.enabled && sticker.kit > 0) return true;
  }

  return config.keychain.enabled && config.keychain.id > 0;
}

bool HasCustomConfiguration(const cosmetic_config::ItemSkinT &config) {
  if (config.paint_kit > 0) return true;

  if (config.stat_trak_enabled) return true;

  if (!config.custom_name.empty()) return true;

  return HasAny(config);
}

AppliedKeychainState GetDesiredKeychainState(
    const cosmetic_config::ItemSkinT &config) {
  AppliedKeychainState state{};
  state.effective = config.keychain.enabled && config.keychain.id > 0;

  if (!state.effective) return state;

  state.id = config.keychain.id;
  state.seed = (std::clamp)(config.keychain.seed, 0, 100000);
  state.offset_x = config.keychain.offset_x;
  state.offset_y = config.keychain.offset_y;
  state.offset_z = config.keychain.offset_z;
  return state;
}

bool KeychainStatesEqual(const AppliedKeychainState &lhs,
                         const AppliedKeychainState &rhs) {
  if (lhs.effective != rhs.effective) return false;

  if (!lhs.effective) return true;

  return lhs.id == rhs.id && lhs.seed == rhs.seed &&
         lhs.offset_x == rhs.offset_x && lhs.offset_y == rhs.offset_y &&
         lhs.offset_z == rhs.offset_z;
}

std::uint64_t ComputeConfigHash(const cosmetic_config::ItemSkinT &config) {
  constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;

  std::uint64_t hash = kFnvOffsetBasis;

  HashPod(hash, config.paint_kit);
  HashPod(hash, config.seed);
  HashFloat(hash, config.wear);
  HashPod(hash, config.stat_trak_enabled);
  HashPod(hash, config.stat_trak_kills);

  for (const char c : config.custom_name)
    HashByte(hash, static_cast<std::uint8_t>(c));

  HashByte(hash, 0xFF);

  for (const auto &sticker : config.stickers) {
    const bool effective = sticker.enabled && sticker.kit > 0;
    HashPod(hash, effective);

    if (!effective) continue;

    HashPod(hash, sticker.kit);
    HashFloat(hash, sticker.wear);
    HashFloat(hash, sticker.scale);
    HashFloat(hash, sticker.rotation);
    HashFloat(hash, sticker.offset_x);
    HashFloat(hash, sticker.offset_y);
  }

  const bool keychain_effective =
      config.keychain.enabled && config.keychain.id > 0;
  HashPod(hash, keychain_effective);

  if (keychain_effective) {
    HashPod(hash, config.keychain.id);
    HashPod(hash, config.keychain.seed);
    HashFloat(hash, config.keychain.offset_x);
    HashFloat(hash, config.keychain.offset_y);
    HashFloat(hash, config.keychain.offset_z);
  }

  return hash;
}

}  // namespace features::changer::weapon_cosmetics
