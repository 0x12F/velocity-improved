#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/skin_application.hpp>

#include <core/features/changer/econ_item_attribute_manager.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <excpt.h>

namespace features::changer
{
    namespace
    {
        constexpr int ITEM_QUALITY_NORMAL = 0;
        constexpr int ITEM_QUALITY_UNUSUAL = 3;
        constexpr int ITEM_QUALITY_STRANGE = 9;

        using BuildModernWeaponSkinMaterialFn = void(__fastcall *)(engine::item_view *, void *, std::int64_t, int, char,
                                                                   char, void *);
        using ReleaseCompositeMaterialsFn = void(__fastcall *)(engine::composite_material_owner *, bool);

        struct WeaponStickerState
        {
            std::uint16_t definitionIndex = 0;
            int effectiveCount = 0;
        };

        // Track the previous count so the native release is only used for N -> 0.
        std::unordered_map<std::uintptr_t, WeaponStickerState> g_WeaponStickerStates;

        int GetEffectiveStickerCount(const cosmetic_config::item_skin_t &config)
        {
            int count = 0;

            for (const auto &sticker : config.stickers)
            {
                if (sticker.enabled && sticker.kit > 0)
                    ++count;
            }

            return count;
        }

        bool UpdateStickerState(engine::weapon *weapon, engine::item_view *item, int effectiveCount)
        {
            if (!weapon || !item)
                return false;

            const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(weapon);
            const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();
            const auto it = g_WeaponStickerStates.find(key);

            bool finalStickerRemoved = false;

            if (it != g_WeaponStickerStates.end() && it->second.definitionIndex == definitionIndex)
                finalStickerRemoved = it->second.effectiveCount > 0 && effectiveCount == 0;

            g_WeaponStickerStates[key] = {definitionIndex, effectiveCount};
            return finalStickerRemoved;
        }

        ReleaseCompositeMaterialsFn GetReleaseCompositeMaterials()
        {
            return engine::cached_address(
                []
                {
                    return reinterpret_cast<ReleaseCompositeMaterialsFn>(
                        PATTERN(patterns::release_composite_materials));
                });
        }

