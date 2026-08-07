#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../steam.hpp"

namespace steam {

	namespace detail {

		inline std::uintptr_t utils_interface {};

	} // namespace detail

	bool utils::initialize () {
		detail::utils_interface = memory::call<std::uintptr_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_SteamUtils_v010"));
		return detail::utils_interface != 0;
	}

	bool utils::get_image_size (int image, std::uint32_t* width, std::uint32_t* height) {
		if (image <= 0) {
			return false;
		}

		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamUtils_GetImageSize"), detail::utils_interface, image, width, height);
	}

	bool utils::get_image_rgba (int image, std::uint8_t* dest, int dest_size) {
		if (image <= 0) {
			return false;
		}

		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamUtils_GetImageRGBA"), detail::utils_interface, image, dest, dest_size);
	}

} // namespace steam