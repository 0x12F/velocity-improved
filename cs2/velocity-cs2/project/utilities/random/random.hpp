#pragma once

namespace random {

	void seed( std::uint32_t s );
	int integer( int min, int max );
	float floating( float min, float max );

	float normal( float mean, float stddev );
	float normal_clamped( float mean, float stddev, float min, float max );

	float hold_duration( float mean = 0.085f, float stddev = 0.012f );
	float subtick_fraction( );

	namespace engine {

		void seed( std::uint32_t s );
		int random_int( int min, int max );
		float random_float( float min, float max );
		std::int32_t md5_pseudo_random( std::int32_t seed );

	} // namespace engine

} // namespace random