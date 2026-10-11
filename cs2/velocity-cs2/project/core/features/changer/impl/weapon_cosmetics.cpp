#include "../weapon_cosmetics.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>

namespace features::changer::weapon_cosmetics
{
    namespace
    {
        void HashByte(std::uint64_t &hash, std::uint8_t value)
        {
            constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
            hash ^= value;
            hash *= FNV_PRIME;
        }

        template <typename T> void HashPod(std::uint64_t &hash, const T &value)
        {
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);

            for (std::size_t i = 0; i < sizeof(T); ++i)
                HashByte(hash, bytes[i]);
        }

        void HashFloat(std::uint64_t &hash, float value)
        {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            HashPod(hash, bits);
        }

    } // namespace

    bool HasAny(const cosmetic_config::item_skin_t &config)
    {
        for (const auto &sticker : config.stickers)
        {
            if (sticker.enabled && sticker.kit > 0)
                return true;
        }

        return config.keychain.enabled && config.keychain.id > 0;
    }

    bool HasCustomConfiguration(const cosmetic_config::item_skin_t &config)
    {
        if (config.paintKit > 0)
            return true;

        if (config.statTrakEnabled)
            return true;

        if (!config.customName.empty())
            return true;

        return HasAny(config);
    }

    AppliedKeychainState GetDesiredKeychainState(const cosmetic_config::item_skin_t &config)
    {
        AppliedKeychainState state{};
        state.effective = config.keychain.enabled && config.keychain.id > 0;

        if (!state.effective)
            return state;

        state.id = config.keychain.id;
        state.seed = (std::clamp)(config.keychain.seed, 0, 100000);
        state.offsetX = config.keychain.offsetX;
        state.offsetY = config.keychain.offsetY;
        state.offsetZ = config.keychain.offsetZ;
        return state;
    }

    bool KeychainStatesEqual(const AppliedKeychainState &lhs, const AppliedKeychainState &rhs)
    {
        if (lhs.effective != rhs.effective)
            return false;

        if (!lhs.effective)
            return true;

        return lhs.id == rhs.id && lhs.seed == rhs.seed && lhs.offsetX == rhs.offsetX && lhs.offsetY == rhs.offsetY &&
               lhs.offsetZ == rhs.offsetZ;
    }

    std::uint64_t ComputeConfigHash(const cosmetic_config::item_skin_t &config)
    {
        constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

        std::uint64_t hash = FNV_OFFSET_BASIS;

        HashPod(hash, config.paintKit);
        HashPod(hash, config.seed);
        HashFloat(hash, config.wear);
        HashPod(hash, config.statTrakEnabled);
        HashPod(hash, config.statTrakKills);

        for (const char c : config.customName)
            HashByte(hash, static_cast<std::uint8_t>(c));

        HashByte(hash, 0xFF);

        for (const auto &sticker : config.stickers)
        {
            const bool effective = sticker.enabled && sticker.kit > 0;
            HashPod(hash, effective);

            if (!effective)
                continue;

            HashPod(hash, sticker.kit);
            HashFloat(hash, sticker.wear);
            HashFloat(hash, sticker.scale);
            HashFloat(hash, sticker.rotation);
            HashFloat(hash, sticker.offsetX);
            HashFloat(hash, sticker.offsetY);
        }

        const bool keychainEffective = config.keychain.enabled && config.keychain.id > 0;
        HashPod(hash, keychainEffective);

        if (keychainEffective)
        {
            HashPod(hash, config.keychain.id);
            HashPod(hash, config.keychain.seed);
            HashFloat(hash, config.keychain.offsetX);
            HashFloat(hash, config.keychain.offsetY);
            HashFloat(hash, config.keychain.offsetZ);
        }

        return hash;
    }

} // namespace features::changer::weapon_cosmetics
