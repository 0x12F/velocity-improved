#pragma once
#include <core/systems/systems.hpp>
#include <protection/game_addresses.hpp>
#include <utilities/memory/memory.hpp>

namespace features::changer::engine
{
    // Non-owning views of just the game fields used by cosmetic application.
    template <typename T> T &field(void *object, std::uint32_t offset)
    {
        return *reinterpret_cast<T *>(reinterpret_cast<std::uintptr_t>(object) + offset);
    }

    template <typename Resolver> auto cached_address(Resolver resolver)
    {
        using pointer = decltype(resolver());
        static constinit std::atomic<pointer> cached{nullptr};
        auto value = cached.load(std::memory_order_acquire);
        if (!value)
        {
            value = resolver();
            if (value)
                cached.store(value, std::memory_order_release);
        }
        return value;
    }

    template <typename T> T *relative_address(T *address, int offset, int adjustment = 0)
    {
        if (!address)
            return nullptr;
        auto displacement = reinterpret_cast<std::uint8_t *>(address) + offset;
        return reinterpret_cast<T *>(displacement + sizeof(std::int32_t) + *reinterpret_cast<std::int32_t *>(displacement) + adjustment);
    }

    inline void *(__fastcall *allocate_attributes)(std::size_t){};
    inline void(__fastcall *free_attributes)(void *){};

    class entity
    {
    };

    inline constexpr std::uint32_t INVALID_EHANDLE_INDEX = 0xFFFFFFFFu;
    inline constexpr std::uint32_t entry_mask = 0x7FFFu;

    class item_handle
    {
      public:
        item_handle() noexcept = default;

        bool operator==(const item_handle &other) const noexcept
        {
            return nIndex == other.nIndex;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return nIndex != INVALID_EHANDLE_INDEX;
        }

        [[nodiscard]] int index() const noexcept
        {
            return static_cast<int>(nIndex & entry_mask);
        }

      private:
        std::uint32_t nIndex = INVALID_EHANDLE_INDEX;
    };

    struct weapon_handles
    {
        int size;
        item_handle *data;
        int count() const
        {
            return size;
        }
        const item_handle &element(int index) const
        {
            return data[index];
        }
    };

    class scene_node;

