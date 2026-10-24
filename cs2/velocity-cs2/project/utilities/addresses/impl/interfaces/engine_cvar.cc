module;

#include "platform/platform.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace interfaces {

namespace {

constexpr std::uint16_t kInvalidIndex = static_cast<std::uint16_t>(-1);
constexpr std::uint16_t kCapacityMask = 0x7FFF;

[[nodiscard]] bool TryHashNameImpl(const char* name, std::uint32_t& hash) {
  if (!name) return false;

  __try {
    hash = fnv1a::RuntimeHash(name);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

[[nodiscard]] bool TryHashName(const char* name, std::uint32_t& hash) {
  diag::ProbeScope probe;
  return TryHashNameImpl(name, hash);
}

}  // namespace

CConvar* CEngineCvar::Find(std::uint32_t name_hash) {
  const auto capacity =
      static_cast<std::uint16_t>(m_allocation_count & kCapacityMask);
  if (m_head != kInvalidIndex && (!m_container || capacity == 0)) return 0;

  auto current = m_head;
  for (std::size_t visited = 0; current != kInvalidIndex && visited < capacity;
       ++visited) {
    if (current >= capacity) return 0;

    const auto entry = memory::SafeRead<CvarContainerT>(
        reinterpret_cast<std::uintptr_t>(m_container) +
        current * sizeof(CvarContainerT));
    if (!entry) return 0;

    if (entry->m_cvar) {
      const auto name = memory::SafeRead<const char*>(
          reinterpret_cast<std::uintptr_t>(entry->m_cvar) +
          offsetof(CConvar, m_name));
      std::uint32_t hash{};
      if (name && TryHashName(*name, hash) && hash == name_hash)
        return entry->m_cvar;
    }

    current = entry->m_next_index;
  }

  return 0;
}

bool CEngineCvar::UnlockAll() {
  constexpr std::uint64_t kFcvarDevelopmentonly = 1ull << 1;
  constexpr std::uint64_t kFcvarHidden = 1ull << 4;
  constexpr std::uint64_t kFcvarRegistryRestricted = 1ull << 10;
  constexpr auto kRestrictionMask =
      kFcvarDevelopmentonly | kFcvarHidden | kFcvarRegistryRestricted;

  const auto capacity =
      static_cast<std::uint16_t>(m_allocation_count & kCapacityMask);
  if (m_head != kInvalidIndex && (!m_container || capacity == 0)) return false;

  auto current = m_head;
  std::size_t visited = 0;
  for (; current != kInvalidIndex && visited < capacity; ++visited) {
    if (current >= capacity) return false;

    const auto entry = memory::SafeRead<CvarContainerT>(
        reinterpret_cast<std::uintptr_t>(m_container) +
        current * sizeof(CvarContainerT));
    if (!entry) return false;

    if (entry->m_cvar) {
      const auto flags_address =
          reinterpret_cast<std::uintptr_t>(entry->m_cvar) +
          offsetof(CConvar, m_flags);
      const auto flags = memory::SafeRead<std::uint64_t>(flags_address);
      if (!flags ||
          !memory::SafeWrite(flags_address, *flags & ~kRestrictionMask))
        return false;
    }

    current = entry->m_next_index;
  }

  return current == kInvalidIndex;
}

}  // namespace interfaces
