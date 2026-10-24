module;

#include "platform/platform.h"

export module velocity:utilities.hooking.hooking;

import <cstddef>;
import <cstdint>;
import <initializer_list>;
import <type_traits>;

export {
namespace hooking {

class Jmp {
 public:
  bool Create(void* target, void* hook_function);
  bool Enable();
  bool Disable();
  void Reset();

  template <typename T, typename... ArgsT>
  T Call(ArgsT... args) const {
    if (!this->m_trampoline_) [[unlikely]] {
      if constexpr (std::is_same_v<T, void>)
        return;
      else
        return T{};
    }

    return reinterpret_cast<T (*)(ArgsT...)>(this->m_trampoline_)(args...);
  }

  template <typename T>
  T Original() const {
    return reinterpret_cast<T>(this->m_trampoline_);
  }

  explicit operator bool() const { return this->IsValid(); }
  bool IsValid() const { return this->m_target_ && this->m_trampoline_; }
  bool IsEnabled() const { return this->m_enabled_; }
  void* GetTarget() const { return this->m_target_; }
  void* GetTrampoline() const { return this->m_trampoline_; }
  const std::uint8_t* GetOriginalBytes() const {
    return this->m_original_bytes_;
  }
  std::size_t GetOriginalLength() const { return this->m_original_length_; }

 private:
  void* m_target_{};
  void* m_hook_{};
  void* m_trampoline_{};
  std::uint8_t m_original_bytes_[32]{};
  std::uint8_t m_hook_bytes_[16]{};
  std::size_t m_original_length_{};
  std::size_t m_patch_size_{};
  bool m_enabled_{};
};

namespace allocator {

void* Allocate(std::size_t size, void* near_address);
void Free(void* address);

}  // namespace allocator

namespace manager {

struct Entry {
  Jmp* hook;
  void* detour;
  const char* name;
  std::uintptr_t address;
};

bool Create(const std::initializer_list<Entry>& entries);

}  // namespace manager

}  // namespace hooking
}  // export
