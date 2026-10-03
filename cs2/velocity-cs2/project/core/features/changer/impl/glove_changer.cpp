#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/glove_changer.hpp>

#include <core/features/changer/skinchanger.hpp>

#include <core/features/changer/econ_item_attribute_manager.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <Windows.h>

namespace
{
    constexpr std::uint16_t DEFAULT_T_GLOVES = 5028;
    constexpr std::uint16_t DEFAULT_CT_GLOVES = 5029;

    constexpr std::uint8_t TEAM_T = 2;
    constexpr std::uint8_t TEAM_CT = 3;
    using CreatePaintKitFn = features::changer::engine::generated_paint_kit * (__fastcall*)(void*);
    using SetBodyGroupFn = void(__fastcall*)(void*, const char*, unsigned int);

    std::atomic_bool g_forceUpdate{ true };

    features::changer::engine::player_items* g_lastPawn = nullptr;

    std::uint16_t g_lastDefinition = 0;
    int g_lastPaintKit = -1;
    int g_lastSeed = -1;
    float g_lastWear = -1.0f;
    float g_lastSpawnTime = -1.0f;
    std::uint8_t g_lastTeam = 0xFF;

    bool g_appliedCustomGloves = false;

    CreatePaintKitFn GetCreatePaintKit()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<CreatePaintKitFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::glove_create_paint_kit))); });
        return fn;
    }

    SetBodyGroupFn GetSetBodyGroup()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<SetBodyGroupFn>(features::changer::engine::relative_address(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::glove_set_body_group)), 1)); });
        return fn;
    }

    std::uint16_t GetDefaultGloveDefinition(std::uint8_t team)
    {
        if (team == TEAM_T)
            return DEFAULT_T_GLOVES;

        if (team == TEAM_CT)
            return DEFAULT_CT_GLOVES;

        return 0;
    }

    bool IsDefaultGloveDefinition(std::uint16_t definitionIndex)
    {
        return definitionIndex == DEFAULT_T_GLOVES || definitionIndex == DEFAULT_CT_GLOVES;
    }

    bool ApplyGloves(features::changer::engine::player_items* pawn, std::uint16_t definitionIndex, int paintKit, int seed, float wear)
    {
        if (!pawn || definitionIndex == 0)
            return false;

        features::changer::engine::item_view* item = pawn->m_EconGloves();

        if (!item)
            return false;

        const SetBodyGroupFn setBodyGroup = GetSetBodyGroup();

        if (!setBodyGroup)
            return false;

        CreatePaintKitFn createPaintKit = nullptr;

        if (paintKit > 0)
        {
            createPaintKit = GetCreatePaintKit();

            if (!createPaintKit)
                return false;
        }

        item->m_bInitialized() = false;

        features::changer::attributes::Remove(item);

        item->m_iItemDefinitionIndex() = definitionIndex;
        item->m_bDisallowSOC() = false;
        item->m_bRestoreCustomMaterialAfterPrecache() = true;
        item->m_iItemIDHigh() = IsDefaultGloveDefinition(definitionIndex) ? 0u : 0xFFFFFFFFu;

        if (paintKit > 0)
        {
            features::changer::attributes::Create(item, paintKit, wear, seed, -1, 0);

            features::changer::engine::generated_paint_kit* generatedPaintKit = nullptr;

            __try
            {
                generatedPaintKit = createPaintKit(item);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                item->m_bInitialized() = true;
                return false;
            }

            if (generatedPaintKit)
            {
                if (const features::changer::econ_item_system::paint_kit* paintKitInfo = features::changer::g_econ_item_system.find_paint_kit(paintKit))
                    generatedPaintKit->m_Name = paintKitInfo->name.c_str();
            }
        }

        __try
        {
            setBodyGroup(pawn, "first_or_third_person", 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            item->m_bInitialized() = true;
            return false;
        }

        item->m_bInitialized() = true;
        pawn->m_bNeedToReApplyGloves() = true;

        return true;
    }

    void ClearCachedState()
    {
        g_lastPawn = nullptr;
        g_lastDefinition = 0;
        g_lastPaintKit = -1;
        g_lastSeed = -1;
        g_lastWear = -1.0f;
        g_lastSpawnTime = -1.0f;
        g_lastTeam = 0xFF;
    }
}

void features::changer::glove_application::Run(features::changer::engine::player_items* localPawn)
{
    if (!localPawn || localPawn->m_iHealth() <= 0)
        return;

    // Keep glove/model material regeneration consistent with weapons: continuous
    // menu drags commit once on release instead of rebuilding every frame.
    if (features::changer::application::IsInteractiveEditActive())
        return;

    const std::uint8_t team = localPawn->m_iTeamNum();
    const float spawnTime = localPawn->m_flLastSpawnTimeIndex();

    std::uint16_t definitionIndex = features::changer::cosmetic_config::skin_changer.gloveDefinition;

    int paintKit = 0;
    int seed = 0;
    float wear = 0.0001f;

    bool customSelection = features::changer::cosmetic_config::skin_changer.enabled && definitionIndex != 0;

    if (customSelection)
    {
        const features::changer::econ_item_system::item_def* itemInfo = features::changer::g_econ_item_system.find_def(definitionIndex);

        if (!itemInfo || itemInfo->category != econ_item_system::item_category::glove)
            customSelection = false;
    }

    if (customSelection)
    {
        const auto configIt = features::changer::cosmetic_config::skin_changer.gloves.find(definitionIndex);

        if (configIt != features::changer::cosmetic_config::skin_changer.gloves.end())
        {
            const features::changer::cosmetic_config::item_skin_t& config = configIt->second;

            if (config.enabled && config.paintKit > 0)
            {
                paintKit = config.paintKit;
                seed = (std::clamp)(config.seed, 0, 1000);
                wear = (std::clamp)(config.wear, 0.0001f, 1.0f);
            }
        }
    }
    else
    {
        if (!g_appliedCustomGloves)
            return;

        definitionIndex = GetDefaultGloveDefinition(team);

        if (definitionIndex == 0)
            return;
    }

    const bool stateChanged =
        localPawn != g_lastPawn ||
        definitionIndex != g_lastDefinition ||
        paintKit != g_lastPaintKit ||
        seed != g_lastSeed ||
        wear != g_lastWear ||
        spawnTime != g_lastSpawnTime ||
        team != g_lastTeam;

    if (!stateChanged && !g_forceUpdate.load(std::memory_order_acquire))
        return;

    if (!ApplyGloves(localPawn, definitionIndex, paintKit, seed, wear))
        return;

    g_lastPawn = localPawn;
    g_lastDefinition = definitionIndex;
    g_lastPaintKit = paintKit;
    g_lastSeed = seed;
    g_lastWear = wear;
    g_lastSpawnTime = spawnTime;
    g_lastTeam = team;

    g_appliedCustomGloves = customSelection;

    g_forceUpdate.store(false, std::memory_order_release);
}

void features::changer::glove_application::ForceUpdate()
{
    g_forceUpdate.store(true, std::memory_order_release);
}

void features::changer::glove_application::Reset()
{
    g_forceUpdate.store(true, std::memory_order_release);
    ClearCachedState();
    g_appliedCustomGloves = false;
}
