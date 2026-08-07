#pragma once

#include <external/stackwalker/stackwalker.hpp>
#include <external/nlohmann/json.hpp>
#include <vector>
#include <thread>
#include <atomic>

namespace security {

	namespace integrity {

		struct cached_hash
		{
			std::uintptr_t module_base;
			std::uint32_t crc32;
			bool valid;
		};

		bool initialize( );
		cached_hash* get( std::uintptr_t module_base );

	} // namespace integrity

	namespace regions {

		struct region
		{
			std::uintptr_t base;
			std::size_t size;
		};

		void add( std::uintptr_t base, std::size_t size );
		void add_module( HMODULE module );
		bool is_protected( const void* address, std::size_t size );

	} // namespace regions

	namespace prologues {

		struct hook_info
		{
			std::uintptr_t target;
			const std::uint8_t* bytes;
			std::size_t length;
		};

		void add( std::uintptr_t target, const std::uint8_t* bytes, std::size_t length );
		hook_info* get( std::uintptr_t address );

	} // namespace prologues

} // namespace security