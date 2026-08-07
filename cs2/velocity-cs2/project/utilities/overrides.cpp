#include <pch/pch.hpp>
#include <protection/game_addresses.hpp>
#include <utilities/memory/memory.hpp>

inline __attribute__ ((always_inline)) void* game_alloc (std::size_t size) {
	const auto memalloc = *reinterpret_cast <void**>(MODULE_EXPORT ("tier0.dll:g_pMemAlloc"));
	const auto vtable = *reinterpret_cast<void***>(memalloc);
	return reinterpret_cast<void* (__thiscall*)(void*, std::size_t)>(vtable [1])(memalloc, size);
}

inline __attribute__ ((always_inline)) void game_free (void* ptr) {
	const auto memalloc = *reinterpret_cast <void**>(MODULE_EXPORT ("tier0.dll:g_pMemAlloc"));
	const auto vtable = *reinterpret_cast<void***>(memalloc);
	reinterpret_cast<void (__thiscall*)(void*, void*)>(vtable [3])(memalloc, ptr);
}

void* __cdecl operator new(std::size_t size) {
	return game_alloc (size);
}

void* __cdecl operator new [] (std::size_t size) {
	return game_alloc (size);
}

void __cdecl operator delete(void* ptr) noexcept {
	game_free (ptr);
}

void __cdecl operator delete [] (void* ptr) noexcept {
	game_free (ptr);
}

void __cdecl operator delete(void* ptr, std::size_t) noexcept {
	game_free (ptr);
}

void __cdecl operator delete [] (void* ptr, std::size_t) noexcept {
	game_free (ptr);
}
