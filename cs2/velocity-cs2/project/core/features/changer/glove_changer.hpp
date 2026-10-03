#pragma once
#include <core/features/changer/engine.hpp>
#include <core/features/changer/cosmetic_configuration.hpp>


namespace features::changer::glove_application
{
    void Run(features::changer::engine::player_items* localPawn);
    void ForceUpdate();
    void Reset();
}