    class collision_bounds
    {
      public:
        math::vector3 &m_vecMins()
        {
            return field<math::vector3>(this, SCHEMA("CCollisionProperty", "m_vecMins"_hash));
        }
        math::vector3 &m_vecMaxs()
        {
            return field<math::vector3>(this, SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));
        }
    };

    class base_entity : public entity
    {
      public:
        std::int32_t &m_iHealth()
        {
            return field<std::int32_t>(this, SCHEMA("C_BaseEntity", "m_iHealth"_hash));
        }
        std::uint8_t &m_iTeamNum()
        {
            return field<std::uint8_t>(this, SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
        }
        scene_node *&m_pGameSceneNode()
        {
            return field<scene_node *>(this, SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
        }
        collision_bounds *&m_pCollision()
        {
            return field<collision_bounds *>(this, SCHEMA("C_BaseEntity", "m_pCollision"_hash));
        }
        item_handle &m_hOwnerEntity()
        {
            return field<item_handle>(this, SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash));
        }
        int &m_nSubclassID()
        {
            return field<int>(this, SCHEMA("C_BaseEntity", "m_nSubclassID"_hash));
        }
    };

    class attribute_list
    {
      public:
        std::uintptr_t &m_Attributes()
        {
            return field<std::uintptr_t>(this, SCHEMA("CAttributeList", "m_Attributes"_hash));
        }
    };

    class item_view
    {
      public:
        std::uint16_t &m_iItemDefinitionIndex()
        {
            return field<std::uint16_t>(this, SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
        }
        std::int32_t &m_iEntityQuality()
        {
            return field<std::int32_t>(this, SCHEMA("C_EconItemView", "m_iEntityQuality"_hash));
        }
        std::uint32_t &m_iItemIDHigh()
        {
            return field<std::uint32_t>(this, SCHEMA("C_EconItemView", "m_iItemIDHigh"_hash));
        }
        bool &m_bInitialized()
        {
            return field<bool>(this, SCHEMA("C_EconItemView", "m_bInitialized"_hash));
        }
        bool &m_bDisallowSOC()
        {
            return field<bool>(this, SCHEMA("C_EconItemView", "m_bDisallowSOC"_hash));
        }
        bool &m_bRestoreCustomMaterialAfterPrecache()
        {
            return field<bool>(this, SCHEMA("C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash));
        }
        attribute_list *m_AttributeList()
        {
            return reinterpret_cast<attribute_list *>(reinterpret_cast<std::uintptr_t>(this) +
                                                      SCHEMA("C_EconItemView", "m_AttributeList"_hash));
        }
        char *m_szCustomName()
        {
            return reinterpret_cast<char *>(reinterpret_cast<std::uintptr_t>(this) + SCHEMA("C_EconItemView", "m_szCustomName"_hash));
        }
        [[nodiscard]] std::uintptr_t &m_name_description_ptr()
        {
            return m_NameDescription;
        }

        // This cached description pointer is not reflected in the entity schema.
        std::byte m_Unknown000[0x200];
        std::uintptr_t m_NameDescription;
    };

    class attribute_container
    {
      public:
        item_view *m_Item()
        {
            return reinterpret_cast<item_view *>(reinterpret_cast<std::uintptr_t>(this) + SCHEMA("C_AttributeContainer", "m_Item"_hash));
        }
    };

    class econ_entity : public base_entity
    {
      public:
        attribute_container *m_AttributeManager()
        {
            return reinterpret_cast<attribute_container *>(reinterpret_cast<std::uintptr_t>(this) +
                                                           SCHEMA("C_EconEntity", "m_AttributeManager"_hash));
        }
        int &m_nFallbackPaintKit()
        {
            return field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackPaintKit"_hash));
        }
        int &m_nFallbackSeed()
        {
            return field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackSeed"_hash));
        }
        float &m_flFallbackWear()
        {
            return field<float>(this, SCHEMA("C_EconEntity", "m_flFallbackWear"_hash));
        }
        int &m_nFallbackStatTrak()
        {
            return field<int>(this, SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash));
        }

        // Slot 10 is PostDataUpdate; the native wrapper forwards a 32-bit EDX value.
        void PostDataUpdate(int updateType)
        {
            memory::call_vfunc<void>(reinterpret_cast<std::uintptr_t>(this), 10, updateType);
        }
    };

    class weapon_services
    {
      public:
        weapon_handles &m_hMyWeapons()
        {
            return field<weapon_handles>(this, SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_hash));
        }
        item_handle &m_hActiveWeapon()
        {
            return field<item_handle>(this, SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
        }
    };

    class weapon : public econ_entity
    {
      public:
        bool AddKeyChainEntity()
        {
            using Fn = bool(__fastcall *)(weapon *, item_view *, bool);
            const auto fn = cached_address([] {
                return reinterpret_cast<Fn>(
                    reinterpret_cast<std::uint8_t *>(PATTERN(patterns::add_key_chain_entity)));
            });
            if (!fn)
                return false;

            attribute_container *attributeManager = m_AttributeManager();
            item_view *item = attributeManager ? attributeManager->m_Item() : nullptr;
            return item ? fn(this, item, false) : false;
        }
    };

    class item_view;

    enum class team : std::uint8_t
    {
        UNASSIGNED = 0,
        T = 2,
        CT = 3
    };

    class model_data
    {
      public:
        const char *name = nullptr;
    };

    class model
    {
      public:
        void *vtable = nullptr;
        model_data perm_model_data{};
    };

    class model_state
    {
      public:
        model *m_model()
        {
            const auto handle = field<cstypes::strong_handle>(this, SCHEMA("CModelState", "m_hModel"_hash));
            return handle.binding ? *static_cast<model *const *>(handle.binding) : nullptr;
        }
    };

    class skeleton;

    // Private invalidation byte; the schema exposes the rotation values separately.
    struct SceneNodeTransformLayout
    {
        std::byte unknown000[0x110];
        std::uint8_t dirty;
    };

    class scene_node
    {
      public:
        entity *&GetOwner()
        {
            return field<entity *>(this, SCHEMA("CGameSceneNode", "m_pOwner"_hash));
        }
        scene_node *&GetChild()
        {
            return field<scene_node *>(this, SCHEMA("CGameSceneNode", "m_pChild"_hash));
        }
        scene_node *&next_sibling()
        {
            return field<scene_node *>(this, SCHEMA("CGameSceneNode", "m_pNextSibling"_hash));
        }
        math::vector3 &m_angWrappedLocalRotation()
        {
            return field<math::vector3>(this, SCHEMA("CGameSceneNode", "m_angWrappedLocalRotation"_hash));
        }
        math::vector3 &m_angAbsRotation()
        {
            return field<math::vector3>(this, SCHEMA("CGameSceneNode", "m_angAbsRotation"_hash));
        }

        [[nodiscard]] std::uint8_t &TransformDirty()
        {
            return reinterpret_cast<SceneNodeTransformLayout *>(this)->dirty;
        }

        skeleton *GetSkeletonInstance()
        {
            return memory::call_vfunc<skeleton *>(reinterpret_cast<std::uintptr_t>(this), 13);
        }

        void SetMeshGroupMask(std::uint64_t mask)
        {
            using Fn = void(__fastcall *)(scene_node *, std::uint64_t);
            const auto fn = cached_address([] {
                return reinterpret_cast<Fn>(reinterpret_cast<std::uint8_t *>(PATTERN(patterns::weapon_set_mesh_group_mask)));
            });
            if (fn)
                fn(this, mask);
        }
    };

    class skeleton : public scene_node
    {
      public:
        model_state *m_model_state()
        {
            return reinterpret_cast<model_state *>(reinterpret_cast<std::uintptr_t>(this) +
                                                   SCHEMA("CSkeletonInstance", "m_modelState"_hash));
        }
    };

    class player_items : public base_entity
    {
      public:
        weapon_services *&m_pWeaponServices()
        {
            return field<weapon_services *>(this, SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
        }
        item_handle &m_hHudModelArms()
        {
            return field<item_handle>(this, SCHEMA("C_CSPlayerPawn", "m_hHudModelArms"_hash));
        }
        item_view *m_EconGloves()
        {
            return reinterpret_cast<item_view *>(reinterpret_cast<std::uintptr_t>(this) + SCHEMA("C_CSPlayerPawn", "m_EconGloves"_hash));
        }
        bool &m_bNeedToReApplyGloves()
        {
            return field<bool>(this, SCHEMA("C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash));
        }
        float &m_flLastSpawnTimeIndex()
        {
            return field<float>(this, SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
        }

        team getTeam()
        {
            return static_cast<team>(m_iTeamNum());
        }
    };

    // Partial layouts for private HUD data not exposed by the entity schema.
    struct HudAddonEntry
    {
        std::byte unknown[0x0C];
        item_handle entityHandle;
    };

    struct HudAddonDictionary
    {
        std::uint32_t count;
        std::uint32_t allocationFlags;
        HudAddonEntry *entries;

        [[nodiscard]] std::uint32_t Capacity() const
        {
            return allocationFlags & 0x7FFFFFFFu;
        }
    };

    struct HudWeaponAddonLayout
    {
        std::byte unknown0000[0x1328];
        HudAddonDictionary addons;
        std::byte unknown1338[0x20];
        std::uint8_t keychainState;
    };

    struct HudWeaponSelectionEntry
    {
        std::byte unknown00[0x38];
        std::int32_t weaponHandle;
        std::byte unknown3C[0x0C];
    };

    struct HudWeaponSelectionLayout
    {
        std::byte unknown00[0x50];
        std::int32_t count;
        std::byte unknown54[0x04];
        HudWeaponSelectionEntry *entries;
        std::byte unknown60[0x38];
        void *hudElementVtable;

        [[nodiscard]] static HudWeaponSelectionLayout *FromHudElement(std::uintptr_t element)
        {
            return reinterpret_cast<HudWeaponSelectionLayout *>(element - offsetof(HudWeaponSelectionLayout, hudElementVtable));
        }
    };

    struct generated_paint_kit
    {
        int id;
        std::uint32_t padding;
        const char *m_Name;
    };
    class composite_material_owner;
    inline composite_material_owner *composite_owner(void *entity)
    {
        return static_cast<composite_material_owner *>(memory::find_nonvirtual_base(entity, ".?AVCCompositeMaterialOwner@@"));
    }
    inline bool connected()
    {
        return addresses::globals::source2engine_to_client && memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) &&
               memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39);
    }
    inline player_items *local_pawn()
    {
        return connected() ? reinterpret_cast<player_items *>(systems::g_local.get().pawn) : nullptr;
    }
} // namespace features::changer::engine
