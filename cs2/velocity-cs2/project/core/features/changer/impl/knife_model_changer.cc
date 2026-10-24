module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <cstdint>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::changer {
namespace {
constexpr int kItemQualityUnusual = 3;
using SetModelFn = void(__fastcall *)(void *, const char *);
using UpdateSubclassFn = void(__fastcall *)(void *);

SetModelFn GetSetModel() {
  return engine::CachedAddress([] {
    return reinterpret_cast<SetModelFn>(PATTERN(patterns::agent_set_model));
  });
}

UpdateSubclassFn GetUpdateSubclass() {
  return engine::CachedAddress([] {
    return reinterpret_cast<UpdateSubclassFn>(
        PATTERN(patterns::knife_update_subclass));
  });
}

bool SetModel(engine::Weapon *weapon, const char *model_name) {
  if (!weapon || !model_name || !*model_name) return false;

  const auto fn = GetSetModel();

  if (!fn) return false;

  fn(weapon, model_name);
  return true;
}

bool UpdateSubclass(engine::Weapon *weapon) {
  if (!weapon) return false;

  const auto fn = GetUpdateSubclass();

  if (!fn) return false;

  fn(weapon);
  return true;
}

std::uint32_t GetKnifeSubclassId(std::uint16_t definition_index) {
  switch (definition_index) {
    case 500:
      return 3933374535u;  // Bayonet
    case 503:
      return 3787235507u;  // Classic Knife
    case 505:
      return 4046390180u;  // Flip Knife
    case 506:
      return 2047704618u;  // Gut Knife
    case 507:
      return 1731408398u;  // Karambit
    case 508:
      return 1638561588u;  // M9 Bayonet
    case 509:
      return 2282479884u;  // Huntsman Knife
    case 512:
      return 3412259219u;  // Falchion Knife
    case 514:
      return 2511498851u;  // Bowie Knife
    case 515:
      return 1353709123u;  // Butterfly Knife
    case 516:
      return 4269888884u;  // Shadow Daggers
    case 517:
      return 1105782941u;  // Paracord Knife
    case 518:
      return 275962944u;  // Survival Knife
    case 519:
      return 1338637359u;  // Ursus Knife
    case 520:
      return 3230445913u;  // Navaja Knife
    case 521:
      return 3206681373u;  // Nomad Knife
    case 522:
      return 2595277776u;  // Stiletto Knife
    case 523:
      return 4029975521u;  // Talon Knife
    case 525:
      return 365028728u;  // Skeleton Knife
    case 526:
      return 3845286452u;  // Kukri Knife
    default:
      return 0;
  }
}
}  // namespace

bool knife_application::IsKnifeDefinition(std::uint16_t definition_index) {
  return definition_index == 41 || definition_index == 42 ||
         definition_index == 59 ||
         (definition_index >= 500 && definition_index <= 526);
}

bool knife_application::Apply(engine::Weapon *weapon, engine::ItemView *item,
                              std::uint16_t target_definition_index) {
  if (!weapon || !item) return false;

  if (!IsKnifeDefinition(item->MIItemDefinitionIndex()) ||
      target_definition_index < 500)
    return false;

  const EconItemSystem::ItemDef *target_item =
      g_econ_item_system.FindDef(target_definition_index);

  if (!target_item ||
      target_item->category != EconItemSystem::ItemCategory::kNife ||
      target_item->model_player.empty())
    return false;

  const std::uint32_t target_subclass_id =
      GetKnifeSubclassId(target_definition_index);

  if (target_subclass_id == 0) return false;

  if (item->MIItemDefinitionIndex() == target_definition_index &&
      static_cast<std::uint32_t>(weapon->MNSubclassId()) ==
          target_subclass_id) {
    return false;
  }

  item->MIEntityQuality() = kItemQualityUnusual;
  item->MIItemDefinitionIndex() = target_definition_index;
  weapon->MNSubclassId() = static_cast<int>(target_subclass_id);

  if (!UpdateSubclass(weapon)) return false;

  if (!SetModel(weapon, target_item->model_player.c_str())) return false;

  return true;
}

}  // namespace features::changer
