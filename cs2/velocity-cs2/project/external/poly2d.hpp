#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <numeric>
#include <limits>

namespace poly2d {

	struct point
	{
		float x{}, y{};

		point operator+( const point& o ) const { return { x + o.x, y + o.y }; }
		point operator-( const point& o ) const { return { x - o.x, y - o.y }; }
		point operator*( float s ) const { return { x * s, y * s }; }

		bool operator==( const point& o ) const
		{
			constexpr auto eps = 0.05f;
			return std::abs( x - o.x ) < eps && std::abs( y - o.y ) < eps;
		}

		bool operator!=( const point& o ) const { return !( *this == o ); }
	};

	namespace detail {

		constexpr auto k_pi = std::numbers::pi_v<float>;
		constexpr auto k_two_pi = 2.0f * k_pi;

		inline float cross( const point& o, const point& a, const point& b )
		{
			return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x );
		}

		inline float cross2( const point& a, const point& b )
		{
			return a.x * b.y - a.y * b.x;
		}

		inline float dist_sq( const point& a, const point& b )
		{
			const auto dx = a.x - b.x;
			const auto dy = a.y - b.y;
			return dx * dx + dy * dy;
		}

		inline float edge_angle( const point& from, const point& to )
		{
			return std::atan2( to.y - from.y, to.x - from.x );
		}

		inline bool point_in_convex( const point& p, const std::vector<point>& poly )
		{
			const auto n = poly.size( );
			if ( n < 3 )
			{
				return false;
			}

			for ( std::size_t i = 0; i < n; ++i )
			{
				if ( cross( poly[ i ], poly[ ( i + 1 ) % n ], p ) < -0.01f )
				{
					return false;
				}
			}

			return true;
		}

		inline void ensure_ccw( std::vector<point>& poly )
		{
			auto area{ 0.0f };

			for ( std::size_t i = 0; i < poly.size( ); ++i )
			{
				const auto& a = poly[ i ];
				const auto& b = poly[ ( i + 1 ) % poly.size( ) ];
				area += ( b.x - a.x ) * ( b.y + a.y );
			}

			if ( area > 0.0f )
			{
				std::reverse( poly.begin( ), poly.end( ) );
			}
		}

		struct node_map
		{
			static constexpr float k_cell = 0.5f;
			static constexpr float k_merge_sq = 0.25f;

			struct entry { point pos; std::int32_t idx; };

			std::vector<entry> entries;
			std::vector<std::vector<std::int32_t>> buckets;
			std::int32_t bucket_count{};

			void init( std::int32_t expected )
			{
				bucket_count = std::max( 64, expected * 2 );
				buckets.resize( bucket_count );
				entries.reserve( expected );
			}

			std::int32_t hash( float px, float py ) const
			{
				const auto ix = static_cast< std::int32_t >( std::floor( px / k_cell ) );
				const auto iy = static_cast< std::int32_t >( std::floor( py / k_cell ) );
				auto h = ( ix * 73856093 ) ^ ( iy * 19349663 );
				return ( ( h % bucket_count ) + bucket_count ) % bucket_count;
			}

			std::int32_t find_or_add( const point& p )
			{
				for ( int dx = -1; dx <= 1; ++dx )
				{
					for ( int dy = -1; dy <= 1; ++dy )
					{
						const auto nh = hash( p.x + dx * k_cell, p.y + dy * k_cell );
						for ( const auto ei : buckets[ nh ] )
						{
							if ( dist_sq( entries[ ei ].pos, p ) < k_merge_sq )
							{
								return entries[ ei ].idx;
							}
						}
					}
				}

				const auto idx = static_cast< std::int32_t >( entries.size( ) );
				entries.push_back( { p, idx } );
				buckets[ hash( p.x, p.y ) ].push_back( idx );
				return idx;
			}

