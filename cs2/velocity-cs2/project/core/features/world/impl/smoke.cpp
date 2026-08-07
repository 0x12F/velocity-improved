#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../world.hpp"

// was a deadend.

namespace features::world {

	void smoke::on_map( std::uintptr_t token, std::size_t size, std::uintptr_t buf_ptr )
	{
		if ( size != 2480 || !token || !buf_ptr )
		{
			return;
		}

		this->m_token = token;
		this->m_buf = buf_ptr;
	}

	void smoke::on_unmap( std::uintptr_t token )
	{
		const auto buf = this->m_buf;
		if ( !buf || token != this->m_token )
		{
			return;
		}

		if ( settings::g_misc.m_removals.smoke.value )
		{
			const auto count = *reinterpret_cast< std::uint32_t* >( buf + 2464 );
			if ( count > 0 && count <= 16 )
			{
				for ( auto i = 0u; i < count; ++i )
				{
					auto opacity = reinterpret_cast< float* >( buf + 1280 + 16 * static_cast< std::size_t >( i ) );
					opacity[ 0 ] = 0.0f;
				}
			}
		}

		this->m_buf = 0;
		this->m_token = 0;
	}

} // namespace features::world