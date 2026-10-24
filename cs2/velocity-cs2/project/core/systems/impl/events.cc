module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace systems {

bool Events::Initialize() {
  if (!RegisterListener(xs("bullet_impact"), [](void* event) {
        features::misc::g_impacts.OnBulletImpact(
            reinterpret_cast<std::uintptr_t>(event));
      })) {
    return false;
  }

  if (!RegisterListener(xs("player_hurt"), [](void* event) {
        features::misc::g_impacts.OnPlayerHurt(
            reinterpret_cast<std::uintptr_t>(event));
      })) {
    return false;
  }

  if (!RegisterListener(xs("round_start"), [](void* event) {
        features::misc::g_other.OnRoundStart();
      })) {
    return false;
  }

  if (!RegisterListener(xs("player_death"), [](void* event) {
        features::misc::g_other.OnPlayerDeath(
            reinterpret_cast<std::uintptr_t>(event));
      })) {
    return false;
  }

  return true;
}

void Events::Shutdown() {
  for (auto& entry : m_listeners_) {
    if (entry->registered) {
      memory::CallVfunc<void>(addresses::globals::game_event_manager, 5,
                              &entry->listener);
      entry->registered = false;
    }
  }

  m_listeners_.clear();
}

bool Events::RegisterListener(const char* event_name, HandlerFn handler) {
  if (!event_name || !handler) {
    return false;
  }

  auto current_entry = std::make_unique<ListenerEntry>();
  current_entry->handler = handler;
  current_entry->name = event_name;
  current_entry->registered = false;

  current_entry->vtable_data[0] = nullptr;
  current_entry->vtable_data[1] = reinterpret_cast<void*>(&FireEvent);
  current_entry->vtable_data[2] = reinterpret_cast<void*>(&GetDebugId);

  current_entry->listener.vtable = current_entry->vtable_data;
  current_entry->listener.debug_id = static_cast<int>(m_listeners_.size() + 1);

  const auto success =
      memory::CallVfunc<bool>(addresses::globals::game_event_manager, 3,
                              &current_entry->listener, event_name, false);
  if (!success) {
    return false;
  }

  current_entry->registered = true;
  m_listeners_.push_back(std::move(current_entry));

  return true;
}

void Events::UnregisterListener(const char* event_name) {
  if (!event_name) {
    return;
  }

  for (auto it = m_listeners_.begin(); it != m_listeners_.end(); ++it) {
    if (std::strcmp((*it)->name, event_name) == 0 && (*it)->registered) {
      memory::CallVfunc<void>(addresses::globals::game_event_manager, 5,
                              &(*it)->listener);
      (*it)->registered = false;
      m_listeners_.erase(it);
      return;
    }
  }
}

void* __fastcall Events::FireEvent(void* self, void* event) {
  const auto current_listener = reinterpret_cast<Listener*>(self);

  for (const auto& entry : m_listeners_) {
    if (entry->listener.debug_id == current_listener->debug_id &&
        entry->handler) {
      entry->handler(event);
      break;
    }
  }

  return nullptr;
}

int __fastcall Events::GetDebugId(void* self) {
  const auto current_listener = reinterpret_cast<Listener*>(self);
  return current_listener->debug_id;
}

}  // namespace systems
