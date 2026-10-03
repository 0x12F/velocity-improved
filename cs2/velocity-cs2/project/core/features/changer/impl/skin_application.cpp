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

namespace
{
    constexpr int ITEM_QUALITY_NORMAL = 0;
    constexpr int ITEM_QUALITY_UNUSUAL = 3;
    constexpr int ITEM_QUALITY_STRANGE = 9;

    using BuildModernWeaponSkinMaterialFn = void(__fastcall*)(features::changer::engine::item_view*, void*, std::int64_t, int, char, char, void*);
    using ReleaseCompositeMaterialsFn = void(__fastcall*)(features::changer::engine::composite_material_owner*, bool);

    struct WeaponStickerState
    {
        std::uint16_t definitionIndex = 0;
        int effectiveCount = 0;
    };

    // Track the previous count so the native release is only used for N -> 0.
    std::unordered_map<std::uintptr_t, WeaponStickerState> g_WeaponStickerStates;

    int GetEffectiveStickerCount(const features::changer::cosmetic_config::item_skin_t& config)
    {
        int count = 0;

        for (const auto& sticker : config.stickers)
        {
            if (sticker.enabled && sticker.kit > 0)
                ++count;
        }

        return count;
    }

    bool UpdateStickerState(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, int effectiveCount)
    {
        if (!weapon || !item)
            return false;

        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(weapon);
        const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();
        const auto it = g_WeaponStickerStates.find(key);

        bool finalStickerRemoved = false;

        if (it != g_WeaponStickerStates.end() && it->second.definitionIndex == definitionIndex)
            finalStickerRemoved = it->second.effectiveCount > 0 && effectiveCount == 0;

        g_WeaponStickerStates[key] = { definitionIndex, effectiveCount };
        return finalStickerRemoved;
    }

    ReleaseCompositeMaterialsFn GetReleaseCompositeMaterials()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<ReleaseCompositeMaterialsFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::release_composite_materials))); });
        return fn;
    }

    void ReleaseEntityCompositeMaterials(features::changer::engine::base_entity* entity, bool stampGeneration)
    {
        features::changer::engine::composite_material_owner* owner = features::changer::engine::composite_owner(entity);
        if (!owner)
            return;

        const auto release = GetReleaseCompositeMaterials();

        if (!release)
            return;

        __try
        {
            release(owner, stampGeneration);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    int GetBaseQuality(features::changer::skin_application::ItemKind kind)
    {
        return kind == features::changer::skin_application::ItemKind::Knife ? ITEM_QUALITY_UNUSUAL : ITEM_QUALITY_NORMAL;
    }

    int GetDesiredQuality(const features::changer::cosmetic_config::item_skin_t& config, features::changer::skin_application::ItemKind kind)
    {
        return config.statTrakEnabled ? ITEM_QUALITY_STRANGE : GetBaseQuality(kind);
    }

    int GetDesiredStatTrak(const features::changer::cosmetic_config::item_skin_t& config)
    {
        return config.statTrakEnabled ? (std::max)(0, config.statTrakKills) : -1;
    }

    float GetDesiredWear(const features::changer::cosmetic_config::item_skin_t& config)
    {
        return std::clamp(config.wear, 0.0001f, 1.0f);
    }

    int GetDesiredSeed(const features::changer::cosmetic_config::item_skin_t& config)
    {
        return std::clamp(config.seed, 0, 1000);
    }

    features::changer::engine::base_entity* FindHudWeapon(features::changer::engine::base_entity* weapon, features::changer::engine::player_items* localPawn)
    {
        if (!weapon || !localPawn || !addresses::globals::game_entity_system)
            return nullptr;

        const features::changer::engine::item_handle armsHandle = localPawn->m_hHudModelArms();

        if (!armsHandle.valid())
            return nullptr;

        features::changer::engine::base_entity* hudArms = reinterpret_cast<features::changer::engine::base_entity*>(systems::g_entities.get_by_index(armsHandle.index()));

        if (!hudArms)
            return nullptr;

        features::changer::engine::scene_node* armsNode = hudArms->m_pGameSceneNode();

        if (!armsNode)
            return nullptr;

        for (features::changer::engine::scene_node* node = armsNode->GetChild(); node; node = node->next_sibling())
        {
            features::changer::engine::entity* nodeOwner = node->GetOwner();

            if (!nodeOwner)
                continue;

            features::changer::engine::base_entity* hudEntity = reinterpret_cast<features::changer::engine::base_entity*>(nodeOwner);
            const features::changer::engine::item_handle ownerHandle = hudEntity->m_hOwnerEntity();

            if (!ownerHandle.valid())
                continue;

            features::changer::engine::base_entity* ownerEntity = reinterpret_cast<features::changer::engine::base_entity*>(systems::g_entities.get_by_index(ownerHandle.index()));

            if (ownerEntity == weapon)
                return hudEntity;
        }

        return nullptr;
    }

    std::uintptr_t FindHudElement(const char* name)
    {
        using Fn = std::uintptr_t(__fastcall*)(const char*);

        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<Fn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::find_hud_element))); });
        return fn ? fn(name) : 0;
    }

    void ClearHudWeaponIconAtIndex(std::uintptr_t hudWeapons, std::int32_t index, std::int64_t unknown)
    {
        const auto callAddress = features::changer::engine::cached_address([] { return reinterpret_cast<std::uint8_t*>(PATTERN(patterns::clear_hud_weapon)); });

        if (!callAddress)
            return;

        using Fn = std::int64_t(__fastcall*)(std::uintptr_t, std::int32_t, std::int64_t);

        const auto fn = reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(callAddress) + 5 + *reinterpret_cast<std::int32_t*>(callAddress + 1));

        fn(hudWeapons, index, unknown);
    }

    BuildModernWeaponSkinMaterialFn GetBuildModernWeaponSkinMaterial()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<BuildModernWeaponSkinMaterialFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::build_modern_skin))); });
        return fn;
    }

    void BuildModernWeaponSkinMaterial(features::changer::engine::item_view* item, void* model)
    {
        if (!item || !model)
            return;

        const auto fn = GetBuildModernWeaponSkinMaterial();

        if (fn)
            fn(item, model, 0, 2048, 0, 0, nullptr);
    }

    void SetCustomName(features::changer::engine::item_view* item, const std::string& name)
    {
        if (!item)
            return;

        char* customName = item->m_szCustomName();

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
        const features::changer::econ_item_system::paint_kit* paintKitInfo = features::changer::g_econ_item_system.find_paint_kit(paintKit);
        return paintKitInfo && paintKitInfo->legacy_model ? 2ULL : 1ULL;
    }

    void SetMeshGroup(features::changer::engine::base_entity* entity, std::uint64_t meshMask)
    {
        if (!entity)
            return;

        if (features::changer::engine::scene_node* sceneNode = entity->m_pGameSceneNode())
            sceneNode->SetMeshGroupMask(meshMask);
    }

    void ApplyWeaponMaterial(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, std::uint64_t meshMask, bool finalStickerRemoved, bool replacementCompositeExpected)
    {
        features::changer::engine::base_entity* hudWeapon = FindHudWeapon(weapon, localPawn);

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

    void ApplyKnifeMaterial(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, std::uint64_t meshMask)
    {
        SetMeshGroup(weapon, meshMask);
        BuildModernWeaponSkinMaterial(item, weapon);

        features::changer::engine::base_entity* hudWeapon = FindHudWeapon(weapon, localPawn);

        SetMeshGroup(hudWeapon, meshMask);

        if (hudWeapon)
            BuildModernWeaponSkinMaterial(item, hudWeapon);

    }
}

