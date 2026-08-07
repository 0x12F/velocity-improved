#pragma once

namespace fnv1a {

	constexpr std::uint32_t hash( const char* str, std::size_t length ) noexcept
	{
		std::uint32_t hash{ 2166136261u };

		for ( auto i = 0ull; i < length; ++i )
		{
			hash ^= static_cast< std::uint32_t >( str[ i ] );
			hash *= 16777619u;
		}

		return hash;
	}

	inline std::uint32_t runtime_hash( const char* str ) noexcept
	{
		if ( !str )
		{
			return 0;
		}

		std::uint32_t hash{ 2166136261u };

		while ( *str )
		{
			hash ^= static_cast< std::uint32_t >( *str++ );
			hash *= 16777619u;
		}

		return hash;
	}

} // namespace fnv1a

constexpr std::uint32_t operator""_hash( const char* str, std::size_t length ) noexcept
{
	return fnv1a::hash( str, length );
}
