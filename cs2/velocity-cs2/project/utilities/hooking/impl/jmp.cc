module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <initializer_list>;
import <string_view>;
import <type_traits>;

namespace hooking {

namespace detail {

constexpr auto kMinHookSize{14ull};

static bool Decode(ZydisDecodedInstruction* ix, std::uint8_t* ip) {
  ZydisDecoder decoder{};
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  return ZYAN_SUCCESS(
      ZydisDecoderDecodeInstruction(&decoder, nullptr, ip, 15, ix));
}

static void WriteJmpRel32(void* at, std::intptr_t offset) {
  auto* p = static_cast<std::uint8_t*>(at);
  p[0] = 0xe9;
  *reinterpret_cast<std::int32_t*>(p + 1) = static_cast<std::int32_t>(offset);
}

static void WriteJmpAbs64(void* at, const void* dest) {
  auto* p = static_cast<std::uint8_t*>(at);
  p[0] = 0xff;
  p[1] = 0x25;
  p[2] = 0x00;
  p[3] = 0x00;
  p[4] = 0x00;
  p[5] = 0x00;
  *reinterpret_cast<std::uint64_t*>(p + 6) =
      reinterpret_cast<std::uint64_t>(dest);
}

static std::size_t WriteJmpAuto(void* at, const void* src_next,
                                const void* dest) {
  const auto rel = reinterpret_cast<std::intptr_t>(dest) -
                   reinterpret_cast<std::intptr_t>(src_next);

  if (rel >= INT32_MIN && rel <= INT32_MAX) {
    WriteJmpRel32(at, rel);
    return 5;
  }

  WriteJmpAbs64(at, dest);
  return 14;
}

static bool BuildTrampoline(void* target, void* trampoline,
                            std::size_t* out_original_len,
                            std::size_t* out_trampoline_len) {
  auto src = static_cast<std::uint8_t*>(target);
  auto dst = static_cast<std::uint8_t*>(trampoline);

  auto src_offset{0ull};
  auto dst_offset{0ull};
  auto total_copied{0ull};

  ZydisDecodedInstruction ix{};

  while (total_copied < kMinHookSize) {
    if (!Decode(&ix, src + src_offset)) {
      return false;
    }

    const auto instr_len = ix.length;
    auto ip = src + src_offset;
    auto tr = dst + dst_offset;

    if (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
      if (ix.raw.disp.size == 32) {
        std::memcpy(tr, ip, instr_len);
        auto target_addr =
            ip + instr_len + static_cast<std::int32_t>(ix.raw.disp.value);
        const auto new_disp = target_addr - (tr + instr_len);
        *reinterpret_cast<std::int32_t*>(tr + ix.raw.disp.offset) =
            static_cast<std::int32_t>(new_disp);
        dst_offset += instr_len;
      } else if (ix.raw.imm[0].size == 32) {
        std::memcpy(tr, ip, instr_len);
        auto target_addr =
            ip + instr_len + static_cast<std::int32_t>(ix.raw.imm[0].value.s);
        const auto new_disp = target_addr - (tr + instr_len);
        *reinterpret_cast<std::int32_t*>(tr + ix.raw.imm[0].offset) =
            static_cast<std::int32_t>(new_disp);
        dst_offset += instr_len;
      } else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR &&
                 ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
        auto* target_addr =
            ip + instr_len + static_cast<std::int8_t>(ix.raw.imm[0].value.s);
        const auto new_disp = target_addr - (tr + 6);
        *tr++ = 0x0f;
        *tr++ = static_cast<std::uint8_t>(0x80 + (ix.opcode & 0x0f));
        *reinterpret_cast<std::int32_t*>(tr) =
            static_cast<std::int32_t>(new_disp);
        dst_offset += 6;
      } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR &&
                 ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
        auto* target_addr =
            ip + instr_len + static_cast<std::int8_t>(ix.raw.imm[0].value.s);
        const auto new_disp = target_addr - (tr + 5);
        *tr++ = 0xe9;
        *reinterpret_cast<std::int32_t*>(tr) =
            static_cast<std::int32_t>(new_disp);
        dst_offset += 5;
      } else {
        return false;
      }
    } else {
      std::memcpy(tr, ip, instr_len);
      dst_offset += instr_len;
    }

    src_offset += instr_len;
    total_copied += instr_len;

    if (dst_offset > 48) {
      return false;
    }
  }

  dst_offset +=
      WriteJmpAuto(dst + dst_offset, dst + dst_offset + 5, src + src_offset);

  *out_original_len = src_offset;
  *out_trampoline_len = dst_offset;
  return true;
}

static LONG NtProtectVirtualMemory(HANDLE process, PVOID* base, PSIZE_T size,
                                   ULONG protect, PULONG old_protect) {
  return reinterpret_cast<LONG(__stdcall*)(HANDLE, PVOID*, PSIZE_T, ULONG,
                                           PULONG)>(
      MODULE_EXPORT("ntdll.dll:NtProtectVirtualMemory"))(process, base, size,
                                                         protect, old_protect);
}

static LONG NtQueryVirtualMemory(HANDLE process, PVOID address, int info_class,
                                 PVOID buffer, SIZE_T length,
                                 PSIZE_T return_length) {
  return reinterpret_cast<LONG(__stdcall*)(HANDLE, PVOID, int, PVOID, SIZE_T,
                                           PSIZE_T)>(
      MODULE_EXPORT("ntdll.dll:NtQueryVirtualMemory"))(
      process, address, info_class, buffer, length, return_length);
}

}  // namespace detail

