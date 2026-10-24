module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

// was a deadend.

namespace features::world {

void Smoke::OnMap(std::uintptr_t token, std::size_t size,
                  std::uintptr_t buf_ptr) {
  if (size != 2480 || !token || !buf_ptr) {
    return;
  }

  this->m_token_ = token;
  this->m_buf_ = buf_ptr;
}

void Smoke::OnUnmap(std::uintptr_t token) {
  const auto buf = this->m_buf_;
  if (!buf || token != this->m_token_) {
    return;
  }

  if (settings::g_misc.m_removals.smoke.value) {
    const auto count = *reinterpret_cast<std::uint32_t*>(buf + 2464);
    if (count > 0 && count <= 16) {
      for (auto i = 0u; i < count; ++i) {
        auto opacity = reinterpret_cast<float*>(
            buf + 1280 + 16 * static_cast<std::size_t>(i));
        opacity[0] = 0.0f;
      }
    }
  }

  this->m_buf_ = 0;
  this->m_token_ = 0;
}

}  // namespace features::world
