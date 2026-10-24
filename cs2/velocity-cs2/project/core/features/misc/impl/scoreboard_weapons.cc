module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::misc {

static constexpr const char* kSetupScript = R"PANORAMA(
(function () {

	if (typeof SClient !== "undefined") {
		SClient = undefined;
	}

	SClient = (function () {
		var handlers = {};
		return {
			register_handler: function (type, callback) { handlers[type] = callback; },
			receive: function (msg) {
				if (msg && handlers[msg.type]) handlers[msg.type](msg);
			}
		};
	})();

	SWeaponManager = (function () {

		function isValid(panel) {
			return panel && panel.IsValid();
		}

		function getScoreboard() {
			var root = $.GetContextPanel();
			if (!isValid(root)) return null;
			var scoreboard = root.id === "Scoreboard" ? root : root.FindChildTraverse("Scoreboard");
			return isValid(scoreboard) ? scoreboard : null;
		}

		function getRow(sb, xuid, account_id) {
			var keys = [xuid, account_id];
			var prefixes = ["player-", "id-", "id-player-", "player_"];
			for (var i = 0; i < keys.length; ++i) {
				if (!keys[i]) continue;
				for (var j = 0; j < prefixes.length; ++j) {
					var row = sb.FindChildTraverse(prefixes[j] + keys[i]);
					if (isValid(row)) return row;
				}
			}
			return null;
		}

		function getSize(w) {
			var t = w.type, p = w.path;
			if (t === 1) {
				if (p.indexOf("usp_silencer") !== -1) return "42px";
				if (p.indexOf("deagle")       !== -1) return "36px";
				if (p.indexOf("revolver")     !== -1) return "32px";
				if (p.indexOf("tec9")         !== -1) return "36px";
				if (p.indexOf("elite")        !== -1) return "32px";
				return "30px";
			}
			if (t === 2 || t === 3 || t === 4 || t === 5 || t === 6)
				return p.indexOf("mac10") !== -1 ? "30px" : "52px";
			if (t === 9) {
				if (p.indexOf("incgrenade")   !== -1 || p.indexOf("smokegrenade") !== -1) return "14px";
				if (p.indexOf("molotov")      !== -1 || p.indexOf("flashbang")    !== -1) return "16px";
				return "14px";
			}
			if (t === 7)  return "16px";
			if (t === 8)  return "22px";
			if (t === 11) return p.indexOf("healthshot") !== -1 ? "18px" : "14px";
			if (t === 0)  return "46px";
			return "16px";
		}

		function updateWeaponIcon(parent, w, active_path) {
			var id   = "wep_" + w.path.replace(/[^a-zA-Z0-9]/g, "_");
			var img  = parent.FindChildTraverse(id);
			if (!isValid(img)) {
				img = $.CreatePanel("Image", parent, id);
			}

			img.style.verticalAlign = "center";
			img.style.margin        = "0px 1px";
			img.scaling    = "stretch-aspect-preserve";

			var finalPath = w.path;
			if (finalPath.indexOf("file://") !== 0) {
				if (finalPath.indexOf("icons/equipment") === -1)
					finalPath = "icons/equipment/" + finalPath;
				if (finalPath.indexOf(".svg") === -1 && finalPath.indexOf(".vsvg") === -1)
					finalPath += ".svg";
				finalPath = "file://{images}/" + finalPath;
			}
			img.SetImage(finalPath);

			var sz           = getSize(w);
			img.style.height = "16px";
			img.style.width  = sz;
			img.style.opacity = (w.path === active_path) ? "1.0" : "0.35";
			img.style.visibility = "visible";
		}

		function updateNow(xuid, account_id, weapons, active_path) {
			var sb = getScoreboard();
			if (!sb) return;

			var row = getRow(sb, xuid, account_id);
			if (!row) return;

			var nameIcons = row.FindChildTraverse("id-sb-name__nameicons");
			if (!isValid(nameIcons)) return;

			var containerId = "custom-weapons-container-" + xuid;
			var container   = nameIcons.FindChildTraverse(containerId);

			if (!weapons || weapons.length === 0) {
				if (isValid(container)) container.style.visibility = "collapse";
				return;
			}

			if (!isValid(container)) {
				container = $.CreatePanel("Panel", nameIcons, containerId);
				container.AddClass("custom-weapons-container");
				container.style.flowChildren     = "right";
				container.style.height           = "20px";
				container.style.width            = "fit-children";
				container.style.padding          = "1px 4px";
				container.style.verticalAlign    = "center";
				container.style.marginLeft       = "3px";
				container.style.backgroundColor  = "rgba(0,0,0,0.35)";
				container.style.borderRadius     = "3px";
				container.style.border           = "1px solid rgba(255,255,255,0.18)";
			}

			// Scoreboard rows cache their paint commands. Keep panel identities stable:
			// deleting children while Panorama is updating that cache can leave native
			// panel references dangling until the next paint pass.
			var children = container.Children();
			for (var i = 0; i < children.length; ++i) {
				if (isValid(children[i])) children[i].style.visibility = "collapse";
			}

			var primary = [], pistols = [], equip = [];
			weapons.forEach(function (w) {
				if (w.type === 2 || w.type === 3 || w.type === 4 || w.type === 5 || w.type === 6)
					primary.push(w);
				else if (w.type === 1)
					pistols.push(w);
				else
					equip.push(w);
			});

			primary.concat(pistols).concat(equip).forEach(function (w) {
				updateWeaponIcon(container, w, active_path);
			});
			container.style.visibility = "visible";
		}

		return {
			update: function (xuid, account_id, weapons, active_path) {
				// RunScript already enters this panel's V8 context. Scheduling each
				// update additionally churns CUIEngine's native async-event queue.
				updateNow(xuid, account_id, weapons, active_path);
			},

			clear: function () {
				var sb = getScoreboard();
				if (!sb) return;
				var containers = sb.FindChildrenWithClassTraverse("custom-weapons-container");
				for (var i = 0; i < containers.length; ++i) {
					if (isValid(containers[i])) containers[i].style.visibility = "collapse";
				}
			}
		};

	})();

	SClient.register_handler("updateWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, msg.content.account_id, msg.content.weapons, msg.content.active_path);
	});

	SClient.register_handler("clearWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, msg.content.account_id, [], "");
	});

})();
)PANORAMA";