        void ReleaseEntityCompositeMaterials(engine::base_entity *entity, bool stampGeneration)
        {
            engine::composite_material_owner *owner = engine::composite_owner(entity);
            if (!owner)
                return;

            const auto release = GetReleaseCompositeMaterials();

            if (!release)
                return;

            // Keep recovery around the native release; a null check cannot validate
            // the engine-owned composite material state.
            __try
            {
                release(owner, stampGeneration);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        int GetBaseQuality(skin_application::ItemKind kind)
        {
            return kind == skin_application::ItemKind::Knife ? ITEM_QUALITY_UNUSUAL : ITEM_QUALITY_NORMAL;
        }

        int GetDesiredQuality(const cosmetic_config::item_skin_t &config, skin_application::ItemKind kind)
        {
            return config.statTrakEnabled ? ITEM_QUALITY_STRANGE : GetBaseQuality(kind);
        }

        int GetDesiredStatTrak(const cosmetic_config::item_skin_t &config)
        {
            return config.statTrakEnabled ? (std::max)(0, config.statTrakKills) : -1;
        }

        float GetDesiredWear(const cosmetic_config::item_skin_t &config)
        {
            return std::clamp(config.wear, 0.0001f, 1.0f);
        }

        int GetDesiredSeed(const cosmetic_config::item_skin_t &config)
        {
            return std::clamp(config.seed, 0, 1000);
        }

        engine::base_entity *FindHudWeapon(engine::base_entity *weapon, engine::player_items *localPawn)
        {
            if (!weapon || !localPawn || !addresses::globals::game_entity_system)
                return nullptr;

            const engine::item_handle armsHandle = localPawn->m_hHudModelArms();

            if (!armsHandle.valid())
                return nullptr;

            engine::base_entity *hudArms =
                reinterpret_cast<engine::base_entity *>(systems::g_entities.get_by_index(armsHandle.index()));

            if (!hudArms)
                return nullptr;

            engine::scene_node *armsNode = hudArms->m_pGameSceneNode();

            if (!armsNode)
                return nullptr;

            for (engine::scene_node *node = armsNode->GetChild(); node; node = node->next_sibling())
            {
                engine::entity *nodeOwner = node->GetOwner();

                if (!nodeOwner)
                    continue;

                engine::base_entity *hudEntity = reinterpret_cast<engine::base_entity *>(nodeOwner);
                const engine::item_handle ownerHandle = hudEntity->m_hOwnerEntity();

                if (!ownerHandle.valid())
                    continue;

                engine::base_entity *ownerEntity =
                    reinterpret_cast<engine::base_entity *>(systems::g_entities.get_by_index(ownerHandle.index()));

                if (ownerEntity == weapon)
                    return hudEntity;
            }

            return nullptr;
        }

        std::uintptr_t FindHudElement(const char *name)
        {
            using Fn = std::uintptr_t(__fastcall *)(const char *);

            const auto fn =
                engine::cached_address([] { return reinterpret_cast<Fn>(PATTERN(patterns::find_hud_element)); });
            return fn ? fn(name) : 0;
        }

        void ClearHudWeaponIconAtIndex(std::uintptr_t hudWeapons, std::int32_t index, std::int64_t unknown)
        {
            const auto callAddress = engine::cached_address(
                [] { return reinterpret_cast<std::uint8_t *>(PATTERN(patterns::clear_hud_weapon)); });

            if (!callAddress)
                return;

            using Fn = std::int64_t(__fastcall *)(std::uintptr_t, std::int32_t, std::int64_t);

            const auto fn = reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(callAddress) + 5 +
                                                 *reinterpret_cast<std::int32_t *>(callAddress + 1));

            fn(hudWeapons, index, unknown);
        }

        BuildModernWeaponSkinMaterialFn GetBuildModernWeaponSkinMaterial()
        {
            return engine::cached_address(
                [] { return reinterpret_cast<BuildModernWeaponSkinMaterialFn>(PATTERN(patterns::build_modern_skin)); });
        }

        void BuildModernWeaponSkinMaterial(engine::item_view *item, void *model)
        {
            if (!item || !model)
                return;

            const auto fn = GetBuildModernWeaponSkinMaterial();

            if (fn)
                fn(item, model, 0, 2048, 0, 0, nullptr);
        }

        void SetCustomName(engine::item_view *item, const std::string &name)
        {
            if (!item)
                return;

            char *customName = item->m_szCustomName();

            if (!customName)
                return;

            if (name.empty())
            {
                customName[0] = '\0';
                return;
            }

            strncpy_s(customName, 161, name.c_str(), _TRUNCATE);
        }

        std::uint64_t GetMeshMask(int paintKit)
        {
            const econ_item_system::paint_kit *paintKitInfo = g_econ_item_system.find_paint_kit(paintKit);
            return paintKitInfo && paintKitInfo->legacy_model ? 2ULL : 1ULL;
        }

        void SetMeshGroup(engine::base_entity *entity, std::uint64_t meshMask)
        {
            if (!entity)
                return;

            if (engine::scene_node *sceneNode = entity->m_pGameSceneNode())
                sceneNode->SetMeshGroupMask(meshMask);
        }

        void ApplyWeaponMaterial(engine::weapon *weapon, engine::item_view *item, engine::player_items *localPawn,
                                 std::uint64_t meshMask, bool finalStickerRemoved, bool replacementCompositeExpected)
        {
            engine::base_entity *hudWeapon = FindHudWeapon(weapon, localPawn);

            if (finalStickerRemoved)
            {
                // If a paint kit remains, clear the old composite ownership without running
                // the derived empty-material callback. The replacement paint-only composite
                // is built immediately below, avoiding a transient stock-material reapply.
                // The HUD model has its own owner and must be cleared separately on N -> 0.
                const bool stampGeneration = !replacementCompositeExpected;
                ReleaseEntityCompositeMaterials(weapon, stampGeneration);
                ReleaseEntityCompositeMaterials(hudWeapon, stampGeneration);
            }

            SetMeshGroup(weapon, meshMask);
            SetMeshGroup(hudWeapon, meshMask);

            BuildModernWeaponSkinMaterial(item, weapon);

            if (hudWeapon)
                BuildModernWeaponSkinMaterial(item, hudWeapon);
        }

        void ApplyKnifeMaterial(engine::weapon *weapon, engine::item_view *item, engine::player_items *localPawn,
                                std::uint64_t meshMask)
        {
            SetMeshGroup(weapon, meshMask);
            BuildModernWeaponSkinMaterial(item, weapon);

            engine::base_entity *hudWeapon = FindHudWeapon(weapon, localPawn);

            SetMeshGroup(hudWeapon, meshMask);

            if (hudWeapon)
                BuildModernWeaponSkinMaterial(item, hudWeapon);
        }
    } // namespace

