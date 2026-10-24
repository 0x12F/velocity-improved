module;

#include "platform/platform.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace memory {

namespace detail {

struct PatternByteT {
  std::uint8_t byte;
  bool wildcard;
};

[[nodiscard]] static int HexCharToInt(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

enum class ResolveOp : std::uint8_t {
  kDirect,
  kRelCall,
  kRipRelative,
  kAbsolutePtr
};

struct ParsedT {
  std::size_t byte_count{};
  ResolveOp op{ResolveOp::kDirect};
  std::size_t op_byte_offset{};
  std::int64_t post_offset{};
  bool deref_final{};
};

[[nodiscard]] static ParsedT ParsePattern(std::string_view pattern,
                                          std::span<PatternByteT> out_bytes) {
  ParsedT result;

  if (pattern.empty() || out_bytes.empty()) {
    return result;
  }

  auto count{0ull};
  auto idx{0ull};

  while (idx < pattern.length() && count < out_bytes.size()) {
    const auto c = pattern[idx];

    if (c == ' ' || c == '\t') {
      idx++;
      continue;
    }

    if (c == '>') {
      result.op = ResolveOp::kRelCall;
      result.op_byte_offset = count;
      idx++;
      continue;
    }

    if (c == '*') {
      result.op = ResolveOp::kRipRelative;
      result.op_byte_offset = count;
      idx++;
      continue;
    }

    if (c == '^') {
      result.op = ResolveOp::kAbsolutePtr;
      result.op_byte_offset = count;
      idx++;
      continue;
    }

    if (c == '~') {
      result.deref_final = true;
      idx++;
      continue;
    }

    if (c == '+' || c == '-') {
      const auto negate = (c == '-');
      idx++;
      auto val{0ll};

      while (idx < pattern.length()) {
        const auto h = HexCharToInt(pattern[idx]);
        if (h < 0) {
          break;
        }

        val = (val << 4) | h;
        idx++;
      }

      result.post_offset = negate ? -val : val;
      continue;
    }

    if (c == '?') {
      out_bytes[count++] = {.byte = 0, .wildcard = true};
      idx++;

      if (idx < pattern.length() && pattern[idx] == '?') {
        idx++;
      }

      continue;
    }

    const auto high = HexCharToInt(pattern[idx++]);

    if (idx < pattern.length()) {
      const auto low = HexCharToInt(pattern[idx]);

      if (low != -1) {
        out_bytes[count++] = {
            .byte = static_cast<std::uint8_t>((high << 4) | low),
            .wildcard = false};
        idx++;
      } else {
        out_bytes[count++] = {.byte = static_cast<std::uint8_t>(high),
                              .wildcard = false};
      }
    } else {
      out_bytes[count++] = {.byte = static_cast<std::uint8_t>(high),
                            .wildcard = false};
    }
  }

  result.byte_count = count;
  return result;
}

struct QualifiedNameT {
  std::string_view module{};
  std::string_view symbol{};
  bool has_module_prefix{};
};

[[nodiscard]] static QualifiedNameT SplitQualifiedName(
    std::string_view qualified) {
  const auto colon = qualified.find(':');
  if (colon == std::string_view::npos) {
    return {.symbol = qualified, .has_module_prefix = false};
  }

  return {
      .module = qualified.substr(0, colon),
      .symbol = qualified.substr(colon + 1),
      .has_module_prefix = true,
  };
}

}  // namespace detail

std::uintptr_t GetModuleBase(std::string_view module_name) {
  return reinterpret_cast<std::uintptr_t>(
      GetModuleHandleA(std::string(module_name).c_str()));
}

std::uintptr_t GetModuleExport(std::string_view export_name) {
  const auto qualified = detail::SplitQualifiedName(export_name);
  if (qualified.has_module_prefix) {
    const auto module_base = GetModuleBase(qualified.module);
    if (!module_base) {
      logging::console::Print(xs("[error] module not found | module: {}"),
                              qualified.module);
      return 0;
    }

    const auto result = GetModuleExportWithBase(module_base, qualified.symbol);
    if (!result) {
      logging::console::Print(xs("[error] export not found | {}:{}"),
                              qualified.module, qualified.symbol);
    }

    return result;
  }

  static constexpr std::string_view kNownModules[] = {
      "client.dll",
      "engine2.dll",
      "server.dll",
      "scenesystem.dll",
      "materialsystem2.dll",
      "rendersystemdx11.dll",
      "panorama.dll",
      "gameoverlayrenderer64.dll",
      "schemasystem.dll",
      "inputsystem.dll",
      "soundsystem.dll",
      "tier0.dll",
      "particles.dll",
      "resourcesystem.dll",
      "localize.dll",
      "meshsystem.dll",
      "filesystem_stdio.dll",
      "vphysics2.dll",
      "ntdll.dll",
      "steam_api64.dll",
  };

  for (const auto& module_name : kNownModules) {
    const auto module_base = GetModuleBase(module_name);
    if (!module_base) continue;

    const auto result = GetProcAddress(reinterpret_cast<HMODULE>(module_base),
                                       std::string(export_name).c_str());
    if (result) return reinterpret_cast<std::uintptr_t>(result);
  }

  logging::console::Print(xs("[error] export not found | {}"), export_name);
  return 0;
}

std::uintptr_t GetModuleExportWithBase(std::uintptr_t module_base,
                                       std::string_view export_name) {
  return reinterpret_cast<std::uintptr_t>(
      GetProcAddress(reinterpret_cast<HMODULE>(module_base),
                     std::string(export_name).c_str()));
}

std::uintptr_t GetModuleInterface(std::string_view interface_name) {
  const auto qualified = detail::SplitQualifiedName(interface_name);
  const auto lookup_name =
      qualified.has_module_prefix ? qualified.symbol : interface_name;

  if (qualified.has_module_prefix) {
    const auto module_base = GetModuleBase(qualified.module);
    if (!module_base) {
      logging::console::Print(xs("[error] module not found | module: {}"),
                              qualified.module);
      return 0;
    }

    const auto create_interface =
        GetModuleExportWithBase(module_base, xs("CreateInterface"));
    if (!create_interface) {
      logging::console::Print(
          xs("[error] CreateInterface not found | module: {}"),
          qualified.module);
      return 0;
    }

    struct InterfaceRegT {
      std::uintptr_t (*create_fn)();
      const char* name;
      InterfaceRegT* next;
    };

    const auto interface_list_offset =
        *reinterpret_cast<std::int32_t*>(create_interface + 3);
    const auto interface_regs = *reinterpret_cast<InterfaceRegT**>(
        create_interface + 7 + interface_list_offset);

    for (auto current = interface_regs; current; current = current->next) {
      if (!current->name || !current->create_fn) {
        continue;
      }

      if (std::string_view(current->name) == lookup_name) {
        return current->create_fn();
      }
    }

    logging::console::Print(xs("[error] interface not found | {}:{}"),
                            qualified.module, lookup_name);
    return 0;
  }

  static constexpr std::string_view kNownModules[] = {
      "client.dll",           "engine2.dll",
      "server.dll",           "scenesystem.dll",
      "materialsystem2.dll",  "rendersystemdx11.dll",
      "panorama.dll",         "gameoverlayrenderer64.dll",
      "schemasystem.dll",     "inputsystem.dll",
      "soundsystem.dll",      "tier0.dll",
      "particles.dll",        "resourcesystem.dll",
      "localize.dll",         "meshsystem.dll",
      "filesystem_stdio.dll", "vphysics2.dll",
  };

  struct InterfaceRegT {
    std::uintptr_t (*create_fn)();
    const char* name;
    InterfaceRegT* next;
  };

  for (const auto& module_name : kNownModules) {
    const auto module_base = GetModuleBase(module_name);
    if (!module_base) continue;

    const auto create_interface =
        GetModuleExportWithBase(module_base, xs("CreateInterface"));
    if (!create_interface) continue;

    const auto interface_list_offset =
        *reinterpret_cast<std::int32_t*>(create_interface + 3);
    const auto interface_regs = *reinterpret_cast<InterfaceRegT**>(
        create_interface + 7 + interface_list_offset);

    for (auto current = interface_regs; current; current = current->next) {
      if (!current->name || !current->create_fn) continue;

      if (std::string_view(current->name) == lookup_name)
        return current->create_fn();
    }
  }

  logging::console::Print(xs("[error] interface not found | {}"), lookup_name);
  return 0;
}

std::uintptr_t ResolvePattern(std::string_view pattern) {
  // parse "module.dll:pattern" format
  const auto colon = pattern.find(':');
  if (colon == std::string_view::npos) {
    logging::console::Print(
        xs("[error] pattern missing module prefix | pattern: {}"), pattern);
    return 0;
  }

  const auto module_name = pattern.substr(0, colon);
  const auto pattern_str = pattern.substr(colon + 1);

  const auto module_base = GetModuleBase(module_name);
  if (!module_base) {
    logging::console::Print(xs("[error] module not found | module: {}"),
                            module_name);
    return 0;
  }

  // rest of function unchanged, just swap pattern -> pattern_str and
  // module_base is now local
  const auto module_size = GetModuleSize(module_base);
  if (!module_size) {
    logging::console::Print(xs("[error] invalid module size | pattern: {}"),
                            pattern_str);
    return 0;
  }

  std::array<detail::PatternByteT, 128> parsed{};
  const auto info = detail::ParsePattern(pattern_str, parsed);

  if (info.byte_count == 0) {
    logging::console::Print(xs("[error] failed to parse pattern | pattern: {}"),
                            pattern_str);
    return 0;
  }

  const auto pattern_length = info.byte_count;
  auto first_byte_idx{-1};

  for (auto i = 0ull; i < pattern_length; i++) {
    if (!parsed[i].wildcard) {
      first_byte_idx = static_cast<int>(i);
      break;
    }
  }

  if (first_byte_idx == -1) {
    logging::console::Print(xs("[error] no first byte | pattern: {}"),
                            pattern_str);
    return 0;
  }

  const auto first_byte = parsed[first_byte_idx].byte;
  const auto first_byte_vec = _mm256_set1_epi8(static_cast<char>(first_byte));

  const auto module_end = module_base + module_size;
  const auto scan_end = (module_size >= 32) ? module_size - 32 : 0;

  auto match{0ull};

  for (auto i = 0ull; i <= scan_end; i += 32) {
    const auto memory_chunk =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(module_base + i));
    const auto comparison = _mm256_cmpeq_epi8(memory_chunk, first_byte_vec);
    auto mask = _mm256_movemask_epi8(comparison);

    while (mask != 0) {
      auto match_offset{0ul};
      _BitScanForward(&match_offset, mask);

      const auto candidate_offset =
          static_cast<std::int64_t>(i) + match_offset - first_byte_idx;
      if (candidate_offset < 0) {
        mask &= mask - 1;
        continue;
      }

      const auto potential_start =
          module_base + static_cast<std::uint64_t>(candidate_offset);
      if (potential_start + pattern_length > module_end) {
        mask &= mask - 1;
        continue;
      }

      auto found{true};

      for (auto j = 0ull; j < pattern_length; j++) {
        if (parsed[j].wildcard) continue;
        if (*reinterpret_cast<std::uint8_t*>(potential_start + j) !=
            parsed[j].byte) {
          found = false;
          break;
        }
      }

      if (found) {
        match = potential_start;
        goto done_scanning;
      }

      mask &= mask - 1;
    }
  }

done_scanning:
  if (!match) {
    logging::console::Print(xs("[error] pattern not found | {}"), pattern_str);
    return 0;
  }

  auto result = match;

  switch (info.op) {
    case detail::ResolveOp::kDirect:
      break;

    case detail::ResolveOp::kRelCall: {
      const auto operand_addr = match + info.op_byte_offset + 1;
      if (operand_addr + 4 > module_end) return 0;
      const auto rel = *reinterpret_cast<const std::int32_t*>(operand_addr);
      result = operand_addr + 4 + rel;
      break;
    }

    case detail::ResolveOp::kRipRelative: {
      const auto operand_addr = match + info.op_byte_offset;
      if (operand_addr + 4 > module_end) return 0;
      const auto rel = *reinterpret_cast<const std::int32_t*>(operand_addr);
      result = operand_addr + 4 + rel;
      break;
    }

    case detail::ResolveOp::kAbsolutePtr: {
      const auto ptr_addr = match + info.op_byte_offset;
      if (ptr_addr + sizeof(std::uintptr_t) > module_end) return 0;
      result = *reinterpret_cast<const std::uintptr_t*>(ptr_addr);
      break;
    }
  }

  result += info.post_offset;

  if (info.deref_final) {
    if (result + sizeof(std::uintptr_t) > module_end || result < module_base)
      return 0;
    result = *reinterpret_cast<const std::uintptr_t*>(result);
  }

  return result;
}

