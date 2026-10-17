#ifndef VELOCITY_PROTECTION_GAME_ADDRESSES_H_
#define VELOCITY_PROTECTION_GAME_ADDRESSES_H_

#include <cstddef>
#include <cstdint>

#include "utilities/memory/memory.h"

/*
 * @info
 * All addresses have meta data like
 * All patterns live in protection/patterns.h — update signatures there 1:1.
 * PATTERN ( patterns::name )
 * MODULE ( "MODULE" )
 * INTERFACE_ ( "MODULE:INTERFACE_NAME" )
 * CONVAR ( "CONVAR_NAME" )
 *
 * We dont need to store decrypted address, we need to use it in code
 *
 *
 * for example:
 *
 * bad:
 * detail::address_1 = PATTERN (patterns::name); then in another function call
 * detail::address_1 good: call (PATTERN (patterns::name))
 */

namespace protection::addresses {
constexpr std::uint32_t HashConst(const char* str,
                                  std::uint32_t value = 0x811C9DC5u) {
  return *str ? HashConst(str + 1, (value ^ std::uint32_t(*str)) * 0x01000193u)
              : value;
}

template <std::size_t N>
consteval std::uint32_t Hash(const char (&str)[N]) {
  return HashConst(str);
}

template <std::size_t N>
struct FixedString {
  char value[N]{};

  constexpr FixedString(const char (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) value[i] = str[i];
  }

  constexpr operator const char*() const { return value; }
};

template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

enum class AddressType : std::uint32_t {
  kPattern,
  kInterface,
  kConvar,
  kModuleBase,
  kModuleExport
};

struct AddressDataT {
  AddressType type;
  const char* data;
};

union Address {
  AddressDataT data;
  struct {
    volatile std::uintptr_t encoded;
    volatile std::uint64_t key;
  };

  constexpr Address(std::uintptr_t addr = 0) : encoded(addr), key(0) {}
  constexpr Address(AddressType type, const char* str) : data{type, str} {}
};

__forceinline std::uintptr_t Decode(const Address& e) noexcept {
  return static_cast<std::uintptr_t>(e.encoded ^ e.key);
}

template <std::uint32_t Hash, AddressType Type, FixedString Str>
struct AddressHolder {
  [[gnu::section("_addr"), gnu::used, gnu::retain]]
  inline static constexpr Address kEntry{Type, Str.value};
};

struct SentinelHolder {
  [[gnu::section("_addr"), gnu::used, gnu::retain]]
  inline static constexpr Address kEntry{};
};

}  // namespace protection::addresses

#define ADDRESS_IMPL(hash_value, addr_type, str_value) \
  (::protection::addresses ::AddressHolder<            \
      hash_value, addr_type,                           \
      ::protection::addresses ::FixedString{str_value}>::kEntry)

#ifndef PATTERN
#define PATTERN(entry)                                                 \
  ([]() -> std::uintptr_t {                                            \
    static const auto val = memory::ResolvePattern((entry).data.data); \
    return val;                                                        \
  }())
#endif

#ifndef INTERFACE_
#define INTERFACE_(str)                                      \
  []() -> std::uintptr_t {                                   \
    static const auto val = memory::GetModuleInterface(str); \
    return val;                                              \
  }()
#endif

#ifndef CONVAR
#define CONVAR(str)                                                         \
  []() -> CConvar* {                                                        \
    static const auto val =                                                 \
        addresses::globals::cvar->Find(::protection::addresses::Hash(str)); \
    return val;                                                             \
  }()
#endif

#ifndef MODULE_BASE
#define MODULE_BASE(str)                                \
  []() -> std::uintptr_t {                              \
    static const auto val = memory::GetModuleBase(str); \
    return val;                                         \
  }()
#endif

#ifndef MODULE_EXPORT
#define MODULE_EXPORT(str)                \
  ([]() -> std::uintptr_t {               \
    static std::uintptr_t val{};          \
    if (!val) {                           \
      val = memory::GetModuleExport(str); \
    }                                     \
    return val;                           \
  }())
#endif

#include "protection/patterns.h"

#endif  // VELOCITY_PROTECTION_GAME_ADDRESSES_H_