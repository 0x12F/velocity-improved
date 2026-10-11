#pragma once
#include <core/features/changer/engine.hpp>
#include <core/features/changer/cosmetic_configuration.hpp>

#include <array>
#include <cstddef>

namespace features::changer::attributes
{
    constexpr std::size_t STICKER_SLOT_COUNT = 5;

    using StickerData = cosmetic_config::skin_sticker_t;
    using KeychainData = cosmetic_config::skin_keychain_t;

    using StickerArray = std::array<StickerData, STICKER_SLOT_COUNT>;

    void Create(features::changer::engine::item_view *item, int paintKit, float wear, int seed, int statTrak = -1,
                int statTrakScoreType = 0, const StickerArray *stickers = nullptr,
                const KeychainData *keychain = nullptr);
    void Remove(features::changer::engine::item_view *item);

    // Replaces only sticker-related attributes on an existing item while preserving
    // paint, StatTrak, keychain and any other attributes already populated by the game.
    bool SyncStickers(features::changer::engine::item_view *item, const StickerArray &stickers);

    // Updates the existing raw keychain-ID attribute without reallocating the list.
    bool SetKeychainId(features::changer::engine::item_view *item, int id);
} // namespace features::changer::attributes
