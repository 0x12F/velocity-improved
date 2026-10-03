#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/knife_model_changer.hpp>


#include <cstdint>

namespace
{
    constexpr int ITEM_QUALITY_UNUSUAL = 3;
using SetModelFn = void(__fastcall*)(void*, const char*);
    using UpdateSubclassFn = void(__fastcall*)(void*);

    SetModelFn GetSetModel()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<SetModelFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::agent_set_model))); });
        return fn;
    }

    UpdateSubclassFn GetUpdateSubclass()
    {
        const auto fn = features::changer::engine::cached_address([] { return reinterpret_cast<UpdateSubclassFn>(reinterpret_cast<std::uint8_t*>(PATTERN(patterns::knife_update_subclass))); });
        return fn;
    }

    bool SetModel(features::changer::engine::weapon* weapon, const char* modelName)
    {
        if (!weapon || !modelName || !*modelName)
            return false;

        const auto fn = GetSetModel();

        if (!fn)
            return false;

        fn(weapon, modelName);
        return true;
    }

    bool UpdateSubclass(features::changer::engine::weapon* weapon)
    {
        if (!weapon)
            return false;

        const auto fn = GetUpdateSubclass();

        if (!fn)
            return false;

        fn(weapon);
        return true;
    }

    std::uint32_t GetKnifeSubclassId(std::uint16_t definitionIndex)
    {
        switch (definitionIndex)
        {
        case 500: return 3933374535u; // Bayonet
        case 503: return 3787235507u; // Classic Knife
        case 505: return 4046390180u; // Flip Knife
        case 506: return 2047704618u; // Gut Knife
        case 507: return 1731408398u; // Karambit
        case 508: return 1638561588u; // M9 Bayonet
        case 509: return 2282479884u; // Huntsman Knife
        case 512: return 3412259219u; // Falchion Knife
        case 514: return 2511498851u; // Bowie Knife
        case 515: return 1353709123u; // Butterfly Knife
        case 516: return 4269888884u; // Shadow Daggers
        case 517: return 1105782941u; // Paracord Knife
        case 518: return 275962944u;  // Survival Knife
        case 519: return 1338637359u; // Ursus Knife
        case 520: return 3230445913u; // Navaja Knife
        case 521: return 3206681373u; // Nomad Knife
        case 522: return 2595277776u; // Stiletto Knife
        case 523: return 4029975521u; // Talon Knife
        case 525: return 365028728u;  // Skeleton Knife
        case 526: return 3845286452u; // Kukri Knife
        default: return 0;
        }
    }
}

bool features::changer::knife_application::IsKnifeDefinition(std::uint16_t definitionIndex)
{
    return definitionIndex == 41 || definitionIndex == 42 || definitionIndex == 59 ||
        (definitionIndex >= 500 && definitionIndex <= 526);
}

bool features::changer::knife_application::Apply(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, std::uint16_t targetDefinitionIndex)
{
    if (!weapon || !item)
        return false;

    if (!IsKnifeDefinition(item->m_iItemDefinitionIndex()) || targetDefinitionIndex < 500)
        return false;

    const features::changer::econ_item_system::item_def* targetItem = features::changer::g_econ_item_system.find_def(targetDefinitionIndex);

    if (!targetItem || targetItem->category != features::changer::econ_item_system::item_category::knife || targetItem->model_player.empty())
        return false;

    const std::uint32_t targetSubclassId = GetKnifeSubclassId(targetDefinitionIndex);

    if (targetSubclassId == 0)
        return false;

    if (item->m_iItemDefinitionIndex() == targetDefinitionIndex &&
        static_cast<std::uint32_t>(weapon->m_nSubclassID()) == targetSubclassId)
    {
        return false;
    }

    item->m_iEntityQuality() = ITEM_QUALITY_UNUSUAL;
    item->m_iItemDefinitionIndex() = targetDefinitionIndex;
    weapon->m_nSubclassID() = static_cast<int>(targetSubclassId);

    if (!UpdateSubclass(weapon))
        return false;

    if (!SetModel(weapon, targetItem->model_player.c_str()))
        return false;

    return true;
}
