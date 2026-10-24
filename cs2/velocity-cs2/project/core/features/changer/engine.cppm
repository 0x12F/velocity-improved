module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

export module velocity:core.features.changer.engine;

import <atomic>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string_view>;
import <type_traits>;

export import :core.systems.systems;
export import :protection.game_addresses;
export import :utilities.cstypes;
export import :utilities.math.math;
export import :utilities.memory.memory;
export import :utilities.addresses.addresses;
export import :utilities.fnv1a;

export {
namespace features::changer::engine {
// Non-owning views of just the game fields used by cosmetic application.
template <typename T>
T &Field(void *object, std::uint32_t offset) {
  return *reinterpret_cast<T *>(reinterpret_cast<std::uintptr_t>(object) +
                                offset);
}

template <typename Resolver>
auto CachedAddress(Resolver resolver) {
  using Pointer = decltype(resolver());
  static constinit std::atomic<Pointer> cached{nullptr};
  auto value = cached.load(std::memory_order_acquire);
  if (!value) {
    value = resolver();
    if (value) cached.store(value, std::memory_order_release);
  }
  return value;
}

template <typename T>
T *RelativeAddress(T *address, int offset, int adjustment = 0) {
  if (!address) return nullptr;
  auto displacement = reinterpret_cast<std::uint8_t *>(address) + offset;
  return reinterpret_cast<T *>(displacement + sizeof(std::int32_t) +
                               *reinterpret_cast<std::int32_t *>(displacement) +
                               adjustment);
}

inline void *(__fastcall *allocate_attributes)(std::size_t){};
inline void(__fastcall *free_attributes)(void *){};

class Entity {};

inline constexpr std::uint32_t kInvalidEhandleIndex = 0xFFFFFFFFu;
inline constexpr std::uint32_t kEntryMask = 0x7FFFu;

class ItemHandle {
 public:
  ItemHandle() noexcept = default;

  bool operator==(const ItemHandle &other) const noexcept {
    return n_index_ == other.n_index_;
  }

  [[nodiscard]] bool Valid() const noexcept {
    return n_index_ != kInvalidEhandleIndex;
  }

  [[nodiscard]] int Index() const noexcept {
    return static_cast<int>(n_index_ & kEntryMask);
  }

 private:
  std::uint32_t n_index_ = kInvalidEhandleIndex;
};

struct WeaponHandles {
  int size;
  ItemHandle *data;
  int Count() const { return size; }
  const ItemHandle &Element(int index) const { return data[index]; }
};

class SceneNode;

class CollisionBounds {
 public:
  math::Vector3 &MVecMins() {
    return Field<math::Vector3>(this,
                                SCHEMA("CCollisionProperty", "m_vecMins"_hash));
  }
  math::Vector3 &MVecMaxs() {
    return Field<math::Vector3>(this,
                                SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));
  }
};

class BaseEntity : public Entity {
 public:
  std::int32_t &MIHealth() {
    return Field<std::int32_t>(this, SCHEMA("C_BaseEntity", "m_iHealth"_hash));
  }
  std::uint8_t &MITeamNum() {
    return Field<std::uint8_t>(this, SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  }
  SceneNode *&MPGameSceneNode() {
    return Field<SceneNode *>(this,
                              SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  }
  CollisionBounds *&MPCollision() {
    return Field<CollisionBounds *>(
        this, SCHEMA("C_BaseEntity", "m_pCollision"_hash));
  }
  ItemHandle &MHOwnerEntity() {
    return Field<ItemHandle>(this,
                             SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash));
  }
  int &MNSubclassId() {
    return Field<int>(this, SCHEMA("C_BaseEntity", "m_nSubclassID"_hash));
  }
};

class AttributeList {
 public:
  std::uintptr_t &MAttributes() {
    return Field<std::uintptr_t>(this,
                                 SCHEMA("CAttributeList", "m_Attributes"_hash));
  }
};

class ItemView {
 public:
  std::uint16_t &MIItemDefinitionIndex() {
    return Field<std::uint16_t>(
        this, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
  }
  std::int32_t &MIEntityQuality() {
    return Field<std::int32_t>(
        this, SCHEMA("C_EconItemView", "m_iEntityQuality"_hash));
  }
  std::uint32_t &MIItemIdHigh() {
    return Field<std::uint32_t>(this,
                                SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash));
  }
  bool &MBInitialized() {
    return Field<bool>(this, SCHEMA("C_EconItemView", "m_bInitialized"_hash));
  }
  bool &MBDisallowSoc() {
    return Field<bool>(this, SCHEMA("C_EconItemView", "m_bDisallowSOC"_hash));
  }
  bool &MBRestoreCustomMaterialAfterPrecache() {
    return Field<bool>(
        this,
        SCHEMA("C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash));
  }
  AttributeList *MAttributeList() {
    return reinterpret_cast<AttributeList *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("C_EconItemView", "m_AttributeList"_hash));
  }
  char *MSzCustomName() {
    return reinterpret_cast<char *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("C_EconItemView", "m_szCustomName"_hash));
  }
  [[nodiscard]] std::uintptr_t &MNameDescriptionPtr() {
    return m_name_description;
  }

  // This cached description pointer is not reflected in the entity schema.
  std::byte m_unknown000[0x200];
  std::uintptr_t m_name_description;
};

class AttributeContainer {
 public:
  ItemView *MItem() {
    return reinterpret_cast<ItemView *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("C_AttributeContainer", "m_Item"_hash));
  }
};

class EconEntity : public BaseEntity {
 public:
  AttributeContainer *MAttributeManager() {
    return reinterpret_cast<AttributeContainer *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("C_EconEntity", "m_AttributeManager"_hash));
  }
  int &MNFallbackPaintKit() {
    return Field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash));
  }
  int &MNFallbackSeed() {
    return Field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash));
  }
  float &MFlFallbackWear() {
    return Field<float>(this, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash));
  }
  int &MNFallbackStatTrak() {
    return Field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash));
  }

  // Slot 10 is PostDataUpdate; the native wrapper forwards a 32-bit EDX value.
  void PostDataUpdate(int update_type) {
    memory::CallVfunc<void>(reinterpret_cast<std::uintptr_t>(this), 10,
                            update_type);
  }
};

