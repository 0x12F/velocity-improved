#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../hooking.hpp"

namespace hooking::allocator {

	namespace detail {

		static LONG nt_allocate_virtual_memory (HANDLE process, PVOID* base_addr, ULONG_PTR zero_bits, PSIZE_T region_size, ULONG type, ULONG protect) {
			return reinterpret_cast<LONG (__stdcall*)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG)>(MODULE_EXPORT ("ntdll.dll:NtAllocateVirtualMemory")) (process, base_addr, zero_bits, region_size, type, protect);
		}

		static LONG nt_free_virtual_memory (HANDLE process, PVOID* base_addr, PSIZE_T region_size, ULONG free_type) {
			return reinterpret_cast<LONG (__stdcall*)(HANDLE, PVOID*, PSIZE_T, ULONG)>(MODULE_EXPORT ("ntdll.dll:NtFreeVirtualMemory")) (process, base_addr, region_size, free_type);
		}

	} // namespace detail

	void* allocate (std::size_t size, void* near_) {
		const auto target = reinterpret_cast<std::uintptr_t>(near_);
		const auto min_addr = target > 0x7fff0000 ? target - 0x7fff0000 : 0x10000;
		const auto max_addr = target + 0x7fff0000;

		for (auto addr = (target & ~0xffffull) - 0x10000; addr >= min_addr; addr -= 0x10000) {
			auto base = reinterpret_cast<void*>(addr);
			auto region = size;

			if (detail::nt_allocate_virtual_memory (GetCurrentProcess (), &base, 0, &region, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE) >= 0 && base) {
				return base;
			}
		}

		for (auto addr = (target & ~0xffffull) + 0x10000; addr <= max_addr; addr += 0x10000) {
			auto base = reinterpret_cast<void*>(addr);
			auto region = size;

			if (detail::nt_allocate_virtual_memory (GetCurrentProcess (), &base, 0, &region, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE) >= 0 && base) {
				return base;
			}
		}

		return nullptr;
	}

	void free (void* address) {
		if (!address) {
			return;
		}

		auto base = address;
		auto region {0ull};

		detail::nt_free_virtual_memory (GetCurrentProcess (), &base, &region, MEM_RELEASE);
	}

} // namespace hooking::allocator