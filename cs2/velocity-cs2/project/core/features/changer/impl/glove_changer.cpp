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

namespace features::changer
{
    namespace
    {
        constexpr std::uint16_t DEFAULT_T_GLOVES = 5028;
        constexpr std::uint16_t DEFAULT_CT_GLOVES = 5029;

        constexpr std::uint8_t TEAM_T = 2;
        constexpr std::uint8_t TEAM_CT = 3;
        using CreatePaintKitFn = engine::generated_paint_kit *(__fastcall *)(void *);
        using SetBodyGroupFn = void(__fastcall *)(void *, const char *, unsigned int);

        std::atomic_bool g_forceUpdate{true};

        struct GloveState
        {
            engine::player_items *pawn = nullptr;
            std::uint16_t definition = 0;
            int paintKit = -1;
            int seed = -1;
            float wear = -1.0f;
            float spawnTime = -1.0f;
            std::uint8_t team = 0xFF;

            bool operator==(const GloveState &) const = default;
        };

        GloveState g_lastApplied;
        bool g_appliedCustomGloves = false;

        CreatePaintKitFn GetCreatePaintKit()
        {
            return engine::cached_address(
                [] { return reinterpret_cast<CreatePaintKitFn>(PATTERN(patterns::glove_create_paint_kit)); });
        }

        SetBodyGroupFn GetSetBodyGroup()
        {
            return engine::cached_address(
                []
                {
                    return reinterpret_cast<SetBodyGroupFn>(engine::relative_address(
                        reinterpret_cast<std::uint8_t *>(PATTERN(patterns::glove_set_body_group)), 1));
                });
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

        bool UpdateGloveVisuals(engine::player_items *pawn, engine::item_view *item, CreatePaintKitFn createPaintKit,
                                SetBodyGroupFn setBodyGroup, int paintKit)
        {
            // Preserve recovery for native model/material calls in one boundary;
            // ApplyGloves restores item initialization on either success or failure.
            __try
            {
                if (createPaintKit)
                {
                    if (auto *generated = createPaintKit(item))
                    {
                        if (const auto *info = g_econ_item_system.find_paint_kit(paintKit))
                            generated->m_Name = info->name.c_str();
                    }
                }

                setBodyGroup(pawn, "first_or_third_person", 1);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ApplyGloves(engine::player_items *pawn, std::uint16_t definitionIndex, int paintKit, int seed, float wear)
        {
            if (!pawn || definitionIndex == 0)
                return false;

            engine::item_view *item = pawn->m_EconGloves();

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

            attributes::Remove(item);

            item->m_iItemDefinitionIndex() = definitionIndex;
            item->m_bDisallowSOC() = false;
            item->m_bRestoreCustomMaterialAfterPrecache() = true;
            item->m_iItemIDHigh() = IsDefaultGloveDefinition(definitionIndex) ? 0u : 0xFFFFFFFFu;

            if (paintKit > 0)
                attributes::Create(item, paintKit, wear, seed, -1, 0);

            const bool updated = UpdateGloveVisuals(pawn, item, createPaintKit, setBodyGroup, paintKit);
            item->m_bInitialized() = true;
            if (!updated)
                return false;

            pawn->m_bNeedToReApplyGloves() = true;

            return true;
        }

    } // namespace

    void glove_application::Run(engine::player_items *localPawn)
    {
        if (!localPawn || localPawn->m_iHealth() <= 0)
            return;

        // Keep glove/model material regeneration consistent with weapons: continuous
        // menu drags commit once on release instead of rebuilding every frame.
        if (application::IsInteractiveEditActive())
            return;

        const std::uint8_t team = localPawn->m_iTeamNum();
        const float spawnTime = localPawn->m_flLastSpawnTimeIndex();

        std::uint16_t definitionIndex = cosmetic_config::skin_changer.gloveDefinition;

        int paintKit = 0;
        int seed = 0;
        float wear = 0.0001f;

        bool customSelection = cosmetic_config::skin_changer.enabled && definitionIndex != 0;

        if (customSelection)
        {
            const econ_item_system::item_def *itemInfo = g_econ_item_system.find_def(definitionIndex);

            if (!itemInfo || itemInfo->category != econ_item_system::item_category::glove)
                customSelection = false;
        }

        if (customSelection)
        {
            const auto configIt = cosmetic_config::skin_changer.gloves.find(definitionIndex);

            if (configIt != cosmetic_config::skin_changer.gloves.end())
            {
                const cosmetic_config::item_skin_t &config = configIt->second;

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

        const GloveState desired{localPawn, definitionIndex, paintKit, seed, wear, spawnTime, team};
        if (desired == g_lastApplied && !g_forceUpdate.load(std::memory_order_acquire))
            return;

        if (!ApplyGloves(localPawn, definitionIndex, paintKit, seed, wear))
            return;

        g_lastApplied = desired;
        g_appliedCustomGloves = customSelection;

        g_forceUpdate.store(false, std::memory_order_release);
    }

    void glove_application::ForceUpdate()
    {
        g_forceUpdate.store(true, std::memory_order_release);
    }

    void glove_application::Reset()
    {
        g_forceUpdate.store(true, std::memory_order_release);
        g_lastApplied = {};
        g_appliedCustomGloves = false;
    }

} // namespace features::changer
