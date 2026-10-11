#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace features::misc::server_regions {

	inline constexpr std::array names{
		"North America", "Europe", "South America", "Asia", "Oceania", "Middle East", "Africa"
	};

	// Valve's CS2 SDR POP codes. Keep four-character data centers distinct.
	inline constexpr std::array<std::string_view, names.size( )> data_centers{
		"atl,dfw,eat,iad,lax,ord,sea",
		"ams,fra,fsn,hel,lhr,mad,par,sto,sto2,vie,waw,dfra,dvie,dsto,dlhr",
		"eze,gru,lim,scl",
		"bom,bom2,maa,maa2,ctu,ctum,ctut,ctuu,pek,pekm,pekt,peku,pvg,pvgm,pvgt,pvgu,tgd,tgdm,tgdt,tgdu,hkg,seo,sgp,tyo",
		"syd,gum", "dxb", "jnb"
	};

	inline constexpr int disabled{ -1 };
	inline constexpr int selected_ping_ms{ 1 };
	inline constexpr int excluded_ping_ms{ 9999 };

	[[nodiscard]] constexpr bool valid( int region )
	{
		return region >= 0 && region < static_cast<int>( names.size( ) );
	}

	[[nodiscard]] constexpr std::uint32_t pop_id( std::string_view code )
	{
		if ( code.size( ) != 3 && code.size( ) != 4 )
			return 0;

		return ( static_cast<std::uint32_t>( code[ 0 ] ) << 16 )
			| ( static_cast<std::uint32_t>( code[ 1 ] ) << 8 )
			| static_cast<std::uint32_t>( code[ 2 ] )
			| ( code.size( ) == 4 ? static_cast<std::uint32_t>( code[ 3 ] ) << 24 : 0 );
	}

	[[nodiscard]] constexpr bool contains( int region, std::uint32_t pop )
	{
		if ( !valid( region ) )
			return false;

		auto codes = data_centers[ region ];
		while ( !codes.empty( ) )
		{
			const auto separator = codes.find( ',' );
			if ( pop_id( codes.substr( 0, separator ) ) == pop )
				return true;
			if ( separator == std::string_view::npos )
				break;
			codes.remove_prefix( separator + 1 );
		}
		return false;
	}

	[[nodiscard]] constexpr int filter_ping( int region, std::uint32_t pop, int measured_ping )
	{
		if ( !valid( region ) )
			return measured_ping;
		if ( !contains( region, pop ) )
			return excluded_ping_ms;
		// A negative result can mean "not measured", particularly for distant POPs.
		// Keep selected centers eligible so they reach the matchmaking report.
		return selected_ping_ms;
	}

} // namespace features::misc::server_regions