bool features::changer::skin_application::RebuildHudKeychainAddon(features::changer::engine::base_entity* weapon, features::changer::engine::player_items* localPawn)
{
    features::changer::engine::base_entity* hudWeapon = FindHudWeapon(weapon, localPawn);

    if (!hudWeapon || !weapon || !addresses::globals::game_entity_system)
        return false;

    constexpr std::uint32_t KEYCHAIN_HASH = 0x0AA5272A; // Native dictionary hash of "keychain".
    using AddonDictFindFn = std::uint32_t(__fastcall*)(features::changer::engine::HudAddonDictionary*, const char*, std::uint32_t, bool*);
    using UtilRemoveFn = void(__fastcall*)(features::changer::engine::base_entity*);

    const auto findAddon = features::changer::engine::cached_address([] { return reinterpret_cast<AddonDictFindFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::find_hud_keychain_addon))); });

    const auto utilRemove = features::changer::engine::cached_address([] { return reinterpret_cast<UtilRemoveFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::remove_hud_keychain_addon))); });

    if (!findAddon || !utilRemove)
        return false;

    const features::changer::engine::item_handle worldWeaponHandle = hudWeapon->m_hOwnerEntity();
    if (!worldWeaponHandle.valid())
        return false;

    features::changer::engine::base_entity* resolvedWorldWeapon = reinterpret_cast<features::changer::engine::base_entity*>(systems::g_entities.get_by_index(worldWeaponHandle.index()));
    if (resolvedWorldWeapon != weapon)
        return false;

    auto* hud = reinterpret_cast<features::changer::engine::HudWeaponAddonLayout*>(hudWeapon);
    features::changer::engine::HudAddonDictionary& dictionary = hud->addons;
    const std::uint32_t addonIndex = findAddon(&dictionary, "keychain", KEYCHAIN_HASH, nullptr);

    if (addonIndex != engine::INVALID_EHANDLE_INDEX)
    {
        if (addonIndex >= dictionary.Capacity() || !dictionary.entries)
            return false;

        features::changer::engine::item_handle& addonHandle = dictionary.entries[addonIndex].entityHandle;
        if (addonHandle.valid())
        {
            if (features::changer::engine::base_entity* addon = reinterpret_cast<features::changer::engine::base_entity*>(systems::g_entities.get_by_index(addonHandle.index())))
                utilRemove(addon);

            addonHandle = features::changer::engine::item_handle{};
        }
    }

    hud->keychainState = 0xFFu;
    return true;
}

