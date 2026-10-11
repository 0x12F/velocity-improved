#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include "../engine.hpp"
#include "../agent_changer.hpp"

namespace features::changer
{
    namespace
    {
        using AgentSetModelFn = void(__fastcall *)(void *, const char *);
        using AgentPrecacheFn = void *(__fastcall *)(void *, void *, const char *);
        using BufferStringInsertFn = const char *(__fastcall *)(void *, int, const char *, int, bool);
        using BufferStringPurgeFn = void(__fastcall *)(void *, int);

        struct CBufferString
        {
            int m_nLength = 0;
            int m_nAllocatedSize = static_cast<int>(0xC0000008u);

            union
            {
                char *m_pString;
                char m_szString[8];
            };

            CBufferString()
            {
                m_pString = nullptr;
            }
        };

        struct AgentState
        {
            std::uint16_t activeDefinition = 0;
            std::uint16_t failedDefinition = 0;
            std::uintptr_t pawnAddress = 0;
            engine::team team = engine::team::UNASSIGNED;
            float spawnTime = -1.0f;
            std::string originalModel;
            bool customApplied = false;
            std::uint16_t targetDefinition = 0;
            int applyAttempts = 0;
        };

        AgentState g_agent;
        std::atomic_bool g_forceAgentUpdate{true};

        bool IsErrorPlayerModelName(const std::string &modelName)
        {
            return modelName == "models/dev/error.vmdl" || modelName.ends_with("/error.vmdl");
        }

        AgentSetModelFn GetAgentSetModel()
        {
            return engine::cached_address(
                [] { return reinterpret_cast<AgentSetModelFn>(PATTERN(patterns::agent_set_model)); });
        }

        AgentPrecacheFn GetAgentPrecache()
        {
            return engine::cached_address(
                [] { return reinterpret_cast<AgentPrecacheFn>(PATTERN(patterns::resource_system_precache)); });
        }

        void *GetAgentResourceSystem()
        {
            void *resourceSystem =
                engine::cached_address([] { return reinterpret_cast<void *>(addresses::globals::resource_system); });
            return resourceSystem;
        }

        BufferStringInsertFn GetBufferStringInsert()
        {
            return engine::cached_address(
                []
                {
                    const HMODULE tier0 = GetModuleHandleA("tier0.dll");
                    return tier0 ? reinterpret_cast<BufferStringInsertFn>(
                                       GetProcAddress(tier0, "?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z"))
                                 : nullptr;
                });
        }

        BufferStringPurgeFn GetBufferStringPurge()
        {
            return engine::cached_address(
                []
                {
                    const HMODULE tier0 = GetModuleHandleA("tier0.dll");
                    return tier0 ? reinterpret_cast<BufferStringPurgeFn>(
                                       GetProcAddress(tier0, "?Purge@CBufferString@@QEAAXH@Z"))
                                 : nullptr;
                });
        }

        bool PrecacheAgentModel(const std::string &modelName)
        {
            if (modelName.empty())
                return false;

            void *resourceSystem = GetAgentResourceSystem();
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

        const char *GetCurrentPlayerModelName(engine::player_items *pawn)
        {
            if (!pawn)
                return nullptr;

            engine::scene_node *sceneNode = pawn->m_pGameSceneNode();

            if (!sceneNode)
                return nullptr;

            engine::skeleton *skeleton = sceneNode->GetSkeletonInstance();

            if (!skeleton)
                return nullptr;

            engine::model_state *modelState = skeleton->m_model_state();

            if (!modelState)
                return nullptr;

            engine::model *model = modelState->m_model();

            if (!model || !model->perm_model_data.name || !*model->perm_model_data.name)
                return nullptr;

            return model->perm_model_data.name;
        }

        bool SetAgentModel(engine::player_items *pawn, const char *modelName)
        {
            if (!pawn || !modelName || !*modelName)
                return false;

            const auto setModel = GetAgentSetModel();

            if (!setModel)
                return false;

            engine::collision_bounds *collision = pawn->m_pCollision();
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

        void ResetAgentRuntimeState()
        {
            g_agent = {};
            g_forceAgentUpdate.store(true, std::memory_order_release);
        }

    } // namespace

    void application::RunAgentChanger()
    {
        if (!addresses::globals::source2engine_to_client ||
            !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) ||
            !memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39))
        {
            ResetAgentRuntimeState();
            return;
        }

        engine::player_items *localPawn = engine::local_pawn();

        if (!localPawn || localPawn->m_iHealth() <= 0)
        {
            ResetAgentRuntimeState();
            return;
        }

