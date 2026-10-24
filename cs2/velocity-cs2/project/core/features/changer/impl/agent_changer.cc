module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::changer {
namespace {
using AgentSetModelFn = void(__fastcall *)(void *, const char *);
using AgentPrecacheFn = void *(__fastcall *)(void *, void *, const char *);
using BufferStringInsertFn = const char *(__fastcall *)(void *, int,
                                                        const char *, int,
                                                        bool);
using BufferStringPurgeFn = void(__fastcall *)(void *, int);

struct CBufferString {
  int m_n_length = 0;
  int m_n_allocated_size = static_cast<int>(0xC0000008u);

  union {
    char *m_p_string;
    char m_sz_string[8];
  };

  CBufferString() { m_p_string = nullptr; }
};

struct AgentState {
  std::uint16_t active_definition = 0;
  std::uint16_t failed_definition = 0;
  std::uintptr_t pawn_address = 0;
  engine::Team team = engine::Team::kUnassigned;
  float spawn_time = -1.0f;
  std::string original_model;
  bool custom_applied = false;
  std::uint16_t target_definition = 0;
  int apply_attempts = 0;
};

AgentState g_agent;
std::atomic_bool g_force_agent_update{true};

bool IsErrorPlayerModelName(const std::string &model_name) {
  return model_name == "models/dev/error.vmdl" ||
         model_name.ends_with("/error.vmdl");
}

AgentSetModelFn GetAgentSetModel() {
  return engine::CachedAddress([] {
    return reinterpret_cast<AgentSetModelFn>(
        PATTERN(patterns::agent_set_model));
  });
}

AgentPrecacheFn GetAgentPrecache() {
  return engine::CachedAddress([] {
    return reinterpret_cast<AgentPrecacheFn>(
        PATTERN(patterns::resource_system_precache));
  });
}

void *GetAgentResourceSystem() {
  void *resource_system = engine::CachedAddress([] {
    return reinterpret_cast<void *>(addresses::globals::resource_system);
  });
  return resource_system;
}

BufferStringInsertFn GetBufferStringInsert() {
  return engine::CachedAddress([] {
    const HMODULE tier0 = GetModuleHandleA("tier0.dll");
    return tier0 ? reinterpret_cast<BufferStringInsertFn>(GetProcAddress(
                       tier0, "?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z"))
                 : nullptr;
  });
}

BufferStringPurgeFn GetBufferStringPurge() {
  return engine::CachedAddress([] {
    const HMODULE tier0 = GetModuleHandleA("tier0.dll");
    return tier0 ? reinterpret_cast<BufferStringPurgeFn>(
                       GetProcAddress(tier0, "?Purge@CBufferString@@QEAAXH@Z"))
                 : nullptr;
  });
}

bool PrecacheAgentModel(const std::string &model_name) {
  if (model_name.empty()) return false;

  void *resource_system = GetAgentResourceSystem();
  const auto precache = GetAgentPrecache();
  const auto insert = GetBufferStringInsert();

  if (!resource_system || !precache || !insert) return false;

  CBufferString names;
  insert(&names, 0, model_name.c_str(), -1, false);
  precache(resource_system, &names, "");

  if (const auto purge = GetBufferStringPurge()) purge(&names, 0);

  return true;
}

const char *GetCurrentPlayerModelName(engine::PlayerItems *pawn) {
  if (!pawn) return nullptr;

  engine::SceneNode *scene_node = pawn->MPGameSceneNode();

  if (!scene_node) return nullptr;

  engine::Skeleton *skeleton = scene_node->GetSkeletonInstance();

  if (!skeleton) return nullptr;

  engine::ModelState *model_state = skeleton->MModelState();

  if (!model_state) return nullptr;

  engine::Model *model = model_state->MModel();

  if (!model || !model->perm_model_data.name || !*model->perm_model_data.name)
    return nullptr;

  return model->perm_model_data.name;
}

bool SetAgentModel(engine::PlayerItems *pawn, const char *model_name) {
  if (!pawn || !model_name || !*model_name) return false;

  const auto set_model = GetAgentSetModel();

  if (!set_model) return false;

  engine::CollisionBounds *collision = pawn->MPCollision();
  math::Vector3 mins{};
  math::Vector3 maxs{};
  const bool preserve_bounds = collision != nullptr;

  if (preserve_bounds) {
    mins = collision->MVecMins();
    maxs = collision->MVecMaxs();
  }

  PrecacheAgentModel(model_name);
  set_model(pawn, model_name);

  collision = pawn->MPCollision();

  if (preserve_bounds && collision) {
    collision->MVecMins() = mins;
    collision->MVecMaxs() = maxs;
  }

  return true;
}

void ResetAgentRuntimeState() {
  g_agent = {};
  g_force_agent_update.store(true, std::memory_order_release);
}

}  // namespace

void application::RunAgentChanger() {
  if (!addresses::globals::source2engine_to_client ||
      !memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                               40) ||
      !memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                               39)) {
    ResetAgentRuntimeState();
    return;
  }

  engine::PlayerItems *local_pawn = engine::LocalPawn();

  if (!local_pawn || local_pawn->MIHealth() <= 0) {
    ResetAgentRuntimeState();
    return;
  }

  const std::uintptr_t pawn_address =
      reinterpret_cast<std::uintptr_t>(local_pawn);
  const engine::Team team = local_pawn->GetTeam();
  const float spawn_time = local_pawn->MFlLastSpawnTimeIndex();

  if (pawn_address != g_agent.pawn_address || team != g_agent.team ||
      spawn_time != g_agent.spawn_time) {
    ResetAgentRuntimeState();
    g_agent.pawn_address = pawn_address;
    g_agent.team = team;
    g_agent.spawn_time = spawn_time;
    return;
  }

  std::uint16_t target_definition = 0;

  if (cosmetic_config::skin_changer.enabled)
    target_definition = cosmetic_config::skin_changer.agent_definition;

  const EconItemSystem::ItemDef *target_item =
      target_definition != 0 ? g_econ_item_system.FindDef(target_definition)
                             : nullptr;

  if (target_item &&
      (target_item->category != EconItemSystem::ItemCategory::kAgent ||
       target_item->model_player.empty()))
    target_item = nullptr;

  if (target_definition != g_agent.target_definition) {
    g_agent.target_definition = target_definition;
    g_agent.apply_attempts = 0;
  }

  if (g_agent.failed_definition != 0 &&
      target_definition != g_agent.failed_definition)
    g_agent.failed_definition = 0;

  if (target_item && g_agent.failed_definition == target_definition) {
    g_force_agent_update.store(false, std::memory_order_release);
    return;
  }

  const char *current_model_ptr = GetCurrentPlayerModelName(local_pawn);
  const std::string current_model = current_model_ptr ? current_model_ptr : "";

  if (!target_item) {
    if (g_agent.custom_applied && !g_agent.original_model.empty() &&
        (current_model.empty() || g_agent.original_model != current_model))
      SetAgentModel(local_pawn, g_agent.original_model.c_str());

    g_agent.custom_applied = false;
    g_agent.active_definition = 0;
    g_agent.apply_attempts = 0;
    g_force_agent_update.store(false, std::memory_order_release);
    return;
  }

  if (!g_agent.custom_applied && g_agent.original_model.empty() &&
      !current_model.empty() && target_item->model_player != current_model)
    g_agent.original_model = current_model;

  const bool force_update =
      g_force_agent_update.exchange(false, std::memory_order_acq_rel);
  const bool needs_apply = force_update || current_model.empty() ||
                           target_item->model_player != current_model;

  if (!needs_apply) {
    g_agent.custom_applied = true;
    g_agent.active_definition = target_definition;
    g_agent.apply_attempts = 0;
    return;
  }

  const std::string before_model = current_model;
  const bool set_result =
      SetAgentModel(local_pawn, target_item->model_player.c_str());
  const char *after_model_ptr = GetCurrentPlayerModelName(local_pawn);
  const std::string after_model = after_model_ptr ? after_model_ptr : "";

  ++g_agent.apply_attempts;

  if (IsErrorPlayerModelName(after_model)) {
    std::string rollback_model;

    if (!before_model.empty() && !IsErrorPlayerModelName(before_model))
      rollback_model = before_model;
    else if (!g_agent.original_model.empty() &&
             !IsErrorPlayerModelName(g_agent.original_model))
      rollback_model = g_agent.original_model;

    if (!rollback_model.empty())
      SetAgentModel(local_pawn, rollback_model.c_str());

    g_agent.failed_definition = target_definition;
    g_agent.custom_applied = g_agent.active_definition != 0;
    g_force_agent_update.store(false, std::memory_order_release);
    return;
  }

  if (set_result && after_model == target_item->model_player) {
    g_agent.custom_applied = true;
    g_agent.active_definition = target_definition;
    g_agent.apply_attempts = 0;
    return;
  }

  g_agent.custom_applied = false;
  g_force_agent_update.store(g_agent.apply_attempts < 5,
                             std::memory_order_release);
}

void application::ForceAgentUpdate() {
  g_force_agent_update.store(true, std::memory_order_release);
}

bool application::IsAgentDefinitionRejected(std::uint16_t definition_index) {
  (void)definition_index;
  return false;
}

void application::PrecacheAgentModels() {
  if (g_econ_item_system.ItemDefs().empty()) return;

  for (const EconItemSystem::ItemDef &item : g_econ_item_system.ItemDefs()) {
    if (item.category == EconItemSystem::ItemCategory::kAgent &&
        !item.model_player.empty())
      PrecacheAgentModel(item.model_player);
  }
}

void application::ResetAgentRuntime() { ResetAgentRuntimeState(); }

}  // namespace features::changer
