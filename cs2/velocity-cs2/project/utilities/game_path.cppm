module;

#include "platform/platform.h"

export module velocity:utilities.game_path;

import <array>;
import <filesystem>;
import <optional>;
import <system_error>;
import <vector>;

export {
namespace game_path {
[[nodiscard]] inline std::optional<std::filesystem::path> ModulePath(
    HMODULE module) {
  std::vector<wchar_t> buffer(MAX_PATH);

  while (buffer.size() <= 32768) {
    const auto length = GetModuleFileNameW(module, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return std::nullopt;
    }

    if (length < buffer.size()) {
      return std::filesystem::path(buffer.data(), buffer.data() + length);
    }

    buffer.resize(buffer.size() * 2);
  }

  return std::nullopt;
}

// Derive the active CS2 content directory from the loaded game rather than
// assuming which Steam library or drive the user installed it on.
[[nodiscard]] inline std::optional<std::filesystem::path> CsgoDirectory() {
  static const auto directory = []() -> std::optional<std::filesystem::path> {
    const std::array modules{GetModuleHandleW(L"client.dll"),
                             static_cast<HMODULE>(nullptr)};

    for (const auto module : modules) {
      const auto loaded_path = ModulePath(module);
      if (!loaded_path) {
        continue;
      }

      auto current = loaded_path->parent_path();
      while (!current.empty()) {
        const std::array candidates{current, current / L"csgo"};
        for (const auto& candidate : candidates) {
          std::error_code error{};
          if (std::filesystem::is_regular_file(candidate / L"pak01_dir.vpk",
                                               error)) {
            return candidate;
          }
        }

        const auto parent = current.parent_path();
        if (parent == current) {
          break;
        }

        current = parent;
      }
    }

    return std::nullopt;
  }();

  return directory;
}
}  // namespace game_path
}  // export
