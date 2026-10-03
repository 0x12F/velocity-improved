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
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr int FORCE_UPDATE_FRAMES = 6;
using AgentSetModelFn = void(__fastcall*)(void*, const char*);
    using AgentPrecacheFn = void* (__fastcall*)(void*, void*, const char*);
    using BufferStringInsertFn = const char* (__fastcall*)(void*, int, const char*, int, bool);
    using BufferStringPurgeFn = void(__fastcall*)(void*, int);

    struct CBufferString
    {
        int m_nLength = 0;
        int m_nAllocatedSize = static_cast<int>(0xC0000008u);

        union
        {
            char* m_pString;
            char m_szString[8];
        };

        CBufferString()
        {
            m_pString = nullptr;
        }
    };

    std::uint16_t g_activeAgentDefinition = 0;
    std::uint16_t g_failedAgentDefinition = 0;
    std::uintptr_t g_agentPawnAddress = 0;
    features::changer::engine::team g_agentTeam = features::changer::engine::team::UNASSIGNED;
    float g_agentSpawnTime = -1.0f;
    std::string g_originalAgentModel;
    bool g_agentCustomApplied = false;
    std::atomic_bool g_forceAgentUpdate{ true };

    std::uint16_t g_lastAgentTargetDefinition = 0;
    int g_agentApplyAttempts = 0;

    bool IsErrorPlayerModelName(const std::string& modelName)
    {
        return modelName == "models/dev/error.vmdl" || modelName.ends_with("/error.vmdl");
    }

    AgentSetModelFn GetAgentSetModel()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<AgentSetModelFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::agent_set_model))); });
        return fn;
    }

    AgentPrecacheFn GetAgentPrecache()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<AgentPrecacheFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::resource_system_precache))); });
        return fn;
    }

    void* GetAgentResourceSystem()
    {
        void* resourceSystem = features::changer::engine::cached_address([] { return reinterpret_cast<void*>(addresses::globals::resource_system); });
        return resourceSystem;
    }

    BufferStringInsertFn GetBufferStringInsert()
    {
        const auto fn = features::changer::engine::cached_address([]
        {
            const HMODULE tier0 = GetModuleHandleA("tier0.dll");
            return tier0 ? reinterpret_cast<BufferStringInsertFn>(GetProcAddress(tier0, "?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z")) : nullptr;
        });
        return fn;
    }

    BufferStringPurgeFn GetBufferStringPurge()
    {
        const auto fn = features::changer::engine::cached_address([]
        {
            const HMODULE tier0 = GetModuleHandleA("tier0.dll");
            return tier0 ? reinterpret_cast<BufferStringPurgeFn>(GetProcAddress(tier0, "?Purge@CBufferString@@QEAAXH@Z")) : nullptr;
        });
        return fn;
    }

    bool PrecacheAgentModel(const std::string& modelName)
    {
        if (modelName.empty())
            return false;

        void* resourceSystem = GetAgentResourceSystem();
        const auto precache = GetAgentPrecache();
        const auto insert = GetBufferStringInsert();

        if (!resourceSystem || !precache || !insert)
            return false;

        CBufferString names;
        insert(&names, 0, modelName.c_str(), -1, false);
        precache(resourceSystem, &names, "");

        if (const auto purge = GetBufferStringPurge())
            purge(&names, 0);

        return true;
    }

    const char* GetCurrentPlayerModelName(features::changer::engine::player_items* pawn)
    {
        if (!pawn)
            return nullptr;

        features::changer::engine::scene_node* sceneNode = pawn->m_pGameSceneNode();

        if (!sceneNode)
            return nullptr;

        features::changer::engine::skeleton* skeleton = sceneNode->GetSkeletonInstance();

        if (!skeleton)
            return nullptr;

        features::changer::engine::model_state* modelState = skeleton->m_model_state();

        if (!modelState)
            return nullptr;

        features::changer::engine::model* model = modelState->m_model();

        if (!model || !model->perm_model_data.name || !*model->perm_model_data.name)
            return nullptr;

        return model->perm_model_data.name;
    }

    bool SetAgentModel(features::changer::engine::player_items* pawn, const char* modelName)
    {
        if (!pawn || !modelName || !*modelName)
            return false;

        const auto setModel = GetAgentSetModel();

        if (!setModel)
            return false;

        features::changer::engine::collision_bounds* collision = pawn->m_pCollision();
        math::vector3 mins{};
        math::vector3 maxs{};
        const bool preserveBounds = collision != nullptr;

        if (preserveBounds)
        {
            mins = collision->m_vecMins();
            maxs = collision->m_vecMaxs();
        }

        PrecacheAgentModel(modelName);
        setModel(pawn, modelName);

        collision = pawn->m_pCollision();

        if (preserveBounds && collision)
        {
            collision->m_vecMins() = mins;
            collision->m_vecMaxs() = maxs;
        }

        return true;
    }

    void ResetAgentRuntime()
    {
        g_agentPawnAddress = 0;
        g_agentTeam = features::changer::engine::team::UNASSIGNED;
        g_agentSpawnTime = -1.0f;
        g_originalAgentModel.clear();
        g_agentCustomApplied = false;
        g_activeAgentDefinition = 0;
        g_failedAgentDefinition = 0;
        g_lastAgentTargetDefinition = 0;
        g_agentApplyAttempts = 0;
        g_forceAgentUpdate.store(true, std::memory_order_release);
    }

    std::atomic_int g_updateFrames{ FORCE_UPDATE_FRAMES };
    std::vector<features::changer::engine::item_handle> g_lastWeaponHandles;
    std::unordered_map<std::uintptr_t, std::uint64_t> g_lastAppliedGunConfigHash;

    struct AppliedKeychainState
    {
        bool effective = false;
        int id = 0;
        int seed = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
    };

    std::unordered_map<std::uintptr_t, AppliedKeychainState> g_lastAppliedKeychainState;

    struct PendingKeychainOperation
    {
        features::changer::engine::item_handle weaponHandle{};
        AppliedKeychainState desired{};
    };

    std::unordered_map<std::uintptr_t, PendingKeychainOperation> g_pendingKeychainOperations;

    // UI edits and FrameStageNotify run on different threads; use a short lease.
    std::atomic_bool g_interactiveEditActive{ false };
    std::atomic<std::uint64_t> g_interactiveEditHeartbeatMs{ 0 };

    constexpr std::uint64_t INTERACTIVE_EDIT_LEASE_MS = 500;

    // Keep the previous preview frame while live custom-material work settles.
    constexpr std::uint64_t PREVIEW_POST_UPDATE_SETTLE_MS = 500;
    std::atomic<std::uint64_t> g_previewBlockedUntilMs{ 0 };

    std::uint64_t NowMilliseconds()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    void ExtendPreviewUpdateBlock()
    {
        g_previewBlockedUntilMs.store(
            NowMilliseconds() + PREVIEW_POST_UPDATE_SETTLE_MS,
            std::memory_order_release);
    }

    bool HasLivePreviewUpdateTarget()
    {
        if (!addresses::globals::source2engine_to_client ||
            !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) ||
            !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39))
        {
            return false;
        }

        features::changer::engine::player_items* localPawn = features::changer::engine::local_pawn();
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

    features::changer::engine::weapon* GetWeaponFromHandle(const features::changer::engine::item_handle& handle)
    {
        if (!handle.valid() || !addresses::globals::game_entity_system)
            return nullptr;

        return reinterpret_cast<features::changer::engine::weapon*>(systems::g_entities.get_by_index(handle.index()));
    }

    features::changer::engine::item_view* GetWeaponItem(features::changer::engine::weapon* weapon)
    {
        if (!weapon)
            return nullptr;

        features::changer::engine::attribute_container* attributeManager = weapon->m_AttributeManager();
        return attributeManager ? attributeManager->m_Item() : nullptr;
    }

    void ProcessKnife(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, bool forceUpdate, bool& didUpdate)
    {
        if (!weapon || !item || !localPawn)
            return;

        if (g_restoreKnifeAfterDefault)
        {
            const std::uint16_t restoreDefinition = g_restoreKnifeDefinition;

            ResetKnifeDefaultRefresh();

            if (restoreDefinition >= 500 && features::changer::knife_application::Apply(weapon, item, restoreDefinition))
            {
                features::changer::skin_application::ClearHudWeaponIconFor(weapon);
                g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
                didUpdate = true;
            }

            return;
        }

        if (features::changer::cosmetic_config::skin_changer.knifeDefinition < 500)
            return;

        const std::uint16_t targetDefinition = features::changer::cosmetic_config::skin_changer.knifeDefinition;

        if (features::changer::knife_application::Apply(weapon, item, targetDefinition))
        {
            features::changer::skin_application::ClearHudWeaponIconFor(weapon);
            g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
            didUpdate = true;
            return;
        }

        const auto configIt = features::changer::cosmetic_config::skin_changer.knives.find(targetDefinition);
        const features::changer::cosmetic_config::item_skin_t* config = configIt != features::changer::cosmetic_config::skin_changer.knives.end() ? &configIt->second : nullptr;

        if (!config || !config->enabled || config->paintKit <= 0)
        {
            const bool hadCustomPaint = weapon->m_nFallbackPaintKit() > 0;

            if (hadCustomPaint)
            {
                features::changer::skin_application::ApplyDefault(weapon, item, localPawn, features::changer::skin_application::ItemKind::Knife);

                const std::uint16_t temporaryDefinition = GetTemporaryKnifeDefinition(targetDefinition);

                if (features::changer::knife_application::Apply(weapon, item, temporaryDefinition))
                {
                    g_restoreKnifeAfterDefault = true;
                    g_restoreKnifeDefinition = targetDefinition;
                    g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
                }

                features::changer::skin_application::ClearHudWeaponIconFor(weapon);
                didUpdate = true;
                return;
            }

            if (forceUpdate || !features::changer::skin_application::IsDefaultState(weapon, item, features::changer::skin_application::ItemKind::Knife))
            {
                features::changer::skin_application::ApplyDefault(weapon, item, localPawn, features::changer::skin_application::ItemKind::Knife);
                features::changer::skin_application::ClearHudWeaponIconFor(weapon);
                didUpdate = true;
            }

            return;
        }

        if (!features::changer::skin_application::NeedsUpdate(weapon, item, *config, forceUpdate, features::changer::skin_application::ItemKind::Knife))
            return;

        features::changer::skin_application::Apply(weapon, item, *config, localPawn, features::changer::skin_application::ItemKind::Knife);
        features::changer::skin_application::ClearHudWeaponIconFor(weapon);

        didUpdate = true;
    }

    bool HasCustomGunConfiguration(const features::changer::cosmetic_config::item_skin_t& config)
    {
        if (config.paintKit > 0)
            return true;

        if (config.statTrakEnabled)
            return true;

        if (!config.customName.empty())
            return true;

        return features::changer::weapon_cosmetics::HasAny(config);
    }

    void HashByte(std::uint64_t& hash, std::uint8_t value)
    {
        constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
        hash ^= value;
        hash *= FNV_PRIME;
    }

    template <typename T>
    void HashPod(std::uint64_t& hash, const T& value)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);

        for (std::size_t i = 0; i < sizeof(T); ++i)
            HashByte(hash, bytes[i]);
    }

    void HashFloat(std::uint64_t& hash, float value)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        HashPod(hash, bits);
    }

    AppliedKeychainState GetDesiredKeychainState(const features::changer::cosmetic_config::item_skin_t& config)
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

    bool KeychainStatesEqual(const AppliedKeychainState& lhs, const AppliedKeychainState& rhs)
    {
        if (lhs.effective != rhs.effective)
            return false;

        if (!lhs.effective)
            return true;

        return lhs.id == rhs.id &&
            lhs.seed == rhs.seed &&
            lhs.offsetX == rhs.offsetX &&
            lhs.offsetY == rhs.offsetY &&
            lhs.offsetZ == rhs.offsetZ;
    }

    bool IsAppliedKeychainStateCurrent(std::uintptr_t weaponKey, const AppliedKeychainState& desired)
    {
        const auto it = g_lastAppliedKeychainState.find(weaponKey);

        if (it == g_lastAppliedKeychainState.end())
            return !desired.effective;

        return KeychainStatesEqual(it->second, desired);
    }

    void QueueKeychainState(const features::changer::engine::item_handle& weaponHandle, features::changer::engine::weapon* weapon, std::uintptr_t weaponKey, const features::changer::cosmetic_config::item_skin_t& config)
    {
        if (!weapon || !weaponHandle.valid())
            return;

        PendingKeychainOperation pending{};
        pending.weaponHandle = weaponHandle;
        pending.desired = GetDesiredKeychainState(config);

        // Rapid committed edits collapse to the newest desired state. Native
        // C_KeychainModule creation/destruction is deliberately not performed from
        // the pre-original FRAME_NET_UPDATE_END skin pass.
        g_pendingKeychainOperations[weaponKey] = pending;
    }


    std::uint64_t ComputeGunConfigHash(const features::changer::cosmetic_config::item_skin_t& config)
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

        for (const auto& sticker : config.stickers)
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

    void PruneAppliedKeychainState(const std::vector<features::changer::engine::item_handle>& currentWeaponHandles)
    {
        for (auto it = g_lastAppliedKeychainState.begin(); it != g_lastAppliedKeychainState.end();)
        {
            bool stillPresent = false;

            for (const features::changer::engine::item_handle& handle : currentWeaponHandles)
            {
                features::changer::engine::weapon* weapon = GetWeaponFromHandle(handle);

                if (reinterpret_cast<std::uintptr_t>(weapon) == it->first)
                {
                    stillPresent = true;
                    break;
                }
            }

            if (stillPresent)
                ++it;
            else
                it = g_lastAppliedKeychainState.erase(it);
        }
        for (auto it = g_pendingKeychainOperations.begin(); it != g_pendingKeychainOperations.end();)
        {
            bool stillPresent = false;

            for (const features::changer::engine::item_handle& handle : currentWeaponHandles)
            {
                features::changer::engine::weapon* weapon = GetWeaponFromHandle(handle);

                if (reinterpret_cast<std::uintptr_t>(weapon) == it->first)
                {
                    stillPresent = true;
                    break;
                }
            }

            if (stillPresent)
                ++it;
            else
                it = g_pendingKeychainOperations.erase(it);
        }

    }

    void ProcessGun(const features::changer::engine::item_handle& weaponHandle, features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, std::uint16_t definitionIndex, bool forceUpdate, bool& didUpdate)
    {
        if (!weapon || !item || !localPawn)
            return;

        const std::uintptr_t weaponKey = reinterpret_cast<std::uintptr_t>(weapon);
        const auto configIt = features::changer::cosmetic_config::skin_changer.weapons.find(definitionIndex);

        if (configIt == features::changer::cosmetic_config::skin_changer.weapons.end())
        {
            const auto keychainIt = g_lastAppliedKeychainState.find(weaponKey);
            const bool hadKeychain =
                keychainIt != g_lastAppliedKeychainState.end() && keychainIt->second.effective;

            if (hadKeychain)
            {
                PendingKeychainOperation pending{};
                pending.weaponHandle = weaponHandle;
                pending.desired = AppliedKeychainState{};
                g_pendingKeychainOperations[weaponKey] = pending;

                features::changer::skin_application::ApplyDefault(weapon, item, localPawn, features::changer::skin_application::ItemKind::Weapon);
                features::changer::skin_application::ClearHudWeaponIconFor(weapon);
                didUpdate = true;
            }

            g_lastAppliedGunConfigHash.erase(weaponKey);
            return;
        }

        const features::changer::cosmetic_config::item_skin_t& config = configIt->second;
        const AppliedKeychainState desiredKeychain = GetDesiredKeychainState(config);
        const bool keychainStateChanged = !IsAppliedKeychainStateCurrent(weaponKey, desiredKeychain);

        if (!config.enabled || !HasCustomGunConfiguration(config))
        {
            const auto keychainIt = g_lastAppliedKeychainState.find(weaponKey);
            const bool hadKeychain =
                keychainIt != g_lastAppliedKeychainState.end() && keychainIt->second.effective;

            if (hadKeychain)
            {
                PendingKeychainOperation pending{};
                pending.weaponHandle = weaponHandle;
                pending.desired = AppliedKeychainState{};
                g_pendingKeychainOperations[weaponKey] = pending;
            }

            if (forceUpdate || hadKeychain || !features::changer::skin_application::IsDefaultState(weapon, item, features::changer::skin_application::ItemKind::Weapon))
            {
                features::changer::skin_application::ApplyDefault(weapon, item, localPawn, features::changer::skin_application::ItemKind::Weapon);
                features::changer::skin_application::ClearHudWeaponIconFor(weapon);
                didUpdate = true;
            }

            g_lastAppliedGunConfigHash.erase(weaponKey);
            return;
        }

        const std::uint64_t configHash = ComputeGunConfigHash(config);
        const auto hashIt = g_lastAppliedGunConfigHash.find(weaponKey);
        const bool cosmeticStateChanged =
            hashIt == g_lastAppliedGunConfigHash.end() ||
            hashIt->second != configHash;

        if (!cosmeticStateChanged &&
            !keychainStateChanged &&
            !features::changer::skin_application::NeedsUpdate(weapon, item, config, forceUpdate, features::changer::skin_application::ItemKind::Weapon))
        {
            return;
        }

        // Apply keychains after the normal attribute/material refresh has finished.
        if (desiredKeychain.effective || keychainStateChanged)
            QueueKeychainState(weaponHandle, weapon, weaponKey, config);

        features::changer::skin_application::Apply(weapon, item, config, localPawn, features::changer::skin_application::ItemKind::Weapon);
        features::changer::skin_application::ClearHudWeaponIconFor(weapon);

        g_lastAppliedGunConfigHash[weaponKey] = configHash;
        didUpdate = true;
    }
}

void features::changer::application::Run()
{
    features::changer::engine::player_items* localPawn = features::changer::engine::local_pawn();

    // features::changer::cosmetic_config values are intentionally allowed to change while the user drags a menu
    // control, but the actual held weapon is not regenerated until the edit commits.
    if (IsInteractiveEditActive())
        return;

    if (!features::changer::cosmetic_config::skin_changer.enabled)
    {
        if (localPawn && localPawn->m_iHealth() > 0)
            features::changer::glove_application::Run(localPawn);

        g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
        g_lastWeaponHandles.clear();
        g_lastAppliedGunConfigHash.clear();
        g_lastAppliedKeychainState.clear();
        g_pendingKeychainOperations.clear();
        ResetKnifeDefaultRefresh();
        return;
    }

    if (!localPawn || !addresses::globals::game_entity_system || localPawn->m_iHealth() <= 0)
        return;

    features::changer::glove_application::Run(localPawn);

    features::changer::engine::weapon_services* weaponServices = localPawn->m_pWeaponServices();

    if (!weaponServices)
        return;

    auto& weaponHandles = weaponServices->m_hMyWeapons();

    std::vector<features::changer::engine::item_handle> currentWeaponHandles;
    currentWeaponHandles.reserve(weaponHandles.count());

    for (int i = 0; i < weaponHandles.count(); ++i)
    {
        const features::changer::engine::item_handle handle = weaponHandles.element(i);

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

    for (const features::changer::engine::item_handle& handle : currentWeaponHandles)
    {
        features::changer::engine::weapon* weapon = GetWeaponFromHandle(handle);

        if (!weapon)
            continue;

        features::changer::engine::item_view* item = GetWeaponItem(weapon);

        if (!item)
            continue;

        const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();

        if (features::changer::knife_application::IsKnifeDefinition(definitionIndex))
        {
            ProcessKnife(weapon, item, localPawn, forceUpdate, didUpdate);
            continue;
        }

        ProcessGun(handle, weapon, item, localPawn, definitionIndex, forceUpdate, didUpdate);
    }

    if (didUpdate)
    {
        features::changer::skin_application::RegenerateSkins();
        ExtendPreviewUpdateBlock();
    }

    if (forceUpdate)
        g_updateFrames.fetch_sub(1, std::memory_order_acq_rel);
}

void features::changer::application::RunAgentChanger()
{
    if (!addresses::globals::source2engine_to_client || !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) || !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39))
    {
        ResetAgentRuntime();
        return;
    }

    features::changer::engine::player_items* localPawn = features::changer::engine::local_pawn();

    if (!localPawn || localPawn->m_iHealth() <= 0)
    {
        ResetAgentRuntime();
        return;
    }

    const std::uintptr_t pawnAddress = reinterpret_cast<std::uintptr_t>(localPawn);
    const features::changer::engine::team team = localPawn->getTeam();
    const float spawnTime = localPawn->m_flLastSpawnTimeIndex();

    if (pawnAddress != g_agentPawnAddress || team != g_agentTeam || spawnTime != g_agentSpawnTime)
    {
        g_agentPawnAddress = pawnAddress;
        g_agentTeam = team;
        g_agentSpawnTime = spawnTime;
        g_originalAgentModel.clear();
        g_agentCustomApplied = false;
        g_activeAgentDefinition = 0;
        g_failedAgentDefinition = 0;
        g_lastAgentTargetDefinition = 0;
        g_agentApplyAttempts = 0;
        g_forceAgentUpdate.store(true, std::memory_order_release);
        return;
    }

    std::uint16_t targetDefinition = 0;

    if (features::changer::cosmetic_config::skin_changer.enabled)
        targetDefinition = features::changer::cosmetic_config::skin_changer.agentDefinition;

    const features::changer::econ_item_system::item_def* targetItem = targetDefinition != 0 ? features::changer::g_econ_item_system.find_def(targetDefinition) : nullptr;

    if (targetItem && (targetItem->category != features::changer::econ_item_system::item_category::agent || targetItem->model_player.empty()))
        targetItem = nullptr;

    if (targetDefinition != g_lastAgentTargetDefinition)
    {
        g_lastAgentTargetDefinition = targetDefinition;
        g_agentApplyAttempts = 0;
    }

    if (g_failedAgentDefinition != 0 && targetDefinition != g_failedAgentDefinition)
        g_failedAgentDefinition = 0;

    if (targetItem && g_failedAgentDefinition == targetDefinition)
    {
        g_forceAgentUpdate.store(false, std::memory_order_release);
        return;
    }

    const char* currentModelPtr = GetCurrentPlayerModelName(localPawn);
    const std::string currentModel = currentModelPtr ? currentModelPtr : "";

    if (!targetItem)
    {
        if (g_agentCustomApplied && !g_originalAgentModel.empty() && (currentModel.empty() || g_originalAgentModel != currentModel))
            SetAgentModel(localPawn, g_originalAgentModel.c_str());

        g_agentCustomApplied = false;
        g_activeAgentDefinition = 0;
        g_agentApplyAttempts = 0;
        g_forceAgentUpdate.store(false, std::memory_order_release);
        return;
    }

    if (!g_agentCustomApplied && g_originalAgentModel.empty() && !currentModel.empty() && targetItem->model_player != currentModel)
        g_originalAgentModel = currentModel;

    const bool forceUpdate = g_forceAgentUpdate.exchange(false, std::memory_order_acq_rel);
    const bool needsApply = forceUpdate || currentModel.empty() || targetItem->model_player != currentModel;

    if (!needsApply)
    {
        g_agentCustomApplied = true;
        g_activeAgentDefinition = targetDefinition;
        g_agentApplyAttempts = 0;
        return;
    }

    const std::string beforeModel = currentModel;
    const bool setResult = SetAgentModel(localPawn, targetItem->model_player.c_str());
    const char* afterModelPtr = GetCurrentPlayerModelName(localPawn);
    const std::string afterModel = afterModelPtr ? afterModelPtr : "";

    ++g_agentApplyAttempts;

    if (IsErrorPlayerModelName(afterModel))
    {
        std::string rollbackModel;

        if (!beforeModel.empty() && !IsErrorPlayerModelName(beforeModel))
            rollbackModel = beforeModel;
        else if (!g_originalAgentModel.empty() && !IsErrorPlayerModelName(g_originalAgentModel))
            rollbackModel = g_originalAgentModel;

        if (!rollbackModel.empty())
            SetAgentModel(localPawn, rollbackModel.c_str());

        g_failedAgentDefinition = targetDefinition;
        g_agentCustomApplied = g_activeAgentDefinition != 0;
        g_forceAgentUpdate.store(false, std::memory_order_release);
        return;
    }

    if (setResult && afterModel == targetItem->model_player)
    {
        g_agentCustomApplied = true;
        g_activeAgentDefinition = targetDefinition;
        g_agentApplyAttempts = 0;
        return;
    }

    g_agentCustomApplied = false;
    g_forceAgentUpdate.store(g_agentApplyAttempts < 5, std::memory_order_release);
}