class WeaponServices {
 public:
  WeaponHandles &MHMyWeapons() {
    return Field<WeaponHandles>(
        this, SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_hash));
  }
  ItemHandle &MHActiveWeapon() {
    return Field<ItemHandle>(
        this, SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
  }
};

class Weapon : public EconEntity {
 public:
  bool AddKeyChainEntity() {
    using Fn = bool(__fastcall *)(Weapon *, ItemView *, bool);
    const auto fn = CachedAddress([] {
      return reinterpret_cast<Fn>(reinterpret_cast<std::uint8_t *>(
          PATTERN(patterns::add_key_chain_entity)));
    });
    if (!fn) return false;

    AttributeContainer *attribute_manager = MAttributeManager();
    ItemView *item = attribute_manager ? attribute_manager->MItem() : nullptr;
    return item ? fn(this, item, false) : false;
  }
};

class ItemView;

enum class Team : std::uint8_t { kUnassigned = 0, kT = 2, kCt = 3 };

class ModelData {
 public:
  const char *name = nullptr;
};

class Model {
 public:
  void *vtable = nullptr;
  ModelData perm_model_data{};
};

class ModelState {
 public:
  Model *MModel() {
    const auto handle = Field<cstypes::StrongHandle>(
        this, SCHEMA("CModelState", "m_hModel"_hash));
    return handle.binding ? *static_cast<Model *const *>(handle.binding)
                          : nullptr;
  }
};

class Skeleton;

// Private invalidation byte; the schema exposes the rotation values separately.
struct SceneNodeTransformLayout {
  std::byte unknown000[0x110];
  std::uint8_t dirty;
};

class SceneNode {
 public:
  Entity *&GetOwner() {
    return Field<Entity *>(this, SCHEMA("CGameSceneNode", "m_pOwner"_hash));
  }
  SceneNode *&GetChild() {
    return Field<SceneNode *>(this, SCHEMA("CGameSceneNode", "m_pChild"_hash));
  }
  SceneNode *&NextSibling() {
    return Field<SceneNode *>(this,
                              SCHEMA("CGameSceneNode", "m_pNextSibling"_hash));
  }
  math::Vector3 &MAngWrappedLocalRotation() {
    return Field<math::Vector3>(
        this, SCHEMA("CGameSceneNode", "m_angWrappedLocalRotation"_hash));
  }
  math::Vector3 &MAngAbsRotation() {
    return Field<math::Vector3>(
        this, SCHEMA("CGameSceneNode", "m_angAbsRotation"_hash));
  }

