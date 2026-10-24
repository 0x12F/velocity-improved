module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

void Entities::OnAddEntity(std::uintptr_t entity, std::uint32_t handle) {
  if (!entity) {
    return;
  }

  const auto index = static_cast<std::int16_t>(handle & 0x7fff);
  if (index < 0 || index > 0x3fff) {
    return;
  }

  const auto schema_name = this->GetSchemaName(entity);
  if (!schema_name || !schema_name[0]) {
    return;
  }

  const auto hashed = fnv1a::RuntimeHash(schema_name);
  const auto entity_type = this->ClassifyEntity(hashed);

  if (entity_type == Type::kUnknown) {
    return;
  }

  Cached entry{};
  entry.ptr = entity;
  entry.index = index;
  entry.schema_hash = hashed;
  entry.type = entity_type;

  std::unique_lock lock(this->m_cache_mtx_);

  for (const auto& c : this->m_cached_) {
    if (c.index == index) {
      return;
    }
  }

  this->m_cached_.emplace_back(entry);
}

void Entities::OnRemoveEntity(std::uintptr_t entity, std::uint32_t handle) {
  if (!entity) {
    return;
  }

  const auto index = static_cast<std::int16_t>(handle & 0x7fff);
  if (index < 0 || index > 0x3fff) {
    return;
  }

  std::unique_lock lock(this->m_cache_mtx_);

  for (auto it = this->m_cached_.begin(); it != this->m_cached_.end(); ++it) {
    if (it->index == index) {
      if (it != this->m_cached_.end() - 1) {
        *it = this->m_cached_.back();
      }

      this->m_cached_.pop_back();
      return;
    }
  }
}

void Entities::ForceUpdate() {
  {
    std::unique_lock lock(this->m_cache_mtx_);
    this->m_cached_.clear();
    this->m_cached_.reserve(128);
  }

  for (auto i = 0; i < 2048; ++i) {
    const auto entity = this->GetByIndex(i);
    if (!entity) {
      continue;
    }

    this->OnAddEntity(entity, static_cast<std::uint32_t>(i));
  }
}

bool Entities::Exists(std::uintptr_t entity_ptr) const {
  std::shared_lock lock(this->m_cache_mtx_);

  for (const auto& c : this->m_cached_) {
    if (c.ptr == entity_ptr) {
      return true;
    }
  }

  return false;
}

const char* Entities::GetSchemaName(std::uintptr_t entity) const {
  const auto identity =
      memory::SafeRead<std::uintptr_t>(entity + 0x10).value_or(0);
  if (!identity) {
    return nullptr;
  }

  const auto entity_class =
      memory::SafeRead<std::uintptr_t>(identity + 0x8).value_or(0);
  if (!entity_class) {
    return nullptr;
  }

  // CEntityClass owns the current SchemaClassInfo pointer at 0x58.
  const auto class_info =
      memory::SafeRead<std::uintptr_t>(entity_class + 0x58).value_or(0);
  if (!class_info) {
    return nullptr;
  }

  return memory::SafeRead<const char*>(class_info + 0x8).value_or(nullptr);
}

std::uintptr_t Entities::GetByIndex(std::int32_t index) {
  if (index < 0 || index > kEntityIndexMask ||
      !addresses::globals::entity_list) {
    return 0;
  }

  const auto entity_list =
      memory::Read<std::uintptr_t>(addresses::globals::entity_list);
  if (!entity_list) {
    return 0;
  }

  if (this->m_cached_entity_list_ != entity_list) {
    this->m_cached_entity_list_ = entity_list;
    this->m_cached_list_entries_.fill(0);
  }

  const auto chunk_index = static_cast<std::size_t>(index) / kEntitiesPerChunk;

  if (!this->m_cached_list_entries_[chunk_index]) {
    this->m_cached_list_entries_[chunk_index] = memory::Read<std::uintptr_t>(
        entity_list + (static_cast<std::uintptr_t>(chunk_index) * 8) + 0x10);
  }

  const auto list_entry = this->m_cached_list_entries_[chunk_index];
  if (!list_entry) {
    return 0;
  }

  return memory::Read<std::uintptr_t>(
      list_entry +
      (static_cast<std::uintptr_t>(index) % kEntitiesPerChunk) * 112);
}

