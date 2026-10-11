#pragma once

#include "server_regions.hpp"
#include <span>
#include <vector>

namespace features::misc::server_ping_report {

	inline constexpr std::uint32_t message_type{ 9103 }; // MatchmakingClient2ServerPing
	inline constexpr std::uint32_t protobuf_flag{ 0x80000000u };
	inline constexpr std::size_t envelope_size{ 8 };
	inline constexpr std::size_t maximum_report_size{ 1024 * 1024 };

	enum class result { unchanged, rewritten, malformed, no_selected_centers };

	namespace detail {
		using bytes = std::span<const std::uint8_t>;

		inline std::uint32_t read_fixed32( bytes data )
		{
			return std::uint32_t( data[ 0 ] ) | ( std::uint32_t( data[ 1 ] ) << 8 )
				| ( std::uint32_t( data[ 2 ] ) << 16 ) | ( std::uint32_t( data[ 3 ] ) << 24 );
		}

		inline bool read_varint( bytes data, std::size_t& offset, std::uint64_t& value )
		{
			value = 0;
			for ( unsigned shift = 0; shift < 64 && offset < data.size( ); shift += 7 )
			{
				const auto byte = data[ offset++ ];
				if ( shift == 63 && byte > 1 )
					return false;
				value |= std::uint64_t( byte & 0x7f ) << shift;
				if ( !( byte & 0x80 ) )
					return true;
			}
			return false;
		}

		struct field
		{
			std::uint32_t number{};
			unsigned wire_type{};
			bytes encoded{};
			bytes payload{};
		};

		inline bool next_field( bytes data, std::size_t& offset, field& out )
		{
			const auto start = offset;
			std::uint64_t tag{};
			if ( !read_varint( data, offset, tag ) || tag < 8 || tag > 0xffffffffu )
				return false;
			out.number = static_cast<std::uint32_t>( tag >> 3 );
			out.wire_type = tag & 7;
			auto payload_start = offset;
			std::uint64_t length{};
			switch ( out.wire_type )
			{
			case 0:
				if ( !read_varint( data, offset, length ) )
					return false;
				break;
			case 1: length = 8; break;
			case 5: length = 4; break;
			case 2:
				if ( !read_varint( data, offset, length ) )
					return false;
				payload_start = offset;
				break;
			default: return false;
			}
			if ( out.wire_type != 0 )
			{
				if ( length > data.size( ) - offset )
					return false;
				offset += static_cast<std::size_t>( length );
			}
			out.encoded = data.subspan( start, offset - start );
			out.payload = data.subspan( payload_start, offset - payload_start );
			return true;
		}

		inline void append( std::vector<std::uint8_t>& out, bytes data )
		{
			out.insert( out.end( ), data.begin( ), data.end( ) );
		}

		inline void append_varint( std::vector<std::uint8_t>& out, std::uint64_t value )
		{
			while ( value >= 0x80 )
			{
				out.push_back( static_cast<std::uint8_t>( value ) | 0x80 );
				value >>= 7;
			}
			out.push_back( static_cast<std::uint8_t>( value ) );
		}

		inline bool rewrite_center( bytes data, int region, std::vector<std::uint8_t>& out, bool& selected )
		{
			std::size_t offset{};
			std::uint32_t pop{};
			bool has_pop{};
			while ( offset < data.size( ) )
			{
				field current;
				if ( !next_field( data, offset, current ) )
					return false;
				if ( current.number == 1 )
				{
					if ( current.wire_type != 5 )
						return false;
					pop = read_fixed32( current.payload );
					has_pop = true;
				}
				if ( current.number == 2 )
				{
					if ( current.wire_type != 0 )
						return false;
				}
				else
					append( out, current.encoded );
			}
			if ( !has_pop )
				return false;
			selected = server_regions::contains( region, pop );
			out.push_back( 2 << 3 ); // DataCenterPing.ping is sint32 (ZigZag).
			append_varint( out, std::uint32_t( selected ? server_regions::selected_ping_ms
				: server_regions::excluded_ping_ms ) << 1 );
			return true;
		}
	}

	// Preserve the GC envelope and every unrelated protobuf field byte-for-byte.
	inline result rewrite( std::span<const std::uint8_t> packet, int region, std::vector<std::uint8_t>& output )
	{
		output.clear( );
		if ( !server_regions::valid( region ) )
			return result::unchanged;
		if ( packet.size( ) < envelope_size || packet.size( ) > maximum_report_size )
			return result::malformed;
		if ( detail::read_fixed32( packet ) != ( message_type | protobuf_flag ) )
			return result::malformed;
		const auto header_size = detail::read_fixed32( packet.subspan( 4 ) );
		if ( header_size > packet.size( ) - envelope_size )
			return result::malformed;
		const auto body_offset = envelope_size + header_size;
		const auto body = packet.subspan( body_offset );
		std::vector<std::uint8_t> rewritten;
		detail::append( rewritten, packet.first( body_offset ) );
		std::size_t offset{};
		bool has_selected_center{};
		while ( offset < body.size( ) )
		{
			detail::field current;
			if ( !detail::next_field( body, offset, current ) )
				return result::malformed;
			if ( current.number == 4 )
			{
				std::vector<std::uint8_t> center;
				bool selected{};
				if ( current.wire_type != 2 || !detail::rewrite_center( current.payload, region, center, selected ) )
					return result::malformed;
				has_selected_center |= selected;
				rewritten.push_back( ( 4 << 3 ) | 2 );
				detail::append_varint( rewritten, center.size( ) );
				detail::append( rewritten, center );
			}
			else if ( current.number == 5 )
			{
				if ( current.wire_type != 0 )
					return result::malformed;
			}
			else
				detail::append( rewritten, current.encoded );
		}
		if ( !has_selected_center )
			return result::no_selected_centers;
		constexpr std::uint32_t matchmaking_ping_limit{ 25 };
		rewritten.push_back( 5 << 3 );
		detail::append_varint( rewritten, matchmaking_ping_limit );
		output = std::move( rewritten );
		return result::rewritten;
	}

} // namespace features::misc::server_ping_report
