module;

#include "platform/platform.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace memory {
// MSVC x64 RTTI uses image-relative addresses in these descriptors.
struct CompleteObjectLocator {
  std::uint32_t signature;
  std::uint32_t offset;
  std::uint32_t constructor_displacement;
  std::uint32_t type_descriptor_rva;
  std::uint32_t hierarchy_rva;
  std::uint32_t self_rva;
};

struct ClassHierarchyDescriptor {
  std::uint32_t signature;
  std::uint32_t attributes;
  std::uint32_t base_count;
  std::uint32_t base_array_rva;
};

struct BaseClassDescriptor {
  std::uint32_t type_descriptor_rva;
  std::uint32_t contained_bases;
  std::int32_t member_displacement;
  std::int32_t vbtable_displacement;
  std::int32_t virtual_displacement;
  std::uint32_t attributes;
  std::uint32_t hierarchy_rva;
};

struct TypeDescriptor {
  const void *vtable;
  void *spare;
  char name[1];
};

void *FindNonvirtualBase(void *object, const char *type_name) {
  if (!object || !type_name) return nullptr;

  __try {
    const auto *vtable =
        *reinterpret_cast<const std::uintptr_t *const *>(object);
    if (!vtable) return nullptr;

    const auto *locator =
        reinterpret_cast<const CompleteObjectLocator *>(vtable[-1]);
    if (!locator || locator->signature != 1 || !locator->self_rva ||
        !locator->hierarchy_rva || locator->constructor_displacement != 0)
      return nullptr;

    const std::uintptr_t image_base =
        reinterpret_cast<std::uintptr_t>(locator) - locator->self_rva;
    const auto *hierarchy = reinterpret_cast<const ClassHierarchyDescriptor *>(
        image_base + locator->hierarchy_rva);
    if (!hierarchy->base_array_rva || hierarchy->base_count == 0 ||
        hierarchy->base_count > 1024)
      return nullptr;

    const auto *bases = reinterpret_cast<const std::uint32_t *>(
        image_base + hierarchy->base_array_rva);
    auto *complete_object = static_cast<std::byte *>(object) - locator->offset;
    void *result = nullptr;

    for (std::uint32_t i = 0; i < hierarchy->base_count; ++i) {
      if (!bases[i]) return nullptr;

      const auto *base =
          reinterpret_cast<const BaseClassDescriptor *>(image_base + bases[i]);
      if (!base->type_descriptor_rva) return nullptr;

      const auto *type = reinterpret_cast<const TypeDescriptor *>(
          image_base + base->type_descriptor_rva);
      if (std::strcmp(type->name, type_name) != 0) continue;

      // The supported model entities use a unique, non-virtual owner base.
      if (base->vbtable_displacement != -1 || base->member_displacement < 0 ||
          (base->attributes & 0x2u))
        return nullptr;

      void *candidate = complete_object + base->member_displacement;
      if (result && result != candidate) return nullptr;

      result = candidate;
    }

    return result;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}
}  // namespace memory