void CUiEngine::RunScript(CUiPanel* panel, const char* script) {
  static constexpr char kOriginFile[] = "";
  memory::CallVfunc<void>(reinterpret_cast<std::uintptr_t>(this), 77, panel,
                          script, kOriginFile, static_cast<std::uint64_t>(1));
}

CUiEngine* CPanoramaUiEngine::GetUiEngine() {
  return memory::CallVfunc<CUiEngine*>(reinterpret_cast<std::uintptr_t>(this),
                                       13);
}

CUiPanel* ScoreboardWeapons::FindHudPanel() const {
  if (!addresses::globals::hud) return nullptr;

  const auto hud =
      memory::SafeRead<std::uintptr_t>(addresses::globals::hud).value_or(0);
  if (!hud) return nullptr;

  const auto panel = memory::SafeRead<CUiPanel*>(hud + 0x8).value_or(nullptr);
  if (!panel) return nullptr;

  const auto vtable =
      memory::SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(panel));
  return vtable && *vtable ? panel : nullptr;
}

void ScoreboardWeapons::OnLevelChange() {
  m_script_injected_ = false;
  m_cache_.clear();
  m_scoreboard_open_ = false;
  m_throttle_ = 0;
  m_init_throttle_ = 0;
  m_ui_engine_ = nullptr;
  m_script_panel_ = nullptr;

  logging::console::Print(
      xs("[scoreboard_weapons] level change — state reset\n"));
}

void ScoreboardWeapons::OnFrameStageNotify() {
  if (!settings::g_misc.m_scoreboard_weapons.enabled.value) {
    if (m_script_injected_) {
      ClearAll();
      m_script_injected_ = false;
    }
    m_scoreboard_open_ = false;
    m_cache_.clear();
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.IsValid()) return;

  const auto scoreboard_open = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
  if (!scoreboard_open) {
    if (m_scoreboard_open_ && m_script_injected_) ClearAll();
    m_scoreboard_open_ = false;
    return;
  }

  if (!m_scoreboard_open_) {
    m_scoreboard_open_ = true;
    m_cache_.clear();
    m_throttle_ = 0;
    TryInitialize();
  }

  if (!m_script_injected_) {
    ++m_init_throttle_;
    if (m_init_throttle_ % 30 == 0) TryInitialize();

    if (!m_script_injected_) return;
  }

  ++m_throttle_;
  if (m_throttle_ % 8 != 0) return;
  if (m_throttle_ % 64 == 0) m_cache_.clear();

  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);
  const auto items =
      systems::g_entities.GetByType(systems::Entities::Type::kItem);
  if (m_throttle_ == 8) {
    logging::console::Print(xs("[scoreboard_weapons] scoreboard opened; "
                               "players={} weapon_entities={}\n"),
                            players.size(), items.size());
  }
  for (const auto& player : players) {
    if (!player.ptr) continue;

    SendPlayerWeapons(player.ptr, items);
  }
}

