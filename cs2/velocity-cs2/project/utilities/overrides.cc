#include "platform/platform.h"

import <cstddef>;
import <string_view>;
import <type_traits>;

#include "protection/address_macros.h"

import velocity;

__forceinline void* GameAlloc(std::size_t size) {
  const auto memalloc =
      *reinterpret_cast<void**>(MODULE_EXPORT("tier0.dll:g_pMemAlloc"));
  const auto vtable = *reinterpret_cast<void***>(memalloc);
  return reinterpret_cast<void*(__thiscall*)(void*, std::size_t)>(vtable[1])(
      memalloc, size);
}

__forceinline void GameFree(void* ptr) {
  const auto memalloc =
      *reinterpret_cast<void**>(MODULE_EXPORT("tier0.dll:g_pMemAlloc"));
  const auto vtable = *reinterpret_cast<void***>(memalloc);
  reinterpret_cast<void(__thiscall*)(void*, void*)>(vtable[3])(memalloc, ptr);
}

void* __cdecl operator new(std::size_t size) { return GameAlloc(size); }

void* __cdecl operator new[](std::size_t size) { return GameAlloc(size); }

void __cdecl operator delete(void* ptr) noexcept { GameFree(ptr); }

void __cdecl operator delete[](void* ptr) noexcept { GameFree(ptr); }

void __cdecl operator delete(void* ptr, std::size_t) noexcept { GameFree(ptr); }

void __cdecl operator delete[](void* ptr, std::size_t) noexcept {
  GameFree(ptr);
}