bool features::changer::skin_application::NeedsUpdate(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, const features::changer::cosmetic_config::item_skin_t& config, bool forceUpdate, ItemKind kind)
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

    char* customName = item->m_szCustomName();

    if (!customName)
        return !config.customName.empty();

    return std::strncmp(customName, config.customName.c_str(), 160) != 0;
}

bool features::changer::skin_application::IsDefaultState(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, ItemKind kind)
{
    if (!weapon || !item)
        return true;

    if (weapon->m_nFallbackPaintKit() != 0)
        return false;

    if (weapon->m_nFallbackStatTrak() != -1)
        return false;

    if (item->m_iEntityQuality() != GetBaseQuality(kind))
        return false;

    char* customName = item->m_szCustomName();

    return !customName || customName[0] == '\0';
}

void features::changer::skin_application::Apply(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, const features::changer::cosmetic_config::item_skin_t& config, features::changer::engine::player_items* localPawn, ItemKind kind)
{
    if (!weapon || !item || !localPawn)
        return;

    const bool finalStickerRemoved =
        kind == ItemKind::Weapon && UpdateStickerState(weapon, item, GetEffectiveStickerCount(config));

    const float wear = GetDesiredWear(config);
    const int seed = GetDesiredSeed(config);
    const int statTrak = GetDesiredStatTrak(config);

    features::changer::attributes::Remove(item);

    item->m_iItemIDHigh() = 0xFFFFFFFFu;
    item->m_bInitialized() = true;
    item->m_bDisallowSOC() = false;
    item->m_bRestoreCustomMaterialAfterPrecache() = true;
    item->m_iEntityQuality() = GetDesiredQuality(config, kind);

    weapon->m_nFallbackPaintKit() = config.paintKit;
    weapon->m_nFallbackSeed() = seed;
    weapon->m_flFallbackWear() = wear;
    weapon->m_nFallbackStatTrak() = statTrak;

    features::changer::attributes::StickerArray stickers{};
    features::changer::attributes::KeychainData keychain{};

    if (kind == ItemKind::Weapon)
    {
        for (std::size_t i = 0; i < stickers.size() && i < config.stickers.size(); ++i)
        {
            stickers[i].enabled = config.stickers[i].enabled;
            stickers[i].kit = config.stickers[i].kit;
            stickers[i].wear = config.stickers[i].wear;
            stickers[i].scale = config.stickers[i].scale;
            stickers[i].rotation = config.stickers[i].rotation;
            stickers[i].offsetX = config.stickers[i].offsetX;
            stickers[i].offsetY = config.stickers[i].offsetY;
        }

        keychain.enabled = config.keychain.enabled && config.keychain.id > 0;
        keychain.id = config.keychain.id;
        keychain.seed = config.keychain.seed;
        keychain.offsetX = config.keychain.offsetX;
        keychain.offsetY = config.keychain.offsetY;
        keychain.offsetZ = config.keychain.offsetZ;
    }

    features::changer::attributes::Create(
        item,
        config.paintKit,
        wear,
        seed,
        statTrak,
        0,
        kind == ItemKind::Weapon ? &stickers : nullptr,
        kind == ItemKind::Weapon ? &keychain : nullptr);

    // Keep the live keychain hidden from the normal HUD/material refresh; the
    // deferred keychain pass restores the ID and rebuilds the native world/HUD charm.
    if (kind == ItemKind::Weapon && keychain.enabled)
        features::changer::attributes::SetKeychainId(item, 0);

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

void features::changer::skin_application::ApplyDefault(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, ItemKind kind)
{
    if (!weapon || !item || !localPawn)
        return;

    const bool finalStickerRemoved =
        kind == ItemKind::Weapon && UpdateStickerState(weapon, item, 0);

    features::changer::attributes::Remove(item);

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

        features::changer::engine::base_entity* hudWeapon = FindHudWeapon(weapon, localPawn);

        SetMeshGroup(hudWeapon, 1ULL);
        return;
    }

    features::changer::engine::base_entity* hudWeapon = FindHudWeapon(weapon, localPawn);

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

void features::changer::skin_application::ClearHudWeaponIconFor(features::changer::engine::base_entity* weapon)
{
    if (!weapon || !addresses::globals::game_entity_system)
        return;

    __try
    {
        const std::uintptr_t hud = FindHudElement("HudWeaponSelection");

        if (!hud)
            return;

        auto* selection = features::changer::engine::HudWeaponSelectionLayout::FromHudElement(hud);
        const features::changer::engine::HudWeaponSelectionEntry* data = selection->entries;
        const std::int32_t count = selection->count;

        if (!data || count <= 0 || count > 64)
            return;

        for (std::int32_t i = count - 1; i >= 0; --i)
        {
            const std::int32_t handle = data[i].weaponHandle;

            if (handle < 0)
                continue;

            features::changer::engine::base_entity* hudWeapon = reinterpret_cast<features::changer::engine::base_entity*>(systems::g_entities.get_by_index(handle & features::changer::engine::entry_mask));

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

void features::changer::skin_application::RegenerateSkins()
{
    using Fn = void(__fastcall*)();

    const Fn fn = features::changer::engine::cached_address([] { return reinterpret_cast<Fn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::regenerate_skins))); });

    if (fn)
        fn();
}