bool Jmp::Create(void* target, void* hook_function) {
  if (this->IsValid()) {
    return true;
  }

  if (!target || !hook_function) {
    return false;
  }

  auto resolved = reinterpret_cast<std::uintptr_t>(target);

  for (auto depth = 0; depth < 8; ++depth) {
    MEMORY_BASIC_INFORMATION resolved_mbi{};
    auto resolved_length{0ull};
    if (detail::NtQueryVirtualMemory(
            GetCurrentProcess(), reinterpret_cast<void*>(resolved), 0,
            &resolved_mbi, sizeof(resolved_mbi), &resolved_length) < 0 ||
        resolved_mbi.State != MEM_COMMIT ||
        !(resolved_mbi.Protect &
          (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
           PAGE_EXECUTE_WRITECOPY))) {
      return false;
    }

    const auto region_end =
        reinterpret_cast<std::uintptr_t>(resolved_mbi.BaseAddress) +
        resolved_mbi.RegionSize;
    if (resolved > region_end || region_end - resolved < 6) {
      return false;
    }

    const auto byte = *reinterpret_cast<std::uint8_t*>(resolved);

    if (byte == 0xe9) {
      const auto rel = *reinterpret_cast<std::int32_t*>(resolved + 1);
      resolved = resolved + 5 + rel;
    } else if (byte == 0xff &&
               *reinterpret_cast<std::uint8_t*>(resolved + 1) == 0x25) {
      const auto ptr =
          resolved + 6 + *reinterpret_cast<std::int32_t*>(resolved + 2);
      resolved = *reinterpret_cast<std::uintptr_t*>(ptr);
    } else {
      break;
    }

    if (depth == 7) {
      return false;
    }
  }

  target = reinterpret_cast<void*>(resolved);

  MEMORY_BASIC_INFORMATION mbi{};
  auto return_length{0ull};

  if (detail::NtQueryVirtualMemory(GetCurrentProcess(), target, 0, &mbi,
                                   sizeof(mbi), &return_length) < 0) {
    return false;
  }

  if (mbi.State != MEM_COMMIT) {
    return false;
  }

  if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
    return false;
  }

  const auto trampoline = allocator::Allocate(64, target);
  if (!trampoline) {
    return false;
  }

  auto original_len{0ull};
  auto trampoline_len{0ull};

  if (!detail::BuildTrampoline(target, trampoline, &original_len,
                               &trampoline_len)) {
    allocator::Free(trampoline);
    return false;
  }

  const auto patch_size = detail::WriteJmpAuto(
      this->m_hook_bytes_, static_cast<std::uint8_t*>(target) + 5,
      hook_function);
  std::memcpy(this->m_original_bytes_, target, original_len);

  this->m_target_ = target;
  this->m_hook_ = hook_function;
  this->m_trampoline_ = trampoline;
  this->m_original_length_ = original_len;
  this->m_patch_size_ = patch_size;
  this->m_enabled_ = false;

  return true;
}

bool Jmp::Enable() {
  if (!this->IsValid()) {
    return false;
  }

  if (this->m_enabled_) {
    return true;
  }

  auto base = this->m_target_;
  auto size = this->m_patch_size_;
  auto old_protect{0ul};

  if (detail::NtProtectVirtualMemory(GetCurrentProcess(), &base, &size,
                                     PAGE_EXECUTE_READWRITE,
                                     &old_protect) < 0) {
    return false;
  }

  std::memcpy(this->m_target_, this->m_hook_bytes_, this->m_patch_size_);

  detail::NtProtectVirtualMemory(GetCurrentProcess(), &base, &size, old_protect,
                                 &old_protect);

  this->m_enabled_ = true;
  return true;
}

bool Jmp::Disable() {
  if (!this->IsValid()) {
    return false;
  }

  if (!this->m_enabled_) {
    return true;
  }

  auto base = this->m_target_;
  auto size = this->m_original_length_;
  auto old_protect{0ul};

  if (detail::NtProtectVirtualMemory(GetCurrentProcess(), &base, &size,
                                     PAGE_EXECUTE_READWRITE,
                                     &old_protect) < 0) {
    return false;
  }

  std::memcpy(this->m_target_, this->m_original_bytes_,
              this->m_original_length_);

  detail::NtProtectVirtualMemory(GetCurrentProcess(), &base, &size, old_protect,
                                 &old_protect);

  this->m_enabled_ = false;
  return true;
}

void Jmp::Reset() {
  if (!this->IsValid()) {
    return;
  }

  this->Disable();

  if (this->m_trampoline_) {
    allocator::Free(this->m_trampoline_);
  }

  this->m_target_ = nullptr;
  this->m_hook_ = nullptr;
  this->m_trampoline_ = nullptr;
  this->m_original_length_ = 0;
  this->m_patch_size_ = 0;
  this->m_enabled_ = false;

  std::memset(this->m_original_bytes_, 0, sizeof(this->m_original_bytes_));
  std::memset(this->m_hook_bytes_, 0, sizeof(this->m_hook_bytes_));
}

}  // namespace hooking
