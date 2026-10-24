module;

#include "platform/platform.h"

#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>

module velocity;

import <cstddef>;
import <thread>;

namespace security::regions {

namespace detail {

std::vector<Region> regions{};

}  // namespace detail

void Add(std::uintptr_t base, std::size_t size) {
  detail::regions.push_back({base, size});
}

void AddModule(HMODULE module) {
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);

  Add(base, nt->OptionalHeader.SizeOfImage);
}

bool IsProtected(const void* address, std::size_t size) {
  const auto addr = reinterpret_cast<std::uintptr_t>(address);
  const auto end = addr + size;

  for (const auto& region : detail::regions) {
    const auto region_end = region.base + region.size;

    if (addr < region_end && end > region.base) {
      return true;
    }
  }

  return false;
}

}  // namespace security::regions
