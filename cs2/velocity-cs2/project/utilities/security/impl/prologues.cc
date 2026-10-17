#include "pch/pch.h"

#include "utilities/security/security.h"

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