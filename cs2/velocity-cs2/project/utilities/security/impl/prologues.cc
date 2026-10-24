module;

#include "platform/platform.h"

#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>

module velocity;

import <cstddef>;
import <thread>;

namespace security::prologues {

namespace detail {

std::vector<HookInfo> hooks{};

}  // namespace detail

void Add(std::uintptr_t target, const std::uint8_t* bytes, std::size_t length) {
  detail::hooks.push_back({target, bytes, length});
}

HookInfo* Get(std::uintptr_t address) {
  for (auto& hook : detail::hooks) {
    if (address >= hook.target && address < hook.target + hook.length) {
      return &hook;
    }
  }

  return nullptr;
}

}  // namespace security::prologues
