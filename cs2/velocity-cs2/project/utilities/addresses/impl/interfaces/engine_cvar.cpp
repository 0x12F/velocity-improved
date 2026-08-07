#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../../interfaces.hpp"

namespace interfaces {

	namespace {

		constexpr std::uint16_t invalid_index = static_cast<std::uint16_t>( -1 );
		constexpr std::uint16_t capacity_mask = 0x7FFF;

		[[nodiscard]] bool try_hash_name_impl(
			const char* name,
			std::uint32_t& hash )
		{
			if ( !name )
				return false;

			__try
			{
				hash = fnv1a::runtime_hash( name );
				return true;
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

		[[nodiscard]] bool try_hash_name( const char* name, std::uint32_t& hash )
		{
			diag::probe_scope probe;
			return try_hash_name_impl( name, hash );
		}

	} // namespace

	c_convar* c_engine_cvar::find(std::uint32_t name_hash)
	{
		const auto capacity = static_cast<std::uint16_t>( m_allocation_count & capacity_mask );
		if ( m_head != invalid_index && ( !m_container || capacity == 0 ) )
			return 0;

		auto current = m_head;
		for ( std::size_t visited = 0; current != invalid_index && visited < capacity; ++visited )
		{
			if ( current >= capacity )
				return 0;

			const auto entry = memory::safe_read<cvar_container_t>(
				reinterpret_cast<std::uintptr_t>( m_container ) +
				current * sizeof( cvar_container_t ) );
			if ( !entry )
				return 0;

			if ( entry->m_cvar )
			{
				const auto name = memory::safe_read<const char*>(
					reinterpret_cast<std::uintptr_t>( entry->m_cvar ) +
						offsetof( c_convar, m_name ) );
				std::uint32_t hash{};
				if ( name && try_hash_name( *name, hash ) && hash == name_hash )
					return entry->m_cvar;
			}

			current = entry->m_next_index;
		}

		return 0;
	}

	bool c_engine_cvar::unlock_all()
	{
		constexpr std::uint64_t FCVAR_DEVELOPMENTONLY = 1ull << 1;
		constexpr std::uint64_t FCVAR_HIDDEN = 1ull << 4;
		constexpr std::uint64_t FCVAR_REGISTRY_RESTRICTED = 1ull << 10;
		constexpr auto restriction_mask =
			FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN | FCVAR_REGISTRY_RESTRICTED;

		const auto capacity = static_cast<std::uint16_t>( m_allocation_count & capacity_mask );
		if ( m_head != invalid_index && ( !m_container || capacity == 0 ) )
			return false;

		auto current = m_head;
		std::size_t visited = 0;
		for ( ; current != invalid_index && visited < capacity; ++visited )
		{
			if ( current >= capacity )
				return false;

			const auto entry = memory::safe_read<cvar_container_t>(
				reinterpret_cast<std::uintptr_t>( m_container ) +
					current * sizeof( cvar_container_t ) );
			if ( !entry )
				return false;

			if ( entry->m_cvar )
			{
				const auto flags_address =
					reinterpret_cast<std::uintptr_t>( entry->m_cvar ) +
					offsetof( c_convar, m_flags );
				const auto flags = memory::safe_read<std::uint64_t>( flags_address );
				if ( !flags ||
					!memory::safe_write( flags_address, *flags & ~restriction_mask ) )
					return false;
			}

			current = entry->m_next_index;
		}

		return current == invalid_index;
	}

} // namespace interfaces
