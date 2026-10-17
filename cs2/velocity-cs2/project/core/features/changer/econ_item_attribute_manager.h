#ifndef VELOCITY_CORE_FEATURES_CHANGER_ECON_ITEM_ATTRIBUTE_MANAGER_H_
#define VELOCITY_CORE_FEATURES_CHANGER_ECON_ITEM_ATTRIBUTE_MANAGER_H_
#include <array>
#include <cstddef>

#include "core/features/changer/cosmetic_configuration.h"
#include "core/features/changer/engine.h"

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

#endif  // VELOCITY_CORE_FEATURES_CHANGER_ECON_ITEM_ATTRIBUTE_MANAGER_H_
