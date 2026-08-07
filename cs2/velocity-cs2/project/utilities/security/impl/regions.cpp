#include <pch/pch.hpp>
#include "../security.hpp"

namespace security::regions {

	namespace detail {

		std::vector<region> regions{};

	} // namespace detail

	void add( std::uintptr_t base, std::size_t size )
	{
		detail::regions.push_back( { base, size } );
	}

	void add_module( HMODULE module )
	{
		const auto base = reinterpret_cast< std::uintptr_t >( module );
		const auto dos = reinterpret_cast< IMAGE_DOS_HEADER* >( base );
		const auto nt = reinterpret_cast< IMAGE_NT_HEADERS64* >( base + dos->e_lfanew );

		add( base, nt->OptionalHeader.SizeOfImage );
	}

	bool is_protected( const void* address, std::size_t size )
	{
		const auto addr = reinterpret_cast< std::uintptr_t >( address );
		const auto end = addr + size;

		for ( const auto& region : detail::regions )
		{
			const auto region_end = region.base + region.size;

			if ( addr < region_end && end > region.base )
			{
				return true;
			}
		}

		return false;
	}

} // namespace security::regions