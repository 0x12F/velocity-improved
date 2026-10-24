module;

#include "platform/platform.h"

export module velocity:core.features.changer.cosmetic_configuration;

import <array>;
import <cstdint>;
import <string>;
import <unordered_map>;

export {
namespace features::changer::cosmetic_config {
struct SkinStickerT {
  bool operator==(const SkinStickerT&) const = default;
  bool enabled = false;
  int kit = 0;
  float wear = 0.0f;
  float scale = 1.0f;
  float rotation = 0.0f;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
};

struct SkinKeychainT {
  bool operator==(const SkinKeychainT&) const = default;
  bool enabled = false;
  int id = 0;
  int seed = 0;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
  float offset_z = 0.0f;
};

struct ItemSkinT {
  bool enabled = false;
  int paint_kit = 0;
  int seed = 0;
  float wear = 0.0001f;
  bool stat_trak_enabled = false;
  int stat_trak_kills = 0;
  std::string custom_name;
  std::array<SkinStickerT, 5> stickers{};
  SkinKeychainT keychain{};
};

struct SkinChangerT {
  bool enabled = false;
  std::unordered_map<std::uint16_t, ItemSkinT> weapons;
  std::uint16_t knife_definition = 0;
  std::unordered_map<std::uint16_t, ItemSkinT> knives;
  std::uint16_t glove_definition = 0;
  std::unordered_map<std::uint16_t, ItemSkinT> gloves;
  std::uint16_t agent_definition = 0;
};

SkinChangerT skin_changer{};
}  // namespace features::changer::cosmetic_config
}  // export