void ScoreboardWeapons::TryInitialize() {
  if (m_script_injected_) return;

  if (!addresses::globals::panorama) {
    logging::console::Print(
        xs("[scoreboard_weapons] panorama address not ready\n"));
    return;
  }

  auto* panorama =
      reinterpret_cast<CPanoramaUiEngine*>(addresses::globals::panorama);
  auto* ui_engine = panorama->GetUiEngine();
  if (!ui_engine) {
    logging::console::Print(
        xs("[scoreboard_weapons] get_ui_engine returned null\n"));
    return;
  }

  const auto script_panel = FindHudPanel();
  if (!script_panel) {
    logging::console::Print(
        xs("[scoreboard_weapons] HUD script panel not ready\n"));
    return;
  }

  m_ui_engine_ = ui_engine;
  m_script_panel_ = script_panel;

  logging::console::Print(xs("[scoreboard_weapons] injecting HUD setup script "
                             "(engine={:p} panel={:p})\n"),
                          static_cast<void*>(m_ui_engine_),
                          static_cast<void*>(m_script_panel_));

  if (RunScript(kSetupScript)) {
    m_script_injected_ = true;
    logging::console::Print(xs("[scoreboard_weapons] setup script injected\n"));
  }
}

bool ScoreboardWeapons::RunScript(const std::string& script) {
  if (!m_ui_engine_ || !m_script_panel_) return false;

  auto* panorama =
      reinterpret_cast<CPanoramaUiEngine*>(addresses::globals::panorama);
  if (!panorama || panorama->GetUiEngine() != m_ui_engine_ ||
      FindHudPanel() != m_script_panel_) {
    m_script_injected_ = false;
    m_ui_engine_ = nullptr;
    m_script_panel_ = nullptr;
    m_cache_.clear();
    return false;
  }

  const auto engine = reinterpret_cast<std::uintptr_t>(m_ui_engine_);
  const auto vtable = memory::SafeRead<std::uintptr_t>(engine);
  const auto function = vtable ? memory::SafeRead<std::uintptr_t>(
                                     *vtable + 77 * sizeof(std::uintptr_t))
                               : std::nullopt;
  const auto panorama_begin = addresses::modules::panorama;
  const auto panorama_end =
      panorama_begin + memory::GetModuleSize(panorama_begin);
  if (!function || *function < panorama_begin || *function >= panorama_end)
    return false;

  m_ui_engine_->RunScript(m_script_panel_, script.c_str());
  return true;
}

