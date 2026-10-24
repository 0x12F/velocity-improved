module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace logging::popup {

namespace detail {

inline std::uintptr_t game_ui{0};

}  // namespace detail

bool Initialize() {
  detail::game_ui = memory::FindInstanceByRtti(MODULE_BASE("client.dll"),
                                               xs("CLegacyGameUI"));
  return detail::game_ui != 0;
}

void Show(const char* title, const char* message) {
  if (!detail::game_ui) {
    return;
  }

  memory::CallVfunc<void>(detail::game_ui, 28, title, message, true, false,
                          nullptr, nullptr, nullptr, 0, 0ll);
}

}  // namespace logging::popup
