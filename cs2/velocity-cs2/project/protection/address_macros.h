#ifndef VELOCITY_PROTECTION_ADDRESS_MACROS_H_
#define VELOCITY_PROTECTION_ADDRESS_MACROS_H_

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

#endif  // VELOCITY_PROTECTION_ADDRESS_MACROS_H_
