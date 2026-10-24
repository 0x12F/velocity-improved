module;

#include "platform/platform.h"

#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>

export module velocity:utilities.security.security;

import <atomic>;
import <cstddef>;
import <cstdint>;
import <thread>;
import <vector>;

export {
namespace security {

namespace integrity {

struct CachedHash {
  std::uintptr_t module_base;
  std::uint32_t crc32;
  bool valid;
};

bool Initialize();
CachedHash* Get(std::uintptr_t module_base);

}  // namespace integrity

namespace regions {

struct Region {
  std::uintptr_t base;
  std::size_t size;
};

void Add(std::uintptr_t base, std::size_t size);
void AddModule(HMODULE module);
bool IsProtected(const void* address, std::size_t size);

}  // namespace regions

namespace prologues {

struct HookInfo {
  std::uintptr_t target;
  const std::uint8_t* bytes;
  std::size_t length;
};

void Add(std::uintptr_t target, const std::uint8_t* bytes, std::size_t length);
HookInfo* Get(std::uintptr_t address);

}  // namespace prologues

}  // namespace security
}  // export
