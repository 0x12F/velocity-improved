#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <protection/game_addresses.hpp>
#include "random.hpp"

namespace random {

	static std::mt19937& rng( )
	{
		static std::mt19937 gen{ std::random_device{}( ) };
		return gen;
	}

	void seed( std::uint32_t s )
	{
		rng( ).seed( s );
	}

	int integer( int min, int max )
	{
		return std::uniform_int_distribution<int>{ min, max }( rng( ) );
	}

	float floating( float min, float max )
	{
		return std::uniform_real_distribution<float>{ min, max }( rng( ) );
	}

	float normal( float mean, float stddev )
	{
		return std::normal_distribution<float>{ mean, stddev }( rng( ) );
	}

	float normal_clamped( float mean, float stddev, float min, float max )
	{
		auto val = normal( mean, stddev );
		if ( val < min )
		{
			val = min;
		}

		if ( val > max )
		{
			val = max;
		}

		return val;
	}

	float hold_duration( float mean, float stddev )
	{
		return normal_clamped( mean, stddev, 0.045f, 0.140f );
	}

	float subtick_fraction( )
	{
		return floating( 0.0f, std::nextafter( 1.0f, 0.0f ) );
	}

	namespace engine {

		void seed( std::uint32_t s )
		{
			memory::call<void>(MODULE_EXPORT ("tier0.dll:RandomSeed"), s );
		}

		int random_int( int min, int max )
		{
			return memory::call<int>(MODULE_EXPORT ("tier0.dll:RandomInt"), min, max );
		}

		float random_float( float min, float max )
		{
			return memory::call<float>(MODULE_EXPORT ("tier0.dll:RandomFloat"), min, max );
		}

		std::int32_t md5_pseudo_random( std::int32_t seed )
		{
			return memory::call<std::int32_t>(MODULE_EXPORT ("tier0.dll:MD5_PseudoRandom"), seed );
		}

	} // namespace engine

} // namespace random