  [[nodiscard]] std::uint8_t &TransformDirty() {
    return reinterpret_cast<SceneNodeTransformLayout *>(this)->dirty;
  }

  Skeleton *GetSkeletonInstance() {
    return memory::CallVfunc<Skeleton *>(reinterpret_cast<std::uintptr_t>(this),
                                         13);
  }

  void SetMeshGroupMask(std::uint64_t mask) {
    using Fn = void(__fastcall *)(SceneNode *, std::uint64_t);
    const auto fn = CachedAddress([] {
      return reinterpret_cast<Fn>(reinterpret_cast<std::uint8_t *>(
          PATTERN(patterns::weapon_set_mesh_group_mask)));
    });
    if (fn) fn(this, mask);
  }
};

class Skeleton : public SceneNode {
 public:
  ModelState *MModelState() {
    return reinterpret_cast<ModelState *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("CSkeletonInstance", "m_modelState"_hash));
  }
};

class PlayerItems : public BaseEntity {
 public:
  WeaponServices *&MPWeaponServices() {
    return Field<WeaponServices *>(
        this, SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
  }
  ItemHandle &MHHudModelArms() {
    return Field<ItemHandle>(this,
                             SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash));
  }
  ItemView *MEconGloves() {
    return reinterpret_cast<ItemView *>(
        reinterpret_cast<std::uintptr_t>(this) +
        SCHEMA("C_CSPlayerPawn", "m_EconGloves"_hash));
  }
  bool &MBNeedToReApplyGloves() {
    return Field<bool>(this,
                       SCHEMA("C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash));
  }
  float &MFlLastSpawnTimeIndex() {
    return Field<float>(
        this, SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
  }

  Team GetTeam() { return static_cast<Team>(MITeamNum()); }
};

// Partial layouts for private HUD data not exposed by the entity schema.
struct HudAddonEntry {
  std::byte unknown[0x0C];
  ItemHandle entity_handle;
};

struct HudAddonDictionary {
  std::uint32_t count;
  std::uint32_t allocation_flags;
  HudAddonEntry *entries;

  [[nodiscard]] std::uint32_t Capacity() const {
    return allocation_flags & 0x7FFFFFFFu;
  }
};

struct HudWeaponAddonLayout {
  std::byte unknown0000[0x1328];
  HudAddonDictionary addons;
  std::byte unknown1338[0x20];
  std::uint8_t keychain_state;
};

struct HudWeaponSelectionEntry {
  std::byte unknown00[0x38];
  std::int32_t weapon_handle;
  std::byte unknown3_c[0x0C];
};

struct HudWeaponSelectionLayout {
  std::byte unknown00[0x50];
  std::int32_t count;
  std::byte unknown54[0x04];
  HudWeaponSelectionEntry *entries;
  std::byte unknown60[0x38];
  void *hud_element_vtable;

  [[nodiscard]] static HudWeaponSelectionLayout *FromHudElement(
      std::uintptr_t element) {
    return reinterpret_cast<HudWeaponSelectionLayout *>(
        element - offsetof(HudWeaponSelectionLayout, hud_element_vtable));
  }
};

struct GeneratedPaintKit {
  int id;
  std::uint32_t padding;
  const char *m_name;
};
class CompositeMaterialOwner;
inline CompositeMaterialOwner *CompositeOwner(void *entity) {
  return static_cast<CompositeMaterialOwner *>(
      memory::FindNonvirtualBase(entity, ".?AVCCompositeMaterialOwner@@"));
}
inline bool Connected() {
  return addresses::globals::source2engine_to_client &&
         memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                                 40) &&
         memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                                 39);
}
inline PlayerItems *LocalPawn() {
  return Connected()
             ? reinterpret_cast<PlayerItems *>(systems::g_local.Get().pawn)
             : nullptr;
}
}  // namespace features::changer::engine
}  // export
