module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

std::uint32_t Schemas::Lookup(const char* class_name,
                              std::uint32_t field_hash) {
  const auto type_scope = memory::CallVfunc<std::uintptr_t>(
      addresses::globals::schema_system, 13, xs("client.dll"), nullptr);
  if (!type_scope) {
    return 0;
  }

  auto class_info{0ull};
  memory::CallVfunc<void>(type_scope, 2, &class_info, class_name);

  if (!class_info) {
    return 0;
  }

  const auto fields_ptr = memory::Read<std::uintptr_t>(class_info + 0x30);
  const auto field_count = memory::Read<std::uint16_t>(class_info + 0x24);

  if (!fields_ptr || !field_count) {
    return 0;
  }

  for (std::uint16_t i = 0; i < field_count; ++i) {
    const auto field_addr = fields_ptr + (static_cast<std::size_t>(i) * 0x20);
    const auto name_ptr = memory::Read<std::uintptr_t>(field_addr);

    if (!name_ptr) {
      continue;
    }

    if (fnv1a::RuntimeHash(reinterpret_cast<const char*>(name_ptr)) ==
        field_hash) {
      return memory::Read<std::uint32_t>(field_addr + 0x10);
    }
  }

  return 0;
}

}  // namespace systems