void ScoreboardWeapons::SendPlayerWeapons(
    std::uintptr_t controller,
    std::span<const systems::Entities::Cached> items) {
  if (!controller) return;

  constexpr std::uint64_t kSteamIdBase = 76561197960265728ull;
  const auto steamid =
      memory::SafeRead<std::uint64_t>(
          controller + SCHEMA("CBasePlayerController", "m_steamID"_hash))
          .value_or(0);
  if (steamid < kSteamIdBase) return;

  const auto pawn_handle =
      memory::SafeRead<std::uint32_t>(
          controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash))
          .value_or(0);
  if (!pawn_handle) return;

  const auto pawn = systems::g_entities.Lookup(pawn_handle);

  PlayerWeaponState state{};
  const auto read_weapon =
      [](std::uintptr_t weapon) -> std::optional<WeaponEntry> {
    if (!weapon) return std::nullopt;

    const auto vdata =
        memory::SafeRead<std::uintptr_t>(
            weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8)
            .value_or(0);
    if (!vdata) return std::nullopt;

    const auto name_ptr =
        memory::SafeRead<const char*>(
            vdata + SCHEMA("CCSWeaponBaseVData", "m_szName"_hash))
            .value_or(nullptr);
    if (!name_ptr) return std::nullopt;

    std::string name{};
    name.reserve(32);
    for (std::size_t i = 0; i < 64; ++i) {
      const auto character = memory::SafeRead<char>(
          reinterpret_cast<std::uintptr_t>(name_ptr) + i);
      if (!character) return std::nullopt;
      if (*character == '\0') break;
      name.push_back(*character);
    }

    if (!name.starts_with(xs("weapon_"))) return std::nullopt;
    name.erase(0, 7);

    const auto type = memory::SafeRead<std::uint32_t>(
        vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash));
    if (!type) return std::nullopt;

    return WeaponEntry{std::move(name), static_cast<int>(*type)};
  };

  if (pawn) {
    const auto health =
        memory::SafeRead<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash))
            .value_or(0);

    if (health > 0) {
      const auto weapon_services =
          memory::SafeRead<std::uintptr_t>(
              pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash))
              .value_or(0);

      if (weapon_services) {
        const auto active_handle =
            memory::SafeRead<std::uint32_t>(
                weapon_services +
                SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash))
                .value_or(0);
        const auto active_weapon =
            active_handle ? systems::g_entities.Lookup(active_handle) : 0;

        if (const auto active = read_weapon(active_weapon))
          state.active_name = active->name;
      }

      // Enemy inventory handles are not reliably populated in m_hMyWeapons.
      // Weapon entities and their networked owner handles are, so mirror the
      // working scoreboard implementation and collect by owner instead.
      for (const auto& item : items) {
        if (!item.ptr) continue;

        const auto owner_handle =
            memory::SafeRead<std::uint32_t>(
                item.ptr + SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash))
                .value_or(0);
        if (!owner_handle || systems::g_entities.Lookup(owner_handle) != pawn)
          continue;

        if (auto weapon = read_weapon(item.ptr))
          state.weapons.emplace_back(std::move(*weapon));
      }
    }
  }

  if (m_throttle_ == 8) {
    logging::console::Print(
        xs("[scoreboard_weapons] xuid={} collected_weapons={}\n"), steamid,
        state.weapons.size());
  }

  // skip if nothing changed
  const auto it = m_cache_.find(steamid);
  if (it != m_cache_.end() && it->second == state) return;

  if (state.weapons.empty()) {
    if (SendClear(steamid)) m_cache_[steamid] = state;
    return;
  }

  // sort: primaries → pistols → grenades/other  (mirrors JS-side sort)
  auto sort_key = [](int type) -> int {
    if (type == 2 || type == 3 || type == 4 || type == 5 || type == 6)
      return 0;               // primary
    if (type == 1) return 1;  // pistol
    return 2;                 // grenades / equipment
  };
  std::stable_sort(state.weapons.begin(), state.weapons.end(),
                   [&](const WeaponEntry& a, const WeaponEntry& b) {
                     return sort_key(a.type) < sort_key(b.type);
                   });

  // build weapons JSON
  std::string weapons_json = "[";
  for (std::size_t i = 0; i < state.weapons.size(); ++i) {
    weapons_json += std::format(R"({{path:"{}",type:{}}})",
                                state.weapons[i].name, state.weapons[i].type);
    if (i + 1 < state.weapons.size()) weapons_json += ",";
  }
  weapons_json += "]";

  const auto account_id = steamid - kSteamIdBase;
  const auto script = std::format(
      R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"updateWeapons",content:{{xuid:"{}",account_id:"{}",weapons:{},active_path:"{}"}}}});}})",
      steamid, account_id, weapons_json, state.active_name);

  if (RunScript(script)) m_cache_[steamid] = std::move(state);
}

bool ScoreboardWeapons::SendClear(std::uint64_t steamid) {
  constexpr std::uint64_t kSteamIdBase = 76561197960265728ull;
  const auto account_id =
      steamid >= kSteamIdBase ? steamid - kSteamIdBase : steamid;
  const auto script = std::format(
      R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"clearWeapons",content:{{xuid:"{}",account_id:"{}"}}}});}})",
      steamid, account_id);

  return RunScript(script);
}

void ScoreboardWeapons::ClearAll() {
  if (!m_script_injected_) return;

  (void)RunScript(
      R"(if(typeof(SWeaponManager)!=='undefined'){SWeaponManager.clear();})");
  m_cache_.clear();
}

}  // namespace features::misc
