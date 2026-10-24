module;

#include "platform/platform.h"

export module velocity:utilities.memory.memory;

import <cstddef>;
import <cstdint>;
import <optional>;
import <string>;
import <string_view>;
import <type_traits>;

export import :utilities.diag;

export {
namespace memory {

[[nodiscard]] void* FindNonvirtualBase(void* object, const char* type_name);

namespace detail {

template <typename T>
[[nodiscard]] inline std::optional<T> SafeReadImpl(std::uintptr_t address) {
  __try {
    return *reinterpret_cast<T*>(address);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return std::nullopt;
  }
}

template <typename T>
[[nodiscard]] inline bool SafeWriteImpl(std::uintptr_t address,
                                        const T& value) {
  __try {
    *reinterpret_cast<T*>(address) = value;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

}  // namespace detail

[[nodiscard]] std::uintptr_t GetModuleBase(std::string_view module_name);
[[nodiscard]] std::uintptr_t GetModuleExport(std::string_view export_name);
[[nodiscard]] std::uintptr_t GetModuleInterface(
    std::string_view interface_name);
[[nodiscard]] std::uintptr_t GetModuleExportWithBase(
    std::uintptr_t module_base, std::string_view export_name);
[[nodiscard]] std::uintptr_t ResolvePattern(std::string_view pattern);

[[nodiscard]] std::uintptr_t GetModuleExport(std::uintptr_t module_base,
                                             std::uint16_t ordinal);
[[nodiscard]] std::uintptr_t GetModuleSize(std::uintptr_t module_base);

[[nodiscard]] std::uintptr_t FindVtableByRtti(std::uintptr_t module_base,
                                              std::string_view class_name);
[[nodiscard]] std::uintptr_t FindInstanceByRtti(std::uintptr_t module_base,
                                                std::string_view class_name);
[[nodiscard]] std::uintptr_t FindGlobalInstanceByVtable(
    std::uintptr_t module_base, std::uintptr_t vtable_address);

template <typename T>
[[nodiscard]] inline T Read(std::uintptr_t address) {
  return *reinterpret_cast<T*>(address);
}

template <typename T>
[[nodiscard]] inline std::optional<T> SafeRead(std::uintptr_t address) {
  diag::ProbeScope probe;
  return detail::SafeReadImpl<T>(address);
}

template <typename T>
inline void Write(std::uintptr_t address, const T& value) {
  *reinterpret_cast<T*>(address) = value;
}

template <typename T>
[[nodiscard]] inline bool SafeWrite(std::uintptr_t address, const T& value) {
  diag::ProbeScope probe;
  return detail::SafeWriteImpl(address, value);
}

template <typename T, typename... ArgsT>
inline T Call(std::uintptr_t address, ArgsT... args) {
  if (!address) {
    if constexpr (std::is_void_v<T>)
      return;
    else
      return T{};
  }

  return reinterpret_cast<T(__fastcall*)(ArgsT...)>(address)(args...);
}

template <typename T, typename... ArgsT>
inline T CallVfunc(std::uintptr_t instance, std::size_t index, ArgsT... args) {
  const auto fail = []() -> T {
    if constexpr (std::is_void_v<T>)
      return;
    else
      return T{};
  };

  if (!instance) return fail();

  const auto vtable = SafeRead<std::uintptr_t>(instance);
  if (!vtable || !*vtable) return fail();

  const auto func =
      SafeRead<std::uintptr_t>(*vtable + index * sizeof(std::uintptr_t));
  if (!func || !*func) return fail();

  return reinterpret_cast<T(__fastcall*)(std::uintptr_t, ArgsT...)>(*func)(
      instance, args...);
}

[[nodiscard]] std::uintptr_t GetVfunc(std::uintptr_t instance,
                                      std::size_t index);
[[nodiscard]] std::string ReadString(std::uintptr_t address,
                                     std::size_t max_length = 256);

}  // namespace memory
}  // export