std::uintptr_t Entities::Lookup(std::uint32_t handle) const {
  if (!handle || handle == 0xffffffff) {
    return 0;
  }

  const auto entity_list =
      memory::Read<std::uintptr_t>(addresses::globals::entity_list);
  if (!entity_list) {
    return 0;
  }

  const auto list_entry = memory::Read<std::uintptr_t>(
      entity_list + (static_cast<std::uintptr_t>((handle & 0x7fff) >> 9) * 8) +
      0x10);
  if (!list_entry || list_entry == 0xffffffffffffffff) {
    return 0;
  }

  const auto entity = memory::Read<std::uintptr_t>(
      list_entry + (static_cast<std::uintptr_t>(handle & 0x1ff) * 112));
  if (!entity || entity == 0xffffffffffffffff || entity < 0x10000) {
    return 0;
  }

  return entity;
}

std::vector<Entities::Cached> Entities::GetByType(Type type) const {
  std::shared_lock lock(this->m_cache_mtx_);

  std::vector<Cached> result{};
  result.reserve(this->m_cached_.size());

  for (const auto& c : this->m_cached_) {
    if (c.type == type) {
      result.emplace_back(c);
    }
  }

  return result;
}

bool Entities::IsEmpty() const {
  std::shared_lock lock(this->m_cache_mtx_);
  return this->m_cached_.empty();
}

Entities::Type Entities::ClassifyEntity(std::uint32_t schema_hash) const {
  switch (schema_hash) {
    case "CCSPlayerController"_hash:
      return Type::kPlayer;

    case "C_AK47"_hash:
    case "C_WeaponM4A1"_hash:
    case "C_WeaponM4A1Silencer"_hash:
    case "C_WeaponAWP"_hash:
    case "C_WeaponAug"_hash:
    case "C_WeaponFamas"_hash:
    case "C_WeaponGalilAR"_hash:
    case "C_WeaponSG556"_hash:
    case "C_WeaponG3SG1"_hash:
    case "C_WeaponSCAR20"_hash:
    case "C_WeaponSSG08"_hash:
    case "C_WeaponMAC10"_hash:
    case "C_WeaponMP5SD"_hash:
    case "C_WeaponMP7"_hash:
    case "C_WeaponMP9"_hash:
    case "C_WeaponBizon"_hash:
    case "C_WeaponP90"_hash:
    case "C_WeaponUMP45"_hash:
    case "C_WeaponNOVA"_hash:
    case "C_WeaponSawedoff"_hash:
    case "C_WeaponXM1014"_hash:
    case "C_WeaponMag7"_hash:
    case "C_WeaponM249"_hash:
    case "C_WeaponNegev"_hash:
    case "C_DEagle"_hash:
    case "C_WeaponElite"_hash:
    case "C_WeaponFiveSeven"_hash:
    case "C_WeaponGlock"_hash:
    case "C_WeaponHKP2000"_hash:
    case "C_WeaponUSPSilencer"_hash:
    case "C_WeaponP250"_hash:
    case "C_WeaponCZ75a"_hash:
    case "C_WeaponTec9"_hash:
    case "C_WeaponRevolver"_hash:
    case "C_WeaponTaser"_hash:
    case "C_Knife"_hash:
    case "C_C4"_hash:
    case "C_Item_Healthshot"_hash:
    case "C_HEGrenade"_hash:
    case "C_Flashbang"_hash:
    case "C_SmokeGrenade"_hash:
    case "C_MolotovGrenade"_hash:
    case "C_IncendiaryGrenade"_hash:
    case "C_DecoyGrenade"_hash:
      return Type::kItem;

    case "C_HEGrenadeProjectile"_hash:
    case "C_FlashbangProjectile"_hash:
    case "C_SmokeGrenadeProjectile"_hash:
    case "C_MolotovProjectile"_hash:
    case "C_Inferno"_hash:
    case "C_DecoyProjectile"_hash:
      return Type::kProjectile;

    default:
      return Type::kUnknown;
  }
}

}  // namespace systems
