#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace features::changer::cosmetic_config
{
    struct skin_sticker_t
    {
        bool operator==(const skin_sticker_t&) const = default;
        bool enabled = false;
        int kit = 0;
        float wear = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    struct skin_keychain_t
    {
        bool operator==(const skin_keychain_t&) const = default;
        bool enabled = false;
        int id = 0;
        int seed = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
    };

    struct item_skin_t
    {
        bool enabled = false;
        int paintKit = 0;
        int seed = 0;
        float wear = 0.0001f;
        bool statTrakEnabled = false;
        int statTrakKills = 0;
        std::string customName;
        std::array<skin_sticker_t, 5> stickers{};
        skin_keychain_t keychain{};
    };

    struct skin_changer_t
    {
        bool enabled = false;
        std::unordered_map<std::uint16_t, item_skin_t> weapons;
        std::uint16_t knifeDefinition = 0;
        std::unordered_map<std::uint16_t, item_skin_t> knives;
        std::uint16_t gloveDefinition = 0;
        std::unordered_map<std::uint16_t, item_skin_t> gloves;
        std::uint16_t agentDefinition = 0;
    };

    inline skin_changer_t skin_changer{};
}
