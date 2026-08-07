#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../steam.hpp"

namespace steam {

	namespace detail {

		inline std::uintptr_t friends_interface {};

	} // namespace detail

	bool friends::initialize () {
		detail::friends_interface = memory::call<std::uintptr_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_SteamFriends_v018"));
		return detail::friends_interface != 0;
	}

	int friends::get_medium_friend_avatar (std::uint64_t steam_id) {
		return memory::call<int> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamFriends_GetMediumFriendAvatar"), detail::friends_interface, steam_id);
	}

} // namespace steam