    bool skin_application::RebuildHudKeychainAddon(engine::base_entity *weapon, engine::player_items *localPawn)
    {
        engine::base_entity *hudWeapon = FindHudWeapon(weapon, localPawn);

        if (!hudWeapon || !weapon || !addresses::globals::game_entity_system)
            return false;

        constexpr std::uint32_t KEYCHAIN_HASH = 0x0AA5272A; // Native dictionary hash of "keychain".
        using AddonDictFindFn =
            std::uint32_t(__fastcall *)(engine::HudAddonDictionary *, const char *, std::uint32_t, bool *);
        using UtilRemoveFn = void(__fastcall *)(engine::base_entity *);

        const auto findAddon = engine::cached_address(
            [] { return reinterpret_cast<AddonDictFindFn>(PATTERN(patterns::find_hud_keychain_addon)); });

        const auto utilRemove = engine::cached_address(
            [] { return reinterpret_cast<UtilRemoveFn>(PATTERN(patterns::remove_hud_keychain_addon)); });

        if (!findAddon || !utilRemove)
            return false;

        const engine::item_handle worldWeaponHandle = hudWeapon->m_hOwnerEntity();
        if (!worldWeaponHandle.valid())
            return false;

        engine::base_entity *resolvedWorldWeapon =
            reinterpret_cast<engine::base_entity *>(systems::g_entities.get_by_index(worldWeaponHandle.index()));
        if (resolvedWorldWeapon != weapon)
            return false;

        auto *hud = reinterpret_cast<engine::HudWeaponAddonLayout *>(hudWeapon);
        engine::HudAddonDictionary &dictionary = hud->addons;
        const std::uint32_t addonIndex = findAddon(&dictionary, "keychain", KEYCHAIN_HASH, nullptr);

        if (addonIndex != engine::INVALID_EHANDLE_INDEX)
        {
            if (addonIndex >= dictionary.Capacity() || !dictionary.entries)
                return false;

            engine::item_handle &addonHandle = dictionary.entries[addonIndex].entityHandle;
            if (addonHandle.valid())
            {
                if (engine::base_entity *addon =
                        reinterpret_cast<engine::base_entity *>(systems::g_entities.get_by_index(addonHandle.index())))
                    utilRemove(addon);

                addonHandle = engine::item_handle{};
            }
        }

        hud->keychainState = 0xFFu;
        return true;
    }

    bool skin_application::NeedsUpdate(engine::weapon *weapon, engine::item_view *item,
                                       const cosmetic_config::item_skin_t &config, bool forceUpdate, ItemKind kind)
    {
        if (!weapon || !item)
            return false;

        if (forceUpdate)
            return true;

        if (weapon->m_nFallbackPaintKit() != config.paintKit)
            return true;

        if (weapon->m_nFallbackSeed() != GetDesiredSeed(config))
            return true;

        if (weapon->m_flFallbackWear() != GetDesiredWear(config))
            return true;

        if (weapon->m_nFallbackStatTrak() != GetDesiredStatTrak(config))
            return true;

        if (item->m_iEntityQuality() != GetDesiredQuality(config, kind))
            return true;

        char *customName = item->m_szCustomName();

        if (!customName)
            return !config.customName.empty();

        return std::strncmp(customName, config.customName.c_str(), 160) != 0;
    }

    bool skin_application::IsDefaultState(engine::weapon *weapon, engine::item_view *item, ItemKind kind)
    {
        if (!weapon || !item)
            return true;

        if (weapon->m_nFallbackPaintKit() != 0)
            return false;

        if (weapon->m_nFallbackStatTrak() != -1)
            return false;

        if (item->m_iEntityQuality() != GetBaseQuality(kind))
            return false;

        char *customName = item->m_szCustomName();

        return !customName || customName[0] == '\0';
    }