        const std::uintptr_t pawnAddress = reinterpret_cast<std::uintptr_t>(localPawn);
        const engine::team team = localPawn->getTeam();
        const float spawnTime = localPawn->m_flLastSpawnTimeIndex();

        if (pawnAddress != g_agent.pawnAddress || team != g_agent.team || spawnTime != g_agent.spawnTime)
        {
            ResetAgentRuntimeState();
            g_agent.pawnAddress = pawnAddress;
            g_agent.team = team;
            g_agent.spawnTime = spawnTime;
            return;
        }

        std::uint16_t targetDefinition = 0;

        if (cosmetic_config::skin_changer.enabled)
            targetDefinition = cosmetic_config::skin_changer.agentDefinition;

        const econ_item_system::item_def *targetItem =
            targetDefinition != 0 ? g_econ_item_system.find_def(targetDefinition) : nullptr;

        if (targetItem &&
            (targetItem->category != econ_item_system::item_category::agent || targetItem->model_player.empty()))
            targetItem = nullptr;

        if (targetDefinition != g_agent.targetDefinition)
        {
            g_agent.targetDefinition = targetDefinition;
            g_agent.applyAttempts = 0;
        }

        if (g_agent.failedDefinition != 0 && targetDefinition != g_agent.failedDefinition)
            g_agent.failedDefinition = 0;

        if (targetItem && g_agent.failedDefinition == targetDefinition)
        {
            g_forceAgentUpdate.store(false, std::memory_order_release);
            return;
        }

        const char *currentModelPtr = GetCurrentPlayerModelName(localPawn);
        const std::string currentModel = currentModelPtr ? currentModelPtr : "";

        if (!targetItem)
        {
            if (g_agent.customApplied && !g_agent.originalModel.empty() &&
                (currentModel.empty() || g_agent.originalModel != currentModel))
                SetAgentModel(localPawn, g_agent.originalModel.c_str());

            g_agent.customApplied = false;
            g_agent.activeDefinition = 0;
            g_agent.applyAttempts = 0;
            g_forceAgentUpdate.store(false, std::memory_order_release);
            return;
        }

        if (!g_agent.customApplied && g_agent.originalModel.empty() && !currentModel.empty() &&
            targetItem->model_player != currentModel)
            g_agent.originalModel = currentModel;

        const bool forceUpdate = g_forceAgentUpdate.exchange(false, std::memory_order_acq_rel);
        const bool needsApply = forceUpdate || currentModel.empty() || targetItem->model_player != currentModel;

        if (!needsApply)
        {
            g_agent.customApplied = true;
            g_agent.activeDefinition = targetDefinition;
            g_agent.applyAttempts = 0;
            return;
        }

        const std::string beforeModel = currentModel;
        const bool setResult = SetAgentModel(localPawn, targetItem->model_player.c_str());
        const char *afterModelPtr = GetCurrentPlayerModelName(localPawn);
        const std::string afterModel = afterModelPtr ? afterModelPtr : "";

        ++g_agent.applyAttempts;

        if (IsErrorPlayerModelName(afterModel))
        {
            std::string rollbackModel;

            if (!beforeModel.empty() && !IsErrorPlayerModelName(beforeModel))
                rollbackModel = beforeModel;
            else if (!g_agent.originalModel.empty() && !IsErrorPlayerModelName(g_agent.originalModel))
                rollbackModel = g_agent.originalModel;

            if (!rollbackModel.empty())
                SetAgentModel(localPawn, rollbackModel.c_str());

            g_agent.failedDefinition = targetDefinition;
            g_agent.customApplied = g_agent.activeDefinition != 0;
            g_forceAgentUpdate.store(false, std::memory_order_release);
            return;
        }

        if (setResult && afterModel == targetItem->model_player)
        {
            g_agent.customApplied = true;
            g_agent.activeDefinition = targetDefinition;
            g_agent.applyAttempts = 0;
            return;
        }

        g_agent.customApplied = false;
        g_forceAgentUpdate.store(g_agent.applyAttempts < 5, std::memory_order_release);
    }

    void application::ForceAgentUpdate()
    {
        g_forceAgentUpdate.store(true, std::memory_order_release);
    }

    bool application::IsAgentDefinitionRejected(std::uint16_t definitionIndex)
    {
        (void)definitionIndex;
        return false;
    }

    void application::PrecacheAgentModels()
    {
        if (g_econ_item_system.item_defs().empty())
            return;

        for (const econ_item_system::item_def &item : g_econ_item_system.item_defs())
        {
            if (item.category == econ_item_system::item_category::agent && !item.model_player.empty())
                PrecacheAgentModel(item.model_player);
        }
    }

    void application::ResetAgentRuntime()
    {
        ResetAgentRuntimeState();
    }

} // namespace features::changer
