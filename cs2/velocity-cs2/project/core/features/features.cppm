module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.features;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;

export import :core.features.changer.changer;
export import :core.features.combat.combat;
export import :core.features.esp.esp;
export import :core.features.misc.misc;
export import :core.features.movement.movement;
export import :core.features.world.world;

export {
namespace features {

namespace combat {

Shared g_shared{};
Misc g_misc{};
Rage g_rage{};
Legit g_legit{};

}  // namespace combat

namespace esp {

namespace player {

Glow g_glow{};
Chams g_chams{};
Overlay g_overlay{};

}  // namespace player

namespace item {

Glow g_glow{};
Chams g_chams{};
Overlay g_overlay{};

}  // namespace item

namespace projectile {

Overlay g_overlay{};
Tracers g_tracers{};

}  // namespace projectile

namespace other {

Overlay g_overlay{};

}  // namespace other

}  // namespace esp

namespace misc {

ProjectileTrajectory g_projectile_trajectory{};
Dlight g_dlight{};
Impacts g_impacts{};
Removals g_removals{};
Camera g_camera{};
Hud g_hud{};
Other g_other{};
ScoreboardWeapons g_scoreboard_weapons{};

}  // namespace misc

namespace movement {

Bhop g_bhop{};
Airstrafe g_airstrafe{};
TestStrafer g_test_strafer{};
Jumpbug g_jumpbug{};
Fastladder g_fastladder{};
Edgejump g_edgejump{};
Edgestop g_edgestop{};
Edgebug g_edgebug{};
Slowwalk g_slowwalk{};

}  // namespace movement

namespace world {

Weather g_weather{};
Scene g_scene{};
Smoke g_smoke{};

}  // namespace world

namespace changer {

EconItemSystem g_econ_item_system{};

}  // namespace changer

}  // namespace features
}  // export