void features::changer::application::ForceAgentUpdate()
{
    g_forceAgentUpdate.store(true, std::memory_order_release);
}

bool features::changer::application::IsAgentDefinitionRejected(std::uint16_t definitionIndex)
{
    (void)definitionIndex;
    return false;
}

void features::changer::application::PrecacheAgentModels()
{
    if (features::changer::g_econ_item_system.item_defs().empty())
        return;

    for (const features::changer::econ_item_system::item_def& item : features::changer::g_econ_item_system.item_defs())
    {
        if (item.category == features::changer::econ_item_system::item_category::agent && !item.model_player.empty())
            PrecacheAgentModel(item.model_player);
    }
}

void features::changer::application::ProcessPendingKeychains()
{
    if (g_updateFrames.load(std::memory_order_acquire) > 0 || g_pendingKeychainOperations.empty())
        return;

    for (auto it = g_pendingKeychainOperations.begin(); it != g_pendingKeychainOperations.end();)
    {
        const std::uintptr_t weaponKey = it->first;
        const PendingKeychainOperation pending = it->second;
        features::changer::engine::weapon* weapon = GetWeaponFromHandle(pending.weaponHandle);

        if (!weapon || reinterpret_cast<std::uintptr_t>(weapon) != weaponKey)
        {
            it = g_pendingKeychainOperations.erase(it);
            continue;
        }

        features::changer::engine::item_view* item = GetWeaponItem(weapon);

        if (!item)
        {
            it = g_pendingKeychainOperations.erase(it);
            continue;
        }

        const auto currentIt = g_lastAppliedKeychainState.find(weaponKey);
        const bool hadKeychain =
            currentIt != g_lastAppliedKeychainState.end() && currentIt->second.effective;

        bool completed = true;

        if (!pending.desired.effective)
        {
            if (hadKeychain)
            {
                weapon->AddKeyChainEntity();
                features::changer::skin_application::RebuildHudKeychainAddon(weapon, features::changer::engine::local_pawn());
            }
        }
        else
        {
            if (hadKeychain)
                weapon->AddKeyChainEntity();

            completed = features::changer::attributes::SetKeychainId(item, pending.desired.id);

            if (completed)
            {
                weapon->AddKeyChainEntity();
                features::changer::skin_application::RebuildHudKeychainAddon(weapon, features::changer::engine::local_pawn());
            }
        }

        if (completed)
            g_lastAppliedKeychainState[weaponKey] = pending.desired;

        it = g_pendingKeychainOperations.erase(it);
    }
}

