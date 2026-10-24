module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace steam {

namespace detail {

inline std::uintptr_t utils_interface{};

}  // namespace detail

bool Utils::Initialize() {
  detail::utils_interface = memory::Call<std::uintptr_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamUtils_v010"));
  return detail::utils_interface != 0;
}

bool Utils::GetImageSize(int image, std::uint32_t* width,
                         std::uint32_t* height) {
  if (image <= 0) {
    return false;
  }

  return memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamUtils_GetImageSize"),
      detail::utils_interface, image, width, height);
}

bool Utils::GetImageRgba(int image, std::uint8_t* dest, int dest_size) {
  if (image <= 0) {
    return false;
  }

  return memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamUtils_GetImageRGBA"),
      detail::utils_interface, image, dest, dest_size);
}

}  // namespace steam
