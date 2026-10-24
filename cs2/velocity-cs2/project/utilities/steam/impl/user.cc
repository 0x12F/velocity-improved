module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace steam {

namespace detail {

inline std::uintptr_t user_interface{};

}  // namespace detail

bool User::Initialize() {
  detail::user_interface = memory::Call<std::uintptr_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamUser_v023"));
  return detail::user_interface != 0;
}

std::uint64_t User::GetSteamId() {
  return memory::Call<std::uint64_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamUser_GetSteamID"),
      detail::user_interface);
}

}  // namespace steam