    void skin_application::Apply(engine::weapon *weapon, engine::item_view *item,
                                 const cosmetic_config::item_skin_t &config, engine::player_items *localPawn,
                                 ItemKind kind)
    {
        if (!weapon || !item || !localPawn)
            return;

        const bool finalStickerRemoved =
            kind == ItemKind::Weapon && UpdateStickerState(weapon, item, GetEffectiveStickerCount(config));

        const float wear = GetDesiredWear(config);
        const int seed = GetDesiredSeed(config);
        const int statTrak = GetDesiredStatTrak(config);

        attributes::Remove(item);

        item->m_iItemIDHigh() = 0xFFFFFFFFu;
        item->m_bInitialized() = true;
        item->m_bDisallowSOC() = false;
        item->m_bRestoreCustomMaterialAfterPrecache() = true;
        item->m_iEntityQuality() = GetDesiredQuality(config, kind);

        weapon->m_nFallbackPaintKit() = config.paintKit;
        weapon->m_nFallbackSeed() = seed;
        weapon->m_flFallbackWear() = wear;
        weapon->m_nFallbackStatTrak() = statTrak;

        attributes::StickerArray stickers{};
        attributes::KeychainData keychain{};

        if (kind == ItemKind::Weapon)
        {
            stickers = config.stickers;
            keychain = config.keychain;
            keychain.enabled = config.keychain.enabled && config.keychain.id > 0;
        }

        attributes::Create(item, config.paintKit, wear, seed, statTrak, 0,
                           kind == ItemKind::Weapon ? &stickers : nullptr,
                           kind == ItemKind::Weapon ? &keychain : nullptr);

        // Keep the live keychain hidden from the normal HUD/material refresh; the
        // deferred keychain pass restores the ID and rebuilds the native world/HUD charm.
        if (kind == ItemKind::Weapon && keychain.enabled)
            attributes::SetKeychainId(item, 0);

        SetCustomName(item, config.customName);
        item->m_name_description_ptr() = 0;

        const std::uint64_t meshMask = GetMeshMask(config.paintKit);

        if (kind == ItemKind::Knife)
        {
            ApplyKnifeMaterial(weapon, item, localPawn, meshMask);
            return;
        }

        ApplyWeaponMaterial(weapon, item, localPawn, meshMask, finalStickerRemoved, config.paintKit > 0);
    }

    void skin_application::ApplyDefault(engine::weapon *weapon, engine::item_view *item,
                                        engine::player_items *localPawn, ItemKind kind)
    {
        if (!weapon || !item || !localPawn)
            return;

        const bool finalStickerRemoved = kind == ItemKind::Weapon && UpdateStickerState(weapon, item, 0);

        attributes::Remove(item);

        item->m_iEntityQuality() = GetBaseQuality(kind);
        item->m_bInitialized() = true;
        item->m_bDisallowSOC() = false;
        item->m_bRestoreCustomMaterialAfterPrecache() = true;

        weapon->m_nFallbackPaintKit() = 0;
        weapon->m_nFallbackSeed() = 0;
        weapon->m_flFallbackWear() = 0.0f;
        weapon->m_nFallbackStatTrak() = -1;

        SetCustomName(item, {});
        item->m_name_description_ptr() = 0;

        if (kind == ItemKind::Knife)
        {
            SetMeshGroup(weapon, 1ULL);

            engine::base_entity *hudWeapon = FindHudWeapon(weapon, localPawn);

            SetMeshGroup(hudWeapon, 1ULL);
            return;
        }

        engine::base_entity *hudWeapon = FindHudWeapon(weapon, localPawn);

        if (finalStickerRemoved)
        {
            // No custom paint composite will replace the sticker material in the default
            // state, so use the native stamped release to reapply the base material now.
            ReleaseEntityCompositeMaterials(weapon, true);
            ReleaseEntityCompositeMaterials(hudWeapon, true);
        }

        weapon->PostDataUpdate(1);

        SetMeshGroup(weapon, 1ULL);
        SetMeshGroup(hudWeapon, 1ULL);
    }

    void skin_application::ClearHudWeaponIconFor(engine::base_entity *weapon)
    {
        if (!weapon || !addresses::globals::game_entity_system)
            return;

        // HUD selection owns this mutable array and invokes native removal code.
        // Retain its existing recovery boundary while refactoring the application flow.
        __try
        {
            const std::uintptr_t hud = FindHudElement("HudWeaponSelection");

            if (!hud)
                return;

            auto *selection = engine::HudWeaponSelectionLayout::FromHudElement(hud);
            const engine::HudWeaponSelectionEntry *data = selection->entries;
            const std::int32_t count = selection->count;

            if (!data || count <= 0 || count > 64)
                return;

            for (std::int32_t i = count - 1; i >= 0; --i)
            {
                const std::int32_t handle = data[i].weaponHandle;

                if (handle < 0)
                    continue;

                engine::base_entity *hudWeapon = reinterpret_cast<engine::base_entity *>(
                    systems::g_entities.get_by_index(handle & engine::entry_mask));

                if (hudWeapon == weapon)
                {
                    ClearHudWeaponIconAtIndex(reinterpret_cast<std::uintptr_t>(selection), i, 0);
                    return;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void skin_application::RegenerateSkins()
    {
        using Fn = void(__fastcall *)();

        const Fn fn = engine::cached_address([] { return reinterpret_cast<Fn>(PATTERN(patterns::regenerate_skins)); });

        if (fn)
            fn();
    }

} // namespace features::changer