std::uintptr_t GetModuleExport(std::uintptr_t module_base,
                               std::uint16_t ordinal) {
  return reinterpret_cast<std::uintptr_t>(GetProcAddress(
      reinterpret_cast<HMODULE>(module_base), MAKEINTRESOURCEA(ordinal)));
}

std::uintptr_t GetModuleSize(std::uintptr_t module_base) {
  const auto dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module_base);
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return 0;
  }

  const auto nt_headers =
      reinterpret_cast<IMAGE_NT_HEADERS*>(module_base + dos_header->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return 0;
  }

  return nt_headers->OptionalHeader.SizeOfImage;
}

std::uintptr_t FindVtableByRtti(std::uintptr_t module_base,
                                std::string_view class_name) {
  const auto dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module_base);
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return 0;
  }

  const auto nt_headers =
      reinterpret_cast<IMAGE_NT_HEADERS*>(module_base + dos_header->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return 0;
  }

  auto type_descriptor_name{".?AV" + std::string(class_name) + "@@"};
  auto type_descriptor_addr{0ull};

  for (auto i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
    const auto section = IMAGE_FIRST_SECTION(nt_headers) + i;

    if ((section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) &&
        (section->Characteristics & IMAGE_SCN_MEM_READ)) {
      const auto section_start = module_base + section->VirtualAddress;
      const auto section_end = section_start + section->Misc.VirtualSize -
                               type_descriptor_name.length();

      for (auto current = section_start; current < section_end; current++) {
        if (std::memcmp(reinterpret_cast<const void*>(current),
                        type_descriptor_name.data(),
                        type_descriptor_name.length() + 1) == 0) {
          type_descriptor_addr = current - 0x10;
          goto found_type_descriptor;
        }
      }
    }
  }

