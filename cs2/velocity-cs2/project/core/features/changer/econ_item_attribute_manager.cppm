module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.econ_item_attribute_manager;

import <array>;
import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

export import :core.features.changer.cosmetic_configuration;
export import :core.features.changer.engine;

export {
namespace features::changer::attributes {
constexpr std::size_t kStickerSlotCount = 5;

using StickerData = cosmetic_config::SkinStickerT;
using KeychainData = cosmetic_config::SkinKeychainT;

using StickerArray = std::array<StickerData, kStickerSlotCount>;

void Create(features::changer::engine::ItemView *item, int paint_kit,
            float wear, int seed, int stat_trak = -1,
            int stat_trak_score_type = 0,
            const StickerArray *stickers = nullptr,
            const KeychainData *keychain = nullptr);
void Remove(features::changer::engine::ItemView *item);

// Replaces only sticker-related attributes on an existing item while preserving
// paint, StatTrak, keychain and any other attributes already populated by the
// game.
bool SyncStickers(features::changer::engine::ItemView *item,
                  const StickerArray &stickers);

// Updates the existing raw keychain-ID attribute without reallocating the list.
bool SetKeychainId(features::changer::engine::ItemView *item, int id);
}  // namespace features::changer::attributes
}  // export
