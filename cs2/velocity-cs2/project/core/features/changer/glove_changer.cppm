module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.glove_changer;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

export import :core.features.changer.cosmetic_configuration;
export import :core.features.changer.engine;

export {
namespace features::changer::glove_application {
void Run(features::changer::engine::PlayerItems* local_pawn);
void ForceUpdate();
void Reset();
}  // namespace features::changer::glove_application
}  // export