			const point& pos( std::int32_t idx ) const { return entries[ idx ].pos; }
			std::int32_t count( ) const { return static_cast< std::int32_t >( entries.size( ) ); }
		};

	} // namespace detail

	inline std::vector<point> make_pill( const point& a, const point& b, float radius, int cap_segments = 6 )
	{
		const auto dx = b.x - a.x;
		const auto dy = b.y - a.y;
		const auto len = std::sqrt( dx * dx + dy * dy );

		if ( len <= 0.5f )
		{
			const auto total = ( cap_segments + 1 ) * 2;
			std::vector<point> circle;
			circle.reserve( total );

			for ( int i = 0; i < total; ++i )
			{
				const auto angle = detail::k_two_pi * static_cast< float >( i ) / static_cast< float >( total );
				circle.push_back( { a.x + std::cos( angle ) * radius, a.y + std::sin( angle ) * radius } );
			}

			return circle;
		}

		const auto nx = static_cast< float >( -dy / len );
		const auto ny = static_cast< float >( dx / len );
		const auto base = static_cast< float >( std::atan2( dy, dx ) );

		std::vector<point> pill;

		const auto interior = std::max( 0, cap_segments - 1 );
		pill.reserve( 4 + static_cast< std::size_t >( interior ) * 2 );

		const point right_a = { a.x + nx * radius, a.y + ny * radius };
		const point right_b = { b.x + nx * radius, b.y + ny * radius };
		const point left_b = { b.x - nx * radius, b.y - ny * radius };
		const point left_a = { a.x - nx * radius, a.y - ny * radius };

		pill.push_back( right_a );
		pill.push_back( right_b );

		{
			const auto angle_start = base + detail::k_pi * 0.5f;
			for ( int i = 1; i < cap_segments; ++i )
			{
				const auto t = static_cast< float >( i ) / static_cast< float >( cap_segments );
				const auto angle = angle_start - detail::k_pi * t;
				pill.push_back( { b.x + std::cos( angle ) * radius, b.y + std::sin( angle ) * radius } );
			}
		}

		pill.push_back( left_b );
		pill.push_back( left_a );

		{
			const auto angle_start = base - detail::k_pi * 0.5f;
			for ( int i = 1; i < cap_segments; ++i )
			{
				const auto t = static_cast< float >( i ) / static_cast< float >( cap_segments );
				const auto angle = angle_start - detail::k_pi * t;
				pill.push_back( { a.x + std::cos( angle ) * radius, a.y + std::sin( angle ) * radius } );
			}
		}

		return pill;
	}

	struct union_result
	{
		std::vector<std::vector<point>> outlines;
	};

	inline union_result union_polygons( std::vector<std::vector<point>>& polys )
	{
		union_result result;

		if ( polys.empty( ) )
		{
			return result;
		}

		if ( polys.size( ) == 1 )
		{
			result.outlines.push_back( polys[ 0 ] );
			return result;
		}

		for ( auto& poly : polys )
		{
			detail::ensure_ccw( poly );
		}

		const auto np = static_cast< std::int32_t >( polys.size( ) );

		struct isect { point pos; float t; std::int32_t poly, edge; };

		std::vector<isect> isects;
		isects.reserve( 256 );

		for ( std::int32_t pi = 0; pi < np; ++pi )
		{
			const auto& pa = polys[ pi ];
			const auto na = static_cast< std::int32_t >( pa.size( ) );

			for ( std::int32_t pj = pi + 1; pj < np; ++pj )
			{
				const auto& pb = polys[ pj ];
				const auto nb = static_cast< std::int32_t >( pb.size( ) );

				for ( std::int32_t ei = 0; ei < na; ++ei )
				{
					const auto& a0 = pa[ ei ];
					const auto& a1 = pa[ ( ei + 1 ) % na ];
					const auto da = a1 - a0;

					for ( std::int32_t ej = 0; ej < nb; ++ej )
					{
						const auto& b0 = pb[ ej ];
						const auto& b1 = pb[ ( ej + 1 ) % nb ];
						const auto db = b1 - b0;

						const auto denom = detail::cross2( da, db );
						if ( std::abs( denom ) < 1e-7f )
						{
							continue;
						}

						const auto d = b0 - a0;
						const auto ta = detail::cross2( d, db ) / denom;
						const auto tb = detail::cross2( d, da ) / denom;

						constexpr auto eps{ 1e-5f };
						if ( ta < eps || ta > 1.0f - eps || tb < eps || tb > 1.0f - eps )
						{
							continue;
						}

						const point ix = { a0.x + da.x * ta, a0.y + da.y * ta };
						isects.push_back( { ix, ta, pi, ei } );
						isects.push_back( { ix, tb, pj, ej } );
					}
				}
			}
		}

		struct sub_edge { point from, to; std::int32_t poly_idx; };

		std::vector<sub_edge> subs;
		subs.reserve( 512 );

		for ( std::int32_t pi = 0; pi < np; ++pi )
		{
			const auto& poly = polys[ pi ];
			const auto n = static_cast< std::int32_t >( poly.size( ) );

			for ( std::int32_t ei = 0; ei < n; ++ei )
			{
				const auto& p0 = poly[ ei ];
				const auto& p1 = poly[ ( ei + 1 ) % n ];

				std::vector<std::pair<float, point>> splits;
				for ( const auto& ix : isects )
				{
					if ( ix.poly == pi && ix.edge == ei )
					{
						splits.push_back( { ix.t, ix.pos } );
					}
				}

				if ( splits.empty( ) )
				{
					subs.push_back( { p0, p1, pi } );
					continue;
				}

				std::sort( splits.begin( ), splits.end( ), [ ]( const auto& a, const auto& b ) { return a.first < b.first; } );

				auto prev = p0;
				for ( const auto& [t, sp] : splits )
				{
					if ( detail::dist_sq( prev, sp ) > 0.01f )
					{
						subs.push_back( { prev, sp, pi } );
					}

					prev = sp;
				}

				if ( detail::dist_sq( prev, p1 ) > 0.01f )
				{
					subs.push_back( { prev, p1, pi } );
				}
			}
		}

		struct half_edge { point from, to; std::int32_t poly_idx; bool used; };

		std::vector<half_edge> boundary;
		boundary.reserve( subs.size( ) );

		for ( const auto& se : subs )
		{
			const point mid = { ( se.from.x + se.to.x ) * 0.5f, ( se.from.y + se.to.y ) * 0.5f };
			auto interior{ false };

			for ( std::int32_t pi = 0; pi < np; ++pi )
			{
				if ( pi == se.poly_idx )
				{
					continue;
				}

				if ( detail::point_in_convex( mid, polys[ pi ] ) )
				{
					interior = true;
					break;
				}
			}

			if ( !interior )
			{
				boundary.push_back( { se.from, se.to, se.poly_idx, false } );
			}
		}

		if ( boundary.empty( ) )
		{
			for ( std::int32_t pi = 0; pi < np; ++pi )
			{
				auto contained{ false };

				for ( std::int32_t pj = 0; pj < np; ++pj )
				{
					if ( pi == pj )
					{
						continue;
					}

					if ( detail::point_in_convex( polys[ pi ][ 0 ], polys[ pj ] ) )
					{
						if ( pi > pj && detail::point_in_convex( polys[ pj ][ 0 ], polys[ pi ] ) )
						{
							continue;
						}

						contained = true;
						break;
					}
				}

				if ( !contained )
				{
					result.outlines.push_back( polys[ pi ] );
				}
			}

			if ( result.outlines.empty( ) && !polys.empty( ) )
			{
				result.outlines.push_back( polys[ 0 ] );
			}

			return result;
		}

		detail::node_map nodes;
		nodes.init( static_cast< std::int32_t >( boundary.size( ) * 2 ) );

		const auto ne = static_cast< std::int32_t >( boundary.size( ) );

		std::vector<std::int32_t> e_from( ne ), e_to( ne );
		for ( std::int32_t ei = 0; ei < ne; ++ei )
		{
			e_from[ ei ] = nodes.find_or_add( boundary[ ei ].from );
			e_to[ ei ] = nodes.find_or_add( boundary[ ei ].to );
		}

		const auto nn = nodes.count( );
		std::vector<std::vector<std::int32_t>> adj( nn );

		for ( std::int32_t ei = 0; ei < ne; ++ei )
		{
			if ( e_from[ ei ] != e_to[ ei ] )
			{
				adj[ e_from[ ei ] ].push_back( ei );
			}
		}

		auto start_node{ 0 };

		for ( std::int32_t ni = 1; ni < nn; ++ni )
		{
			const auto& p = nodes.pos( ni );
			const auto& best = nodes.pos( start_node );

			if ( p.x < best.x || ( std::abs( p.x - best.x ) < 0.01f && p.y < best.y ) )
			{
				start_node = ni;
			}
		}

		auto first_edge{ -1 };

		{
			auto best_diff{ 10.0f };
			for ( const auto ei : adj[ start_node ] )
			{
				const auto angle = detail::edge_angle( boundary[ ei ].from, boundary[ ei ].to );
				const auto diff = std::abs( angle - ( -detail::k_pi * 0.5f ) );
				if ( diff < best_diff )
				{
					best_diff = diff;
					first_edge = ei;
				}
			}
		}

		auto safety_global = ne * 4;

		while ( safety_global > 0 )
		{
			auto current{ -1 };

			if ( first_edge >= 0 && !boundary[ first_edge ].used )
			{
				current = first_edge;
				first_edge = -1;
			}
			else
			{
				for ( std::int32_t ei = 0; ei < ne; ++ei )
				{
					if ( !boundary[ ei ].used )
					{
						current = ei;
						break;
					}
				}
			}

			if ( current < 0 )
			{
				break;
			}

			std::vector<point> contour;
			contour.reserve( 64 );

			const auto loop_start = e_from[ current ];
			auto safety = ne + 1;

			while ( current >= 0 && !boundary[ current ].used && safety-- > 0 )
			{
				boundary[ current ].used = true;
				safety_global--;
				contour.push_back( boundary[ current ].from );

				const auto to = e_to[ current ];
				if ( contour.size( ) > 2 && to == loop_start )
				{
					break;
				}

				const auto incoming = detail::edge_angle( boundary[ current ].from, boundary[ current ].to );
				const auto reverse_in = incoming + detail::k_pi;

				current = -1;
				auto best_turn{ 10.0f };

				for ( const auto ei : adj[ to ] )
				{
					if ( boundary[ ei ].used )
					{
						continue;
					}

					const auto outgoing = detail::edge_angle( boundary[ ei ].from, boundary[ ei ].to );
					auto turn = outgoing - reverse_in;

					while ( turn > detail::k_pi )
					{
						turn -= detail::k_two_pi;
					}

					while ( turn < -detail::k_pi )
					{
						turn += detail::k_two_pi;
					}

					if ( turn < best_turn )
					{
						best_turn = turn;
						current = ei;
					}
				}
			}

			if ( contour.size( ) < 3 )
			{
				continue;
			}

			auto area{ 0.0f };
			for ( std::size_t i = 0; i < contour.size( ); ++i )
			{
				const auto& a = contour[ i ];
				const auto& b = contour[ ( i + 1 ) % contour.size( ) ];
				area += a.x * b.y - b.x * a.y;
			}

			if ( std::abs( area ) > 5.0f )
			{
				result.outlines.push_back( std::move( contour ) );
			}
		}

		if ( result.outlines.empty( ) )
		{
			for ( auto& poly : polys )
			{
				result.outlines.push_back( std::move( poly ) );
			}
		}

		return result;
	}

	inline union_result union_pills( std::vector<std::vector<point>>& pills )
	{
		if ( pills.empty( ) )
		{
			return {};
		}

		auto min_x = pills[ 0 ][ 0 ].x, max_x = min_x;
		auto min_y = pills[ 0 ][ 0 ].y, max_y = min_y;

		for ( const auto& pill : pills )
		{
			for ( const auto& p : pill )
			{
				min_x = std::min( min_x, p.x );
				max_x = std::max( max_x, p.x );
				min_y = std::min( min_y, p.y );
				max_y = std::max( max_y, p.y );
			}
		}

		const auto extent = std::max( max_x - min_x, max_y - min_y );
		constexpr auto target_extent{ 1000.0f };
		const auto scale = ( extent > 0.1f ) ? ( target_extent / extent ) : 1.0f;
		const auto inv_scale = 1.0f / scale;

		const auto needs_scale = ( extent < 200.0f );
		if ( needs_scale )
		{
			for ( auto& pill : pills )
			{
				for ( auto& p : pill )
				{
					p.x *= scale;
					p.y *= scale;
				}
			}
		}

		auto result = union_polygons( pills );

		if ( needs_scale )
		{
			for ( auto& outline : result.outlines )
			{
				for ( auto& p : outline )
				{
					p.x *= inv_scale;
					p.y *= inv_scale;
				}
			}

			for ( auto& pill : pills )
			{
				for ( auto& p : pill )
				{
					p.x *= inv_scale;
					p.y *= inv_scale;
				}
			}
		}

		return result;
	}

	inline std::vector<float> triangulate( const std::vector<point>& poly )
	{
		std::vector<float> tris;
		const auto n = poly.size( );

		if ( n < 3 )
		{
			return tris;
		}

		auto convex{ true };

		for ( std::size_t i = 0; i < n && convex; ++i )
		{
			if ( detail::cross( poly[ i ], poly[ ( i + 1 ) % n ], poly[ ( i + 2 ) % n ] ) < -0.1f )
			{
				convex = false;
			}
		}

		tris.reserve( ( n - 2 ) * 6 );

		if ( convex )
		{
			for ( std::size_t i = 1; i + 1 < n; ++i )
			{
				tris.push_back( poly[ 0 ].x ); tris.push_back( poly[ 0 ].y );
				tris.push_back( poly[ i ].x ); tris.push_back( poly[ i ].y );
				tris.push_back( poly[ i + 1 ].x ); tris.push_back( poly[ i + 1 ].y );
			}

			return tris;
		}

		std::vector<std::int32_t> idx( n );
		std::iota( idx.begin( ), idx.end( ), 0 );

		auto area{ 0.0f };

		for ( std::size_t i = 0; i < n; ++i )
		{
			const auto& a = poly[ i ];
			const auto& b = poly[ ( i + 1 ) % n ];
			area += a.x * b.y - b.x * a.y;
		}

		if ( area < 0.0f )
		{
			std::reverse( idx.begin( ), idx.end( ) );
		}

		auto safety = static_cast< std::int32_t >( n ) * 3;

		while ( idx.size( ) > 2 && safety-- > 0 )
		{
			auto found{ false };
			const auto sz = static_cast< std::int32_t >( idx.size( ) );

			for ( std::int32_t i = 0; i < sz; ++i )
			{
				const auto prev = ( i + sz - 1 ) % sz;
				const auto next = ( i + 1 ) % sz;

				const auto& a = poly[ idx[ prev ] ];
				const auto& b = poly[ idx[ i ] ];
				const auto& c = poly[ idx[ next ] ];

				if ( detail::cross( a, b, c ) <= 0.0f )
				{
					continue;
				}

				auto ear{ true };

				for ( std::int32_t j = 0; j < sz; ++j )
				{
					if ( j == prev || j == i || j == next )
					{
						continue;
					}

					const auto& p = poly[ idx[ j ] ];
					if ( detail::cross( a, b, p ) >= 0.0f && detail::cross( b, c, p ) >= 0.0f && detail::cross( c, a, p ) >= 0.0f )
					{
						ear = false;
						break;
					}
				}

				if ( ear )
				{
					tris.push_back( a.x ); tris.push_back( a.y );
					tris.push_back( b.x ); tris.push_back( b.y );
					tris.push_back( c.x ); tris.push_back( c.y );
					idx.erase( idx.begin( ) + i );
					found = true;
					break;
				}
			}

			if ( !found ) break;
		}

		return tris;
	}

} // namespace poly2d