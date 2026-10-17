#include "pch/pch.h"

#include "protection/game_addresses.h"
#include "utilities/memory/memory.h"
#include "utilities/steam/steam.h"

namespace steam {

namespace detail {

inline std::uintptr_t friends_interface{};

}  // namespace detail

bool Friends::Initialize() {
  detail::friends_interface = memory::Call<std::uintptr_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamFriends_v018"));
  return detail::friends_interface != 0;
}

int Friends::GetMediumFriendAvatar(std::uint64_t steam_id) {
  return memory::Call<int>(
      MODULE_EXPORT(
          "steam_api64.dll:SteamAPI_ISteamFriends_GetMediumFriendAvatar"),
      detail::friends_interface, steam_id);
}

}  // namespace steam