#ifndef VELOCITY_CORE_FEATURES_FEATURES_H_
#define VELOCITY_CORE_FEATURES_FEATURES_H_

#include "core/features/changer/changer.h"
#include "core/features/combat/combat.h"
#include "core/features/esp/esp.h"
#include "core/features/misc/misc.h"
#include "core/features/movement/movement.h"
#include "core/features/world/world.h"

namespace features {

namespace combat {

inline Shared g_shared{};
inline Misc g_misc{};
inline Rage g_rage{};
inline Legit g_legit{};

}  // namespace combat

namespace esp {

namespace player {

inline Glow g_glow{};
inline Chams g_chams{};
inline Overlay g_overlay{};

}  // namespace player

namespace item {

inline Glow g_glow{};
inline Chams g_chams{};
inline Overlay g_overlay{};

}  // namespace item

namespace projectile {

inline Overlay g_overlay{};
inline Tracers g_tracers{};

}  // namespace projectile

namespace other {

inline Overlay g_overlay{};

}  // namespace other

}  // namespace esp

namespace misc {

inline ProjectileTrajectory g_projectile_trajectory{};
inline Dlight g_dlight{};
inline Impacts g_impacts{};
inline Removals g_removals{};
inline Camera g_camera{};
inline Hud g_hud{};
inline Other g_other{};
inline ScoreboardWeapons g_scoreboard_weapons{};

}  // namespace misc

namespace movement {

inline Bhop g_bhop{};
inline Airstrafe g_airstrafe{};
inline TestStrafer g_test_strafer{};
inline Jumpbug g_jumpbug{};
inline Fastladder g_fastladder{};
inline Edgejump g_edgejump{};
inline Edgestop g_edgestop{};
inline Edgebug g_edgebug{};
inline Slowwalk g_slowwalk{};

}  // namespace movement

namespace world {

inline Weather g_weather{};
inline Scene g_scene{};
inline Smoke g_smoke{};

}  // namespace world

namespace changer {

inline EconItemSystem g_econ_item_system{};

}  // namespace changer

}  // namespace features

#endif  // VELOCITY_CORE_FEATURES_FEATURES_H_
