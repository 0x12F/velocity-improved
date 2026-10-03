#pragma once
#include <core/features/changer/engine.hpp>
#include <core/features/changer/cosmetic_configuration.hpp>

#include <cstdint>

namespace features::changer::knife_application
{
    [[nodiscard]] bool IsKnifeDefinition(std::uint16_t definitionIndex);
    bool Apply(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, std::uint16_t targetDefinitionIndex);
}