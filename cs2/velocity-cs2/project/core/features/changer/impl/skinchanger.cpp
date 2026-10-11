#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/skinchanger.hpp>

#include <core/features/changer/skin_application.hpp>
#include <core/features/changer/econ_item_attribute_manager.hpp>
#include <core/features/changer/glove_changer.hpp>
#include <core/features/changer/knife_model_changer.hpp>
#include <core/features/changer/weapon_cosmetics.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace features::changer
{
    namespace
    {
        constexpr int FORCE_UPDATE_FRAMES = 6;
        std::atomic_int g_updateFrames{FORCE_UPDATE_FRAMES};
        std::vector<engine::item_handle> g_lastWeaponHandles;
        std::unordered_map<std::uintptr_t, std::uint64_t> g_lastAppliedGunConfigHash;

        using weapon_cosmetics::AppliedKeychainState;

        std::unordered_map<std::uintptr_t, AppliedKeychainState> g_lastAppliedKeychainState;

        struct PendingKeychainOperation
        {
            engine::item_handle weaponHandle{};
            AppliedKeychainState desired{};
        };

        std::unordered_map<std::uintptr_t, PendingKeychainOperation> g_pendingKeychainOperations;

        // UI edits and FrameStageNotify run on different threads; use a short lease.
        std::atomic_bool g_interactiveEditActive{false};
        std::atomic<std::uint64_t> g_interactiveEditHeartbeatMs{0};

        constexpr std::uint64_t INTERACTIVE_EDIT_LEASE_MS = 500;

        // Keep the previous preview frame while live custom-material work settles.
        constexpr std::uint64_t PREVIEW_POST_UPDATE_SETTLE_MS = 500;
        std::atomic<std::uint64_t> g_previewBlockedUntilMs{0};

        std::uint64_t NowMilliseconds()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        void ExtendPreviewUpdateBlock()
        {
            g_previewBlockedUntilMs.store(NowMilliseconds() + PREVIEW_POST_UPDATE_SETTLE_MS, std::memory_order_release);
        }

        bool HasLivePreviewUpdateTarget()
        {
            if (!addresses::globals::source2engine_to_client ||
                !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) ||
                !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39))
            {
                return false;
            }

            engine::player_items *localPawn = engine::local_pawn();
            return localPawn && localPawn->m_iHealth() > 0;
        }

        bool g_restoreKnifeAfterDefault = false;
        std::uint16_t g_restoreKnifeDefinition = 0;

        void ResetKnifeDefaultRefresh()
        {
            g_restoreKnifeAfterDefault = false;
            g_restoreKnifeDefinition = 0;
        }

        std::uint16_t GetTemporaryKnifeDefinition(std::uint16_t currentDefinition)
        {
            return currentDefinition == 500 ? 507 : 500;
        }

        engine::weapon *GetWeaponFromHandle(const engine::item_handle &handle)
        {
            if (!handle.valid() || !addresses::globals::game_entity_system)
                return nullptr;

            return reinterpret_cast<engine::weapon *>(systems::g_entities.get_by_index(handle.index()));
        }

        engine::item_view *GetWeaponItem(engine::weapon *weapon)
        {
            if (!weapon)
                return nullptr;

            engine::attribute_container *attributeManager = weapon->m_AttributeManager();
            return attributeManager ? attributeManager->m_Item() : nullptr;
        }

        void ProcessKnife(engine::weapon *weapon, engine::item_view *item, engine::player_items *localPawn,
                          bool forceUpdate, bool &didUpdate)
        {
            if (!weapon || !item || !localPawn)
                return;

            if (g_restoreKnifeAfterDefault)
            {
                const std::uint16_t restoreDefinition = g_restoreKnifeDefinition;

                ResetKnifeDefaultRefresh();

                if (restoreDefinition >= 500 && knife_application::Apply(weapon, item, restoreDefinition))
                {
                    skin_application::ClearHudWeaponIconFor(weapon);
                    g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
                    didUpdate = true;
                }

                return;
            }

            if (cosmetic_config::skin_changer.knifeDefinition < 500)
                return;

            const std::uint16_t targetDefinition = cosmetic_config::skin_changer.knifeDefinition;

            if (knife_application::Apply(weapon, item, targetDefinition))
            {
                skin_application::ClearHudWeaponIconFor(weapon);
                g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
                didUpdate = true;
                return;
            }

            const auto configIt = cosmetic_config::skin_changer.knives.find(targetDefinition);
            const cosmetic_config::item_skin_t *config =
                configIt != cosmetic_config::skin_changer.knives.end() ? &configIt->second : nullptr;

            if (!config || !config->enabled || config->paintKit <= 0)
            {
                const bool hadCustomPaint = weapon->m_nFallbackPaintKit() > 0;

                if (hadCustomPaint)
                {
                    skin_application::ApplyDefault(weapon, item, localPawn, skin_application::ItemKind::Knife);

                    const std::uint16_t temporaryDefinition = GetTemporaryKnifeDefinition(targetDefinition);

                    if (knife_application::Apply(weapon, item, temporaryDefinition))
                    {
                        g_restoreKnifeAfterDefault = true;
                        g_restoreKnifeDefinition = targetDefinition;
                        g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
                    }

                    skin_application::ClearHudWeaponIconFor(weapon);
                    didUpdate = true;
                    return;
                }

                if (forceUpdate || !skin_application::IsDefaultState(weapon, item, skin_application::ItemKind::Knife))
                {
                    skin_application::ApplyDefault(weapon, item, localPawn, skin_application::ItemKind::Knife);
                    skin_application::ClearHudWeaponIconFor(weapon);
                    didUpdate = true;
                }

                return;
            }

            if (!skin_application::NeedsUpdate(weapon, item, *config, forceUpdate, skin_application::ItemKind::Knife))
                return;

            skin_application::Apply(weapon, item, *config, localPawn, skin_application::ItemKind::Knife);
            skin_application::ClearHudWeaponIconFor(weapon);

            didUpdate = true;
        }

        bool IsAppliedKeychainStateCurrent(std::uintptr_t weaponKey, const AppliedKeychainState &desired)
        {
            const auto it = g_lastAppliedKeychainState.find(weaponKey);

            if (it == g_lastAppliedKeychainState.end())
                return !desired.effective;

            return weapon_cosmetics::KeychainStatesEqual(it->second, desired);
        }

        void QueueKeychainState(const engine::item_handle &weaponHandle, engine::weapon *weapon,
                                std::uintptr_t weaponKey, const cosmetic_config::item_skin_t &config)
        {
            if (!weapon || !weaponHandle.valid())
                return;

            PendingKeychainOperation pending{};
            pending.weaponHandle = weaponHandle;
            pending.desired = weapon_cosmetics::GetDesiredKeychainState(config);

            // Rapid committed edits collapse to the newest desired state. Native
            // C_KeychainModule creation/destruction is deliberately not performed from
            // the pre-original FRAME_NET_UPDATE_END skin pass.
            g_pendingKeychainOperations[weaponKey] = pending;
        }

        void PruneAppliedKeychainState(const std::vector<engine::item_handle> &currentWeaponHandles)
        {
            const auto noLongerOwned = [&](const auto &entry)
            {
                return std::none_of(
                    currentWeaponHandles.begin(), currentWeaponHandles.end(), [&](const engine::item_handle &handle)
                    { return reinterpret_cast<std::uintptr_t>(GetWeaponFromHandle(handle)) == entry.first; });
            };
            std::erase_if(g_lastAppliedKeychainState, noLongerOwned);
            std::erase_if(g_pendingKeychainOperations, noLongerOwned);
        }

        void ResetWeaponRuntime()
        {
            g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
            g_lastWeaponHandles.clear();
            g_lastAppliedGunConfigHash.clear();
            g_lastAppliedKeychainState.clear();
            g_pendingKeychainOperations.clear();
            ResetKnifeDefaultRefresh();
        }

        bool HasAppliedKeychain(std::uintptr_t weaponKey)
        {
            const auto it = g_lastAppliedKeychainState.find(weaponKey);
            return it != g_lastAppliedKeychainState.end() && it->second.effective;
        }

        void QueueKeychainRemoval(std::uintptr_t weaponKey, const engine::item_handle &weaponHandle)
        {
            g_pendingKeychainOperations[weaponKey] = {weaponHandle, {}};
        }

        void ProcessGun(const engine::item_handle &weaponHandle, engine::weapon *weapon, engine::item_view *item,
                        engine::player_items *localPawn, std::uint16_t definitionIndex, bool forceUpdate,
                        bool &didUpdate)
        {
            if (!weapon || !item || !localPawn)
                return;

            const std::uintptr_t weaponKey = reinterpret_cast<std::uintptr_t>(weapon);
            const auto configIt = cosmetic_config::skin_changer.weapons.find(definitionIndex);

            if (configIt == cosmetic_config::skin_changer.weapons.end())
            {
                const bool hadKeychain = HasAppliedKeychain(weaponKey);

                if (hadKeychain)
                {
                    QueueKeychainRemoval(weaponKey, weaponHandle);

                    skin_application::ApplyDefault(weapon, item, localPawn, skin_application::ItemKind::Weapon);
                    skin_application::ClearHudWeaponIconFor(weapon);
                    didUpdate = true;
                }

                g_lastAppliedGunConfigHash.erase(weaponKey);
                return;
            }

            const cosmetic_config::item_skin_t &config = configIt->second;
            const AppliedKeychainState desiredKeychain = weapon_cosmetics::GetDesiredKeychainState(config);
            const bool keychainStateChanged = !IsAppliedKeychainStateCurrent(weaponKey, desiredKeychain);

            if (!config.enabled || !weapon_cosmetics::HasCustomConfiguration(config))
            {
                const bool hadKeychain = HasAppliedKeychain(weaponKey);

                if (hadKeychain)
                {
                    QueueKeychainRemoval(weaponKey, weaponHandle);
                }

                if (forceUpdate || hadKeychain ||
                    !skin_application::IsDefaultState(weapon, item, skin_application::ItemKind::Weapon))
                {
                    skin_application::ApplyDefault(weapon, item, localPawn, skin_application::ItemKind::Weapon);
                    skin_application::ClearHudWeaponIconFor(weapon);
                    didUpdate = true;
                }

                g_lastAppliedGunConfigHash.erase(weaponKey);
                return;
            }

            const std::uint64_t configHash = weapon_cosmetics::ComputeConfigHash(config);
            const auto hashIt = g_lastAppliedGunConfigHash.find(weaponKey);
            const bool cosmeticStateChanged =
                hashIt == g_lastAppliedGunConfigHash.end() || hashIt->second != configHash;

            if (!cosmeticStateChanged && !keychainStateChanged &&
                !skin_application::NeedsUpdate(weapon, item, config, forceUpdate, skin_application::ItemKind::Weapon))
            {
                return;
            }

            // Apply keychains after the normal attribute/material refresh has finished.
            if (desiredKeychain.effective || keychainStateChanged)
                QueueKeychainState(weaponHandle, weapon, weaponKey, config);

            skin_application::Apply(weapon, item, config, localPawn, skin_application::ItemKind::Weapon);
            skin_application::ClearHudWeaponIconFor(weapon);

            g_lastAppliedGunConfigHash[weaponKey] = configHash;
            didUpdate = true;
        }
    } // namespace

    void application::Run()
    {
        engine::player_items *localPawn = engine::local_pawn();

        // cosmetic_config values are intentionally allowed to change while the user drags a menu
        // control, but the actual held weapon is not regenerated until the edit commits.
        if (IsInteractiveEditActive())
            return;

        if (!cosmetic_config::skin_changer.enabled)
        {
            if (localPawn && localPawn->m_iHealth() > 0)
                glove_application::Run(localPawn);

            ResetWeaponRuntime();
            return;
        }

        if (!localPawn || !addresses::globals::game_entity_system || localPawn->m_iHealth() <= 0)
            return;

        glove_application::Run(localPawn);

        engine::weapon_services *weaponServices = localPawn->m_pWeaponServices();

        if (!weaponServices)
            return;

        auto &weaponHandles = weaponServices->m_hMyWeapons();

        std::vector<engine::item_handle> currentWeaponHandles;
        currentWeaponHandles.reserve(weaponHandles.count());

        for (int i = 0; i < weaponHandles.count(); ++i)
        {
            const engine::item_handle handle = weaponHandles.element(i);

            if (handle.valid())
                currentWeaponHandles.emplace_back(handle);
        }

        if (currentWeaponHandles != g_lastWeaponHandles)
        {
            g_lastWeaponHandles = currentWeaponHandles;
            g_lastAppliedGunConfigHash.clear();
            PruneAppliedKeychainState(currentWeaponHandles);
            g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
            ResetKnifeDefaultRefresh();
        }

        const bool forceUpdate = g_updateFrames.load(std::memory_order_acquire) > 0;

        // Each forced application frame extends the preview quiet period. This means the
        // standalone preview starts only after the last live-weapon refresh frame rather
        // than racing the same custom-material pipeline.
        if (forceUpdate)
            ExtendPreviewUpdateBlock();

        bool didUpdate = false;

        for (const engine::item_handle &handle : currentWeaponHandles)
        {
            engine::weapon *weapon = GetWeaponFromHandle(handle);

            if (!weapon)
                continue;

            engine::item_view *item = GetWeaponItem(weapon);

            if (!item)
                continue;

            const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();

            if (knife_application::IsKnifeDefinition(definitionIndex))
            {
                ProcessKnife(weapon, item, localPawn, forceUpdate, didUpdate);
                continue;
            }

            ProcessGun(handle, weapon, item, localPawn, definitionIndex, forceUpdate, didUpdate);
        }

        if (didUpdate)
        {
            skin_application::RegenerateSkins();
            ExtendPreviewUpdateBlock();
        }

        if (forceUpdate)
            g_updateFrames.fetch_sub(1, std::memory_order_acq_rel);
    }

    void application::ProcessPendingKeychains()
    {
        if (g_updateFrames.load(std::memory_order_acquire) > 0 || g_pendingKeychainOperations.empty())
            return;

        for (auto it = g_pendingKeychainOperations.begin(); it != g_pendingKeychainOperations.end();)
        {
            const std::uintptr_t weaponKey = it->first;
            const PendingKeychainOperation pending = it->second;
            engine::weapon *weapon = GetWeaponFromHandle(pending.weaponHandle);

            if (!weapon || reinterpret_cast<std::uintptr_t>(weapon) != weaponKey)
            {
                it = g_pendingKeychainOperations.erase(it);
                continue;
            }

            engine::item_view *item = GetWeaponItem(weapon);

            if (!item)
            {
                it = g_pendingKeychainOperations.erase(it);
                continue;
            }

            const bool hadKeychain = HasAppliedKeychain(weaponKey);

            bool completed = true;

            if (!pending.desired.effective)
            {
                if (hadKeychain)
                {
                    weapon->AddKeyChainEntity();
                    skin_application::RebuildHudKeychainAddon(weapon, engine::local_pawn());
                }
            }
            else
            {
                if (hadKeychain)
                    weapon->AddKeyChainEntity();

                completed = attributes::SetKeychainId(item, pending.desired.id);

                if (completed)
                {
                    weapon->AddKeyChainEntity();
                    skin_application::RebuildHudKeychainAddon(weapon, engine::local_pawn());
                }
            }

            if (completed)
                g_lastAppliedKeychainState[weaponKey] = pending.desired;

            it = g_pendingKeychainOperations.erase(it);
        }
    }

    void application::SetInteractiveEditActive(bool active)
    {
        if (active)
        {
            g_interactiveEditHeartbeatMs.store(NowMilliseconds(), std::memory_order_release);
            g_interactiveEditActive.store(true, std::memory_order_release);
            return;
        }

        g_interactiveEditActive.store(false, std::memory_order_release);
    }

    bool application::IsInteractiveEditActive()
    {
        if (!g_interactiveEditActive.load(std::memory_order_acquire))
            return false;

        const std::uint64_t heartbeat = g_interactiveEditHeartbeatMs.load(std::memory_order_acquire);

        const std::uint64_t now = NowMilliseconds();

        if (heartbeat == 0 || now - heartbeat > INTERACTIVE_EDIT_LEASE_MS)
        {
            g_interactiveEditActive.store(false, std::memory_order_release);
            return false;
        }

        return true;
    }

    bool application::IsUpdatePending()
    {
        // Main-menu previews do not have a live weapon update to wait for.
        if (!HasLivePreviewUpdateTarget())
            return false;

        if (g_updateFrames.load(std::memory_order_acquire) > 0)
            return true;

        return NowMilliseconds() < g_previewBlockedUntilMs.load(std::memory_order_acquire);
    }

    void application::ForceUpdate()
    {
        // Preserve the frame budget while an interactive edit is still active.
        g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
    }

    void application::Reset()
    {
        g_interactiveEditActive.store(false, std::memory_order_release);
        g_interactiveEditHeartbeatMs.store(0, std::memory_order_release);
        g_previewBlockedUntilMs.store(0, std::memory_order_release);

        ResetWeaponRuntime();
        ResetAgentRuntime();
        glove_application::Reset();
    }

    std::vector<std::uint16_t> application::GetOwnedWeaponDefinitionIndices()
    {
        std::vector<std::uint16_t> result;

        if (!engine::local_pawn() || !addresses::globals::game_entity_system)
            return result;

        engine::weapon_services *weaponServices = engine::local_pawn()->m_pWeaponServices();

        if (!weaponServices)
            return result;

        const auto appendWeapon = [&](const engine::item_handle &handle)
        {
            engine::weapon *weapon = GetWeaponFromHandle(handle);

            if (!weapon)
                return;

            engine::item_view *item = GetWeaponItem(weapon);

            if (!item)
                return;

            const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();

            if (knife_application::IsKnifeDefinition(definitionIndex))
                return;

            const econ_item_system::item_def *itemInfo = g_econ_item_system.find_def(definitionIndex);

            if (!itemInfo || itemInfo->category != econ_item_system::item_category::gun)
                return;

            if (std::find(result.begin(), result.end(), definitionIndex) == result.end())
                result.emplace_back(definitionIndex);
        };

        const engine::item_handle activeWeapon = weaponServices->m_hActiveWeapon();

        if (activeWeapon.valid())
            appendWeapon(activeWeapon);

        auto &weapons = weaponServices->m_hMyWeapons();

        for (int i = 0; i < weapons.count(); ++i)
        {
            const engine::item_handle handle = weapons.element(i);

            if (handle.valid() && handle != activeWeapon)
                appendWeapon(handle);
        }

        return result;
    }

} // namespace features::changer