void features::changer::application::SetInteractiveEditActive(bool active)
{
    if (active)
    {
        g_interactiveEditHeartbeatMs.store(NowMilliseconds(), std::memory_order_release);
        g_interactiveEditActive.store(true, std::memory_order_release);
        return;
    }

    g_interactiveEditActive.store(false, std::memory_order_release);
}

bool features::changer::application::IsInteractiveEditActive()
{
    if (!g_interactiveEditActive.load(std::memory_order_acquire))
        return false;

    const std::uint64_t heartbeat =
        g_interactiveEditHeartbeatMs.load(std::memory_order_acquire);

    const std::uint64_t now = NowMilliseconds();

    if (heartbeat == 0 || now - heartbeat > INTERACTIVE_EDIT_LEASE_MS)
    {
        g_interactiveEditActive.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

bool features::changer::application::IsUpdatePending()
{
    // Main-menu previews do not have a live weapon update to wait for.
    if (!HasLivePreviewUpdateTarget())
        return false;

    if (g_updateFrames.load(std::memory_order_acquire) > 0)
        return true;

    return NowMilliseconds() <
        g_previewBlockedUntilMs.load(std::memory_order_acquire);
}

void features::changer::application::ForceUpdate()
{
    // Preserve the frame budget while an interactive edit is still active.
    g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
}

void features::changer::application::Reset()
{
    g_interactiveEditActive.store(false, std::memory_order_release);
    g_interactiveEditHeartbeatMs.store(0, std::memory_order_release);
    g_previewBlockedUntilMs.store(0, std::memory_order_release);

    g_updateFrames.store(FORCE_UPDATE_FRAMES, std::memory_order_release);
    g_lastWeaponHandles.clear();
    g_lastAppliedGunConfigHash.clear();
    g_lastAppliedKeychainState.clear();
    g_pendingKeychainOperations.clear();
    ResetKnifeDefaultRefresh();
    ResetAgentRuntime();
    g_failedAgentDefinition = 0;
    features::changer::glove_application::Reset();
}

std::vector<std::uint16_t> features::changer::application::GetOwnedWeaponDefinitionIndices()
{
    std::vector<std::uint16_t> result;

    if (!features::changer::engine::local_pawn() || !addresses::globals::game_entity_system)
        return result;

    features::changer::engine::weapon_services* weaponServices = features::changer::engine::local_pawn()->m_pWeaponServices();

    if (!weaponServices)
        return result;

    const auto appendWeapon = [&](const features::changer::engine::item_handle& handle)
    {
        features::changer::engine::weapon* weapon = GetWeaponFromHandle(handle);

        if (!weapon)
            return;

        features::changer::engine::item_view* item = GetWeaponItem(weapon);

        if (!item)
            return;

        const std::uint16_t definitionIndex = item->m_iItemDefinitionIndex();

        if (features::changer::knife_application::IsKnifeDefinition(definitionIndex))
            return;

        const features::changer::econ_item_system::item_def* itemInfo = features::changer::g_econ_item_system.find_def(definitionIndex);

        if (!itemInfo || itemInfo->category != econ_item_system::item_category::gun)
            return;

        if (std::find(result.begin(), result.end(), definitionIndex) == result.end())
            result.emplace_back(definitionIndex);
    };

    const features::changer::engine::item_handle activeWeapon = weaponServices->m_hActiveWeapon();

    if (activeWeapon.valid())
        appendWeapon(activeWeapon);

    auto& weapons = weaponServices->m_hMyWeapons();

    for (int i = 0; i < weapons.count(); ++i)
    {
        const features::changer::engine::item_handle handle = weapons.element(i);

        if (handle.valid() && handle != activeWeapon)
            appendWeapon(handle);
    }

    return result;
}