found_type_descriptor:
  if (!type_descriptor_addr) {
    return 0;
  }

  const auto type_descriptor_rva =
      static_cast<std::uint32_t>(type_descriptor_addr - module_base);

  for (auto i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
    const auto section = IMAGE_FIRST_SECTION(nt_headers) + i;

    if (std::string_view(reinterpret_cast<const char*>(section->Name), 8)
            .find(".rdata") != std::string_view::npos) {
      const auto section_start = module_base + section->VirtualAddress;
      const auto section_end = section_start + section->Misc.VirtualSize - 0x30;

      for (auto current = section_start; current < section_end; current += 8) {
        const auto potential_col = reinterpret_cast<std::uint32_t*>(current);

        if (potential_col[3] == type_descriptor_rva) {
          for (auto vt_search = section_start; vt_search < section_end;
               vt_search += 8) {
            const auto ptr_val = *reinterpret_cast<std::uintptr_t*>(vt_search);

            if (ptr_val == current) {
              return vt_search + 8;
            }
          }
        }
      }
    }
  }

  return 0;
}

std::uintptr_t FindInstanceByRtti(std::uintptr_t module_base,
                                  std::string_view class_name) {
  const auto vtable = FindVtableByRtti(module_base, class_name);
  if (!vtable) {
    return 0;
  }

  return FindGlobalInstanceByVtable(module_base, vtable);
}

std::uintptr_t FindGlobalInstanceByVtable(std::uintptr_t module_base,
                                          std::uintptr_t vtable_address) {
  const auto dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module_base);
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return 0;
  }

  const auto nt_headers =
      reinterpret_cast<IMAGE_NT_HEADERS*>(module_base + dos_header->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return 0;
  }

  for (auto i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
    const auto section = IMAGE_FIRST_SECTION(nt_headers) + i;
    std::string_view section_name(reinterpret_cast<const char*>(section->Name),
                                  8);

    if (section_name.find(".data") != std::string_view::npos ||
        section_name.find(".bss") != std::string_view::npos) {
      const auto section_start = module_base + section->VirtualAddress;
      const auto section_end = section_start + section->Misc.VirtualSize - 8;

      for (auto current = section_start; current < section_end; current += 8) {
        const auto ptr_val = *reinterpret_cast<std::uintptr_t*>(current);

        if (ptr_val == vtable_address) {
          return current;
        }
      }
    }
  }

  return 0;
}

std::uintptr_t GetVfunc(std::uintptr_t instance, std::size_t index) {
  const auto vtable = *reinterpret_cast<std::uintptr_t*>(instance);
  if (!vtable) {
    return 0;
  }

  return *reinterpret_cast<std::uintptr_t*>(vtable +
                                            index * sizeof(std::uintptr_t));
}

std::string ReadString(std::uintptr_t address, std::size_t max_length) {
  if (!address || max_length == 0) {
    return {};
  }

  std::string result;
  for (std::size_t offset = 0; offset < max_length; ++offset) {
    if (address + offset < address) {
      return {};
    }
    const auto character = SafeRead<char>(address + offset);
    if (!character) {
      return {};
    }
    if (*character == '\0') {
      break;
    }
    result.push_back(*character);
  }

  return result;
}

}  // namespace memory
