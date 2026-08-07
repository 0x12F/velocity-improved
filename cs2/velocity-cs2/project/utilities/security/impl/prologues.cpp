#include <pch/pch.hpp>
#include "../security.hpp"

namespace security::prologues {

	namespace detail {

		std::vector<hook_info> hooks{};

	} // namespace detail

	void add( std::uintptr_t target, const std::uint8_t* bytes, std::size_t length )
	{
		detail::hooks.push_back( { target, bytes, length } );
	}

	hook_info* get( std::uintptr_t address )
	{
		for ( auto& hook : detail::hooks )
		{
			if ( address >= hook.target && address < hook.target + hook.length )
			{
				return &hook;
			}
		}

		return nullptr;
	}

} // namespace security::prologues