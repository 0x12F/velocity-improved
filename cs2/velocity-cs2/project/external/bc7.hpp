#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace bc7 {

	namespace detail {

		struct bit_reader
		{
			const std::uint8_t* data;
			int pos;

			std::uint32_t read( int bits )
			{
				auto val{ 0u };

				for ( auto i = 0; i < bits; ++i )
				{
					val |= ( ( data[ pos >> 3 ] >> ( pos & 7 ) ) & 1u ) << i;
					++pos;
				}

				return val;
			}
		};

		struct mode_info
		{
			int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
		};

		constexpr mode_info k_modes[ 8 ]
		{
			{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
			{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
			{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
			{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
			{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
			{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
			{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
			{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
		};

		constexpr std::uint8_t k_partitions2[ 64 ][ 16 ]
		{
			{0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1},{0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},
			{0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1},{0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
			{0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},
			{0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
			{0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1},
			{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
			{0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},
			{0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
			{0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1},{0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0},{0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
			{0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0},{0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0},
			{0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0},{0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
			{0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},{0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0},
			{0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0},{0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
			{0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0},{0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},
			{0,1,1,0,0,0,1,1,1,1,0,0,0,1,1,0},{0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
			{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},{0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
			{0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0},{0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
			{0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0},{0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0},
			{0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1},{0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
			{0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0},{0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0},
			{0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0},{0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
			{0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0},{0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1},
			{0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1},{0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
			{0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0},{0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0},
			{0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0},{0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
			{0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1},
			{0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0},{0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
			{0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1},{0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1},
			{0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1},{0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
			{0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1},{0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0},
			{0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0},{0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1},
		};

		constexpr std::uint8_t k_partitions3[ 64 ][ 16 ]
		{
			{0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2},{0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1},
			{0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1},{0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
			{0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2},{0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2},
			{0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1},{0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
			{0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2},{0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2},
			{0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2},{0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
			{0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2},{0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2},
			{0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2},{0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
			{0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2},{0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0},
			{0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2},{0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
			{0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2},{0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1},
			{0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2},{0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
			{0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0},{0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2},
			{0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0},{0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
			{0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2},{0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2},
			{0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1},{0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
			{0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2},{0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1},
			{0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2},{0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
			{0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0},{0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},
			{0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0},{0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
			{0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1},{0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2},
			{0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1},{0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
			{0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1},{0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1},
			{0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1},{0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
			{0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2},{0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1},
			{0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2},{0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
			{0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2},{0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2},
			{0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2},{0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
			{0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2},{0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},
			{0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2},{0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
			{0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1},{0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2},
			{0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2},{0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0},
		};

		constexpr std::uint8_t k_anchor2[ 64 ]
		{
			15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
			15, 2, 8, 2, 2, 8, 8,15, 2, 8, 2, 2, 8, 8, 2, 2,
			15,15, 6, 8, 2, 8,15,15, 2, 8, 2, 2, 2,15,15, 6,
			 6, 2, 6, 8,15,15, 2, 2,15,15,15,15, 3, 6, 6, 8,
		};

		constexpr std::uint8_t k_anchor3a[ 64 ]
		{
			 3, 3,15,15, 8, 3,15,15, 8, 8, 6, 6, 6, 5, 3, 3,
			 3, 3, 8,15, 3, 3, 6,10, 5, 8, 8, 6, 8, 5,15,15,
			 8,15, 3, 5, 6,10, 8,15,15, 3,15, 5,15,15,15,15,
			 3,15, 5, 5, 5, 8, 5,10, 5,10, 8,13,15,12, 3, 3,
		};

		constexpr std::uint8_t k_anchor3b[ 64 ]
		{
			15, 8, 8, 3,15,15, 3, 8,15,15,15,15,15,15,15, 8,
			15, 8,15, 3,15, 8,15, 8, 3,15, 6,10,15,15,10, 8,
			15, 3,15,10,10, 8, 9,10, 6,15, 8,15, 3, 6, 6, 8,
			15, 3,15,15,15,15,15,15,15,15,15,15, 3,15,15, 8,
		};

		constexpr std::uint8_t k_interp2[ ]{ 0, 21, 43, 64 };
		constexpr std::uint8_t k_interp3[ ]{ 0, 9, 18, 27, 37, 46, 55, 64 };
		constexpr std::uint8_t k_interp4[ ]{ 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

		inline std::uint8_t interp( std::uint8_t e0, std::uint8_t e1, int index, int index_bits )
		{
			const auto* weights = index_bits == 2 ? k_interp2 : ( index_bits == 3 ? k_interp3 : k_interp4 );
			return static_cast< std::uint8_t >( ( e0 * ( 64 - weights[ index ] ) + e1 * weights[ index ] + 32 ) >> 6 );
		}

		inline std::uint8_t expand( std::uint32_t val, int bits )
		{
			if ( bits >= 8 )
			{
				return static_cast< std::uint8_t >( val );
			}

			val <<= ( 8 - bits );
			val |= ( val >> bits );
			return static_cast< std::uint8_t >( val );
		}

	} // namespace detail

	inline void decode_block( const std::uint8_t* block, std::uint8_t* out_rgba )
	{
		detail::bit_reader br{ block, 0 };

		auto mode{ 0 };
		while ( mode < 8 && !( block[ 0 ] & ( 1 << mode ) ) )
		{
			++mode;
		}

		if ( mode >= 8 )
		{
			std::memset( out_rgba, 0, 64 );
			return;
		}

		br.pos = mode + 1;

		const auto& mi = detail::k_modes[ mode ];
		const auto partition = mi.pb ? br.read( mi.pb ) : 0u;
		const auto rotation = mi.rb ? br.read( mi.rb ) : 0u;
		const auto idx_sel = mi.isb ? br.read( 1 ) : 0u;
		const auto num_ep = mi.ns * 2;

		std::uint8_t endpoints[ 6 ][ 4 ]{};

		for ( auto ch = 0; ch < 3; ++ch )
		{
			for ( auto ep = 0; ep < num_ep; ++ep )
			{
				endpoints[ ep ][ ch ] = static_cast< std::uint8_t >( br.read( mi.cb ) );
			}
		}

		if ( mi.ab )
		{
			for ( auto ep = 0; ep < num_ep; ++ep )
			{
				endpoints[ ep ][ 3 ] = static_cast< std::uint8_t >( br.read( mi.ab ) );
			}
		}
		else
		{
			for ( auto ep = 0; ep < num_ep; ++ep )
			{
				endpoints[ ep ][ 3 ] = 255;
			}
		}

		if ( mi.epb )
		{
			for ( auto ep = 0; ep < num_ep; ++ep )
			{
				const auto pbit = br.read( 1 );
				for ( auto ch = 0; ch < 3; ++ch )
				{
					endpoints[ ep ][ ch ] = static_cast< std::uint8_t >( ( endpoints[ ep ][ ch ] << 1 ) | pbit );
				}
				if ( mi.ab )
				{
					endpoints[ ep ][ 3 ] = static_cast< std::uint8_t >( ( endpoints[ ep ][ 3 ] << 1 ) | pbit );
				}
			}
		}
		else if ( mi.spb )
		{
			for ( auto s = 0; s < mi.ns; ++s )
			{
				const auto pbit = br.read( 1 );
				for ( auto j = 0; j < 2; ++j )
				{
					for ( auto ch = 0; ch < 3; ++ch )
					{
						endpoints[ s * 2 + j ][ ch ] = static_cast< std::uint8_t >( ( endpoints[ s * 2 + j ][ ch ] << 1 ) | pbit );
					}
					if ( mi.ab )
					{
						endpoints[ s * 2 + j ][ 3 ] = static_cast< std::uint8_t >( ( endpoints[ s * 2 + j ][ 3 ] << 1 ) | pbit );
					}
				}
			}
		}

		const auto color_bits = mi.cb + ( mi.epb || mi.spb ? 1 : 0 );
		const auto alpha_bits = mi.ab ? mi.ab + ( mi.epb || mi.spb ? 1 : 0 ) : 0;

		for ( auto ep = 0; ep < num_ep; ++ep )
		{
			for ( auto ch = 0; ch < 3; ++ch )
			{
				endpoints[ ep ][ ch ] = detail::expand( endpoints[ ep ][ ch ], color_bits );
			}
			if ( mi.ab )
			{
				endpoints[ ep ][ 3 ] = detail::expand( endpoints[ ep ][ 3 ], alpha_bits );
			}
		}

		std::uint8_t indices[ 16 ]{};
		std::uint8_t indices2[ 16 ]{};

		for ( auto i = 0; i < 16; ++i )
		{
			auto is_anchor = ( i == 0 );

			if ( mi.ns >= 2 )
			{
				is_anchor = is_anchor || ( i == static_cast< int >( detail::k_anchor2[ partition ] ) );
			}

			if ( mi.ns >= 3 )
			{
				is_anchor = is_anchor || ( i == static_cast< int >( detail::k_anchor3a[ partition ] ) ) || ( i == static_cast< int >( detail::k_anchor3b[ partition ] ) );
			}

			indices[ i ] = static_cast< std::uint8_t >( br.read( mi.ib - ( is_anchor ? 1 : 0 ) ) );
		}

		if ( mi.ib2 )
		{
			for ( auto i = 0; i < 16; ++i )
			{
				indices2[ i ] = static_cast< std::uint8_t >( br.read( mi.ib2 - ( i == 0 ? 1 : 0 ) ) );
			}
		}

		for ( auto i = 0; i < 16; ++i )
		{
			const auto* part_table = mi.ns == 3 ? detail::k_partitions3[ partition ] : ( mi.ns == 2 ? detail::k_partitions2[ partition ] : nullptr );
			const auto subset = part_table ? part_table[ i ] : 0;
			const auto e0 = subset * 2;
			const auto e1 = subset * 2 + 1;

			auto color_idx = indices[ i ];
			auto alpha_idx = mi.ib2 ? indices2[ i ] : indices[ i ];
			auto color_bits_i = mi.ib;
			auto alpha_bits_i = mi.ib2 ? mi.ib2 : mi.ib;

			if ( idx_sel )
			{
				std::swap( color_idx, alpha_idx );
				std::swap( color_bits_i, alpha_bits_i );
			}

			auto* dst = out_rgba + i * 4;
			dst[ 0 ] = detail::interp( endpoints[ e0 ][ 0 ], endpoints[ e1 ][ 0 ], color_idx, color_bits_i );
			dst[ 1 ] = detail::interp( endpoints[ e0 ][ 1 ], endpoints[ e1 ][ 1 ], color_idx, color_bits_i );
			dst[ 2 ] = detail::interp( endpoints[ e0 ][ 2 ], endpoints[ e1 ][ 2 ], color_idx, color_bits_i );
			dst[ 3 ] = mi.ab ? detail::interp( endpoints[ e0 ][ 3 ], endpoints[ e1 ][ 3 ], alpha_idx, alpha_bits_i ) : 255;

			switch ( rotation )
			{
			case 1: std::swap( dst[ 3 ], dst[ 0 ] ); break;
			case 2: std::swap( dst[ 3 ], dst[ 1 ] ); break;
			case 3: std::swap( dst[ 3 ], dst[ 2 ] ); break;
			}
		}
	}

	inline void decode_image( const std::uint8_t* src, std::uint8_t* dst_rgba, int width, int height )
	{
		const auto bw = ( width + 3 ) / 4;
		const auto bh = ( height + 3 ) / 4;

		for ( auto by = 0; by < bh; ++by )
		{
			for ( auto bx = 0; bx < bw; ++bx )
			{
				std::uint8_t block_rgba[ 64 ];
				decode_block( src, block_rgba );
				src += 16;

				for ( int py = 0; py < 4; ++py )
				{
					const auto dy = by * 4 + py;
					if ( dy >= height )
					{
						break;
					}

					for ( int px = 0; px < 4; ++px )
					{
						const auto dx = bx * 4 + px;
						if ( dx >= width )
						{
							break;
						}

						std::memcpy( dst_rgba + ( dy * width + dx ) * 4, block_rgba + ( py * 4 + px ) * 4, 4 );
					}
				}
			}
		}
	}

} // namespace bc7