#pragma once

#include <utilities/diag.hpp>

namespace memory {

	namespace detail {

		template <typename T>
		[[nodiscard]] inline std::optional<T> safe_read_impl( std::uintptr_t address )
		{
			__try
			{
				return *reinterpret_cast<T*>( address );
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return std::nullopt;
			}
		}

		template <typename T>
		[[nodiscard]] inline bool safe_write_impl(
			std::uintptr_t address,
			const T& value )
		{
			__try
			{
				*reinterpret_cast<T*>( address ) = value;
				return true;
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

	} // namespace detail

	[[nodiscard]] std::uintptr_t get_module_base( std::string_view module_name );
	[[nodiscard]] std::uintptr_t get_module_export( std::string_view export_name );
	[[nodiscard]] std::uintptr_t get_module_interface( std::string_view interface_name );
	[[nodiscard]] std::uintptr_t get_module_export_with_base (std::uintptr_t module_base, std::string_view export_name);
	[[nodiscard]] std::uintptr_t resolve_pattern( std::string_view pattern );

	[[nodiscard]] std::uintptr_t get_module_export( std::uintptr_t module_base, std::uint16_t ordinal );
	[[nodiscard]] std::uintptr_t get_module_size( std::uintptr_t module_base );
	
	[[nodiscard]] std::uintptr_t find_vtable_by_rtti( std::uintptr_t module_base, std::string_view class_name );
	[[nodiscard]] std::uintptr_t find_instance_by_rtti( std::uintptr_t module_base, std::string_view class_name );
	[[nodiscard]] std::uintptr_t find_global_instance_by_vtable( std::uintptr_t module_base, std::uintptr_t vtable_address );

	template <typename T>
	[[nodiscard]] inline T read( std::uintptr_t address )
	{
		return *reinterpret_cast< T* >( address );
	}

	template <typename T>
	[[nodiscard]] inline std::optional<T> safe_read( std::uintptr_t address )
	{
		diag::probe_scope probe;
		return detail::safe_read_impl<T>( address );
	}

	template <typename T>
	inline void write( std::uintptr_t address, const T& value )
	{
		*reinterpret_cast< T* >( address ) = value;
	}

	template <typename T>
	[[nodiscard]] inline bool safe_write( std::uintptr_t address, const T& value )
	{
		diag::probe_scope probe;
		return detail::safe_write_impl( address, value );
	}

	template <typename T, typename... args_t>
	inline T call( std::uintptr_t address, args_t... args )
	{
		if ( !address )
		{
			if constexpr ( std::is_void_v<T> )
				return;
			else
				return T{};
		}

		return reinterpret_cast< T( __fastcall* )( args_t... ) >( address )( args... );
	}

	template <typename T, typename... args_t>
	inline T call_vfunc( std::uintptr_t instance, std::size_t index, args_t... args )
	{
		const auto fail = []() -> T
		{
			if constexpr ( std::is_void_v<T> )
				return;
			else
				return T{};
		};

		if ( !instance )
			return fail();

		const auto vtable = safe_read<std::uintptr_t>( instance );
		if ( !vtable || !*vtable )
			return fail();

		const auto func = safe_read<std::uintptr_t>(
			*vtable + index * sizeof( std::uintptr_t ) );
		if ( !func || !*func )
			return fail();

		return reinterpret_cast< T( __fastcall* )( std::uintptr_t, args_t... ) >(
			*func )( instance, args... );
	}

	[[nodiscard]] std::uintptr_t get_vfunc( std::uintptr_t instance, std::size_t index );
	[[nodiscard]] std::string read_string( std::uintptr_t address, std::size_t max_length = 256 );

} // namespace memory
