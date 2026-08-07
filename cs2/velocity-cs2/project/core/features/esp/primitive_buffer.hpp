#pragma once

#include <utilities/memory/memory.hpp>

namespace features::esp::detail {

	inline constexpr std::size_t primitive_size{ 0x70 };
	inline constexpr std::size_t primitive_material_offset{ 0x20 };
	inline constexpr std::size_t primitive_material_copy_offset{ 0x28 };
	inline constexpr std::size_t primitive_color_offset{ 0x50 };
	inline constexpr std::size_t primitive_draw_order_offset{ 0x58 };
	inline constexpr std::size_t primitive_flags_offset{ 0x62 };
	inline constexpr std::uint16_t primitive_draw_last{ 0x8 };

	struct mesh_primitive
	{
		std::byte pad_00[ 0x18 ]{};
		std::uintptr_t scene_object{};
		std::uintptr_t material{};
		std::uintptr_t material_copy{};
		std::byte pad_30[ 0x20 ]{};
		std::uint32_t color{};
		float opacity{};
		std::int32_t draw_order{};
		std::byte pad_5c[ 0x6 ]{};
		std::uint16_t flags{};
		std::uint16_t flags_2{};
		std::byte pad_66[ 0xA ]{};
	};

	static_assert( sizeof( mesh_primitive ) == primitive_size );
	static_assert( offsetof( mesh_primitive, material ) == primitive_material_offset );
	static_assert( offsetof( mesh_primitive, material_copy ) == primitive_material_copy_offset );
	static_assert( offsetof( mesh_primitive, color ) == primitive_color_offset );
	static_assert( offsetof( mesh_primitive, draw_order ) == primitive_draw_order_offset );
	static_assert( offsetof( mesh_primitive, flags ) == primitive_flags_offset );

	// GeneratePrimitives writes to a fixed array and then a separate overflow
	// array. Treating the two as one allocation corrupts renderer memory.
	struct primitive_output_buffer
	{
		std::uintptr_t fixed_data{};
		std::int32_t fixed_capacity{};
		std::int32_t fixed_count{};
		std::int32_t overflow_count{};
		std::uint32_t reserved_14{};
		std::uintptr_t overflow_data{};
		std::int32_t overflow_capacity{};
		std::uint32_t overflow_allocation_flags{};

		[[nodiscard]] int count() const noexcept
		{
			if ( fixed_count < 0 || overflow_count < 0 ||
				fixed_capacity < 0 || overflow_capacity < 0 ||
				fixed_count > fixed_capacity ||
				overflow_count > overflow_capacity ||
				( fixed_count && !fixed_data ) ||
				( overflow_count && !overflow_data ) ) {
				return -1;
			}

			constexpr auto sane_primitive_limit{ 1 << 20 };
			if ( fixed_count > sane_primitive_limit - overflow_count ) {
				return -1;
			}

			return fixed_count + overflow_count;
		}

		[[nodiscard]] std::uintptr_t at( int index ) const noexcept
		{
			const auto total = this->count();
			if ( index < 0 || total < 0 || index >= total ) {
				return 0;
			}

			if ( index < fixed_count ) {
				return fixed_data + static_cast<std::size_t>( index ) * primitive_size;
			}

			return overflow_data +
				static_cast<std::size_t>( index - fixed_count ) * primitive_size;
		}
	};

	static_assert( sizeof( primitive_output_buffer ) == 0x28 );
	static_assert( offsetof( primitive_output_buffer, fixed_count ) == 0xC );
	static_assert( offsetof( primitive_output_buffer, overflow_count ) == 0x10 );
	static_assert( offsetof( primitive_output_buffer, overflow_data ) == 0x18 );

	[[nodiscard]] inline std::optional<primitive_output_buffer> read_primitive_buffer(
		std::uintptr_t address )
	{
		if ( !address ) {
			return std::nullopt;
		}

		const auto result = memory::safe_read<primitive_output_buffer>( address );
		if ( !result || result->count() < 0 ) {
			return std::nullopt;
		}

		return result;
	}

	inline void replace_primitive(
		std::uintptr_t primitive, std::uintptr_t material,
		const xdraw::color& color )
	{
		if ( !primitive || !material ) {
			return;
		}

		(void) memory::safe_write<std::uintptr_t>(
			primitive + primitive_material_offset, material );
		(void) memory::safe_write<std::uintptr_t>(
			primitive + primitive_material_copy_offset, material );
		(void) memory::safe_write<std::uint32_t>(
			primitive + primitive_color_offset, color );
	}

	inline void mark_primitive_last( std::uintptr_t primitive )
	{
		if ( !primitive ) {
			return;
		}

		const auto flags = memory::safe_read<std::uint16_t>(
			primitive + primitive_flags_offset );
		if ( flags ) {
			(void) memory::safe_write<std::uint16_t>(
				primitive + primitive_flags_offset,
				static_cast<std::uint16_t>( *flags | primitive_draw_last ) );
		}

		const auto order = memory::safe_read<std::int32_t>(
			primitive + primitive_draw_order_offset );
		if ( order && *order < INT32_MAX ) {
			(void) memory::safe_write<std::int32_t>(
				primitive + primitive_draw_order_offset, *order + 1 );
		}
	}

} // namespace features::esp::detail
