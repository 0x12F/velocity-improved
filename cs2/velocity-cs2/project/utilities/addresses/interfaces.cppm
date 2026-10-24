module;

#include "platform/platform.h"

export module velocity:utilities.addresses.interfaces;

import <cstdint>;
import <type_traits>;

export import :utilities.fnv1a;

export {
union ConvarValue {
  bool i1;
  short i16;
  int i32;
  int64_t i64;
  float fl;
  double db;
  const char* sz;
};

class CConvar {
 public:
  const char* m_name;               // 0x0000
  const void* m_default_value_ptr;  // 0x0008
  const void* m_min_value;          // 0x0010
  const void* m_max_value;          // 0x0018
  const char* m_description;        // 0x0020
  int16_t m_type;                   // 0x28
  char pad_01[0x2];                 // 0x2A
  uint32_t m_change_count;          // 0x2C
  uint64_t m_flags;                 // 0x30
  char pad_02[0x20];                // 0x38
  ConvarValue m_value;              // 0x58
  char value_tail[0x8];             // 0x60

  template <typename T>
  T Get() {
    if constexpr (std::is_same_v<T, bool>)
      return m_value.i1;
    else if constexpr (std::is_same_v<T, short>)
      return m_value.i16;
    else if constexpr (std::is_same_v<T, int>)
      return m_value.i32;
    else if constexpr (std::is_same_v<T, int64_t>)
      return m_value.i64;
    else if constexpr (std::is_same_v<T, float>)
      return m_value.fl;
    else
      return T{};
  }
};

namespace interfaces {

class CEngineCvar {
 public:
  struct CvarContainerT {
    CConvar* m_cvar;        // 0x000
    uint16_t m_generation;  // 0x008
    uint16_t m_next_index;  // 0x00A
    uint32_t m_links;       // 0x00C
  };

  char pad0[0x4A];              // 0x0000
  uint16_t m_allocation_count;  // 0x004A
  char pad1[0x4];               // 0x004C
  CvarContainerT* m_container;  // 0x0050
  uint16_t m_head;              // 0x0058

  [[nodiscard]] CConvar* Find(std::uint32_t name_hash);

  bool UnlockAll();
};

}  // namespace interfaces

static_assert(offsetof(CConvar, m_flags) == 0x30);
static_assert(offsetof(CConvar, m_value) == 0x58);
static_assert(sizeof(interfaces::CEngineCvar::CvarContainerT) == 0x10);
static_assert(offsetof(interfaces::CEngineCvar, m_container) == 0x50);
static_assert(offsetof(interfaces::CEngineCvar, m_head) == 0x58);
}  // export
