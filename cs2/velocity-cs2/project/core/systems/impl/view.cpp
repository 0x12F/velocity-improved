#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../systems.hpp"

namespace systems {

	void view::update( std::uintptr_t view )
	{
		this->update_matrix( );

		if ( view )
		{
			this->m_origin = memory::read<math::vector3>( view + 0x0 );
			this->m_angles = memory::read<math::vector3>( view + 0xc );
			this->m_fov = memory::read<float>( view + 0x18 );
		}
		else
		{
			this->m_origin = { k_invalid, k_invalid, k_invalid };
			this->m_angles = { k_invalid, k_invalid, k_invalid };
			this->m_fov = k_invalid;
		}
	}

	void view::update_matrix( )
	{
		if ( !addresses::globals::view_matrix )
		{
			this->m_matrix_valid.store( false, std::memory_order_release );
			return;
		}

		const auto matrix = memory::read<math::matrix4x4>( addresses::globals::view_matrix );
		auto valid = true;
		auto magnitude = 0.0f;

		for ( auto row = 0; row < 4; ++row )
		{
			for ( auto column = 0; column < 4; ++column )
			{
				const auto value = matrix[ row ][ column ];
				valid = valid && std::isfinite( value );
				magnitude += std::fabsf( value );
			}
		}

		valid = valid && magnitude > 0.001f;
		if ( valid )
		{
			std::scoped_lock lock( this->m_matrix_mtx );
			this->m_matrix = matrix;
		}

		this->m_matrix_valid.store( valid, std::memory_order_release );
	}

	math::vector2 view::project( const math::vector3& world_pos )
	{
		if ( !this->has_camera( ) )
		{
			return { this->k_invalid, this->k_invalid };
		}

		const auto m = this->matrix( );

		const auto w = m[ 3 ][ 0 ] * world_pos.x + m[ 3 ][ 1 ] * world_pos.y + m[ 3 ][ 2 ] * world_pos.z + m[ 3 ][ 3 ];

		if ( w < 0.001f )
		{
			return { this->k_invalid, this->k_invalid };
		}

		const auto x = m[ 0 ][ 0 ] * world_pos.x + m[ 0 ][ 1 ] * world_pos.y + m[ 0 ][ 2 ] * world_pos.z + m[ 0 ][ 3 ];
		const auto y = m[ 1 ][ 0 ] * world_pos.x + m[ 1 ][ 1 ] * world_pos.y + m[ 1 ][ 2 ] * world_pos.z + m[ 1 ][ 3 ];

		const auto display = xdraw::viewport_size( );
		const auto inv_w = 1.0f / w;

		return
		{
			display.first * 0.5f * ( 1.0f + x * inv_w ),
			display.second * 0.5f * ( 1.0f - y * inv_w )
		};
	}

	view::projection view::project_full( const math::vector3& world_pos ) const
	{
		if ( !this->has_camera( ) )
		{
			return {};
		}

		const auto m = this->matrix( );

		const auto w = m[ 3 ][ 0 ] * world_pos.x + m[ 3 ][ 1 ] * world_pos.y + m[ 3 ][ 2 ] * world_pos.z + m[ 3 ][ 3 ];
		if ( w < 0.001f )
		{
			return {};
		}

		const auto x = m[ 0 ][ 0 ] * world_pos.x + m[ 0 ][ 1 ] * world_pos.y + m[ 0 ][ 2 ] * world_pos.z + m[ 0 ][ 3 ];
		const auto y = m[ 1 ][ 0 ] * world_pos.x + m[ 1 ][ 1 ] * world_pos.y + m[ 1 ][ 2 ] * world_pos.z + m[ 1 ][ 3 ];

		const auto [sw, sh] = xdraw::viewport_size( );
		const auto inv_w = 1.0f / w;

		const math::vector2 screen
		{
			sw * 0.5f * ( 1.0f + x * inv_w ),
			sh * 0.5f * ( 1.0f - y * inv_w )
		};

		projection r{};
		r.screen = screen;
		r.w = w;
		r.on_screen = w > 0.01f&& screen.x >= 0.0f && screen.x < static_cast< float >( sw )&& screen.y >= 0.0f && screen.y < static_cast< float >( sh );
		return r;
	}

	math::matrix4x4 view::matrix( ) const
	{
		std::scoped_lock lock( this->m_matrix_mtx );
		return this->m_matrix;
	}

} // namespace systems
