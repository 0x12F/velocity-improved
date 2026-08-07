#pragma once

namespace math {

	class vector2
	{
	public:
		float x, y;

		vector2( ) noexcept : x( 0.0f ), y( 0.0f ) {}
		constexpr vector2( float x, float y ) noexcept : x( x ), y( y ) {}

		[[nodiscard]] vector2 operator+( const vector2& v ) const noexcept;
		[[nodiscard]] vector2 operator-( const vector2& v ) const noexcept;
		[[nodiscard]] vector2 operator*( float scalar ) const noexcept;
		[[nodiscard]] vector2 operator/( float scalar ) const noexcept;
		[[nodiscard]] vector2 operator-( ) const noexcept;

		vector2& operator*=( float scalar ) noexcept;
		vector2& operator/=( float scalar ) noexcept;
		vector2& operator+=( const vector2& v ) noexcept;
		vector2& operator-=( const vector2& v ) noexcept;

		[[nodiscard]] bool operator==( const vector2& v ) const noexcept;
		[[nodiscard]] bool operator!=( const vector2& v ) const noexcept;

		[[nodiscard]] float dot( const vector2& v ) const noexcept;
		[[nodiscard]] float length_sqr( ) const noexcept;
		[[nodiscard]] float length( ) const noexcept;

		vector2& normalize( ) noexcept;
		[[nodiscard]] vector2 normalized( ) const noexcept;
	};

	class vector4 {
	public:
		float x, y, z, w;
	};

	class vector3
	{
	public:
		float x, y, z;

		vector3( ) noexcept : x( 0.0f ), y( 0.0f ), z( 0.0f ) {}
		constexpr vector3( float x, float y, float z ) noexcept : x( x ), y( y ), z( z ) {}

		[[nodiscard]] vector3 operator+( const vector3& v ) const noexcept;
		[[nodiscard]] vector3 operator-( const vector3& v ) const noexcept;
		[[nodiscard]] vector3 operator*( float scalar ) const noexcept;
		[[nodiscard]] vector3 operator/( float scalar ) const noexcept;
		[[nodiscard]] vector3 operator-( ) const noexcept;

		vector3& operator*=( float scalar ) noexcept;
		vector3& operator/=( float scalar ) noexcept;
		vector3& operator+=( const vector3& v ) noexcept;
		vector3& operator-=( const vector3& v ) noexcept;

		[[nodiscard]] bool operator==( const vector3& v ) const noexcept;
		[[nodiscard]] bool operator!=( const vector3& v ) const noexcept;

		[[nodiscard]] float dot( const vector3& v ) const noexcept;
		[[nodiscard]] vector3 cross( const vector3& v ) const noexcept;

		[[nodiscard]] float length_sqr( ) const noexcept;
		[[nodiscard]] float length( ) const noexcept;
		[[nodiscard]] float length_2d( ) const noexcept;

		vector3& normalize( ) noexcept;
		[[nodiscard]] vector3 normalized( ) const noexcept;
		[[nodiscard]] float normalize_2d( ) noexcept;
		[[nodiscard]] float distance( const vector3& v ) const noexcept;
		[[nodiscard]] float distance_sqr( const vector3& v ) const noexcept;
		[[nodiscard]] vector3 to_right_vector( ) const noexcept;
		void to_directions( vector3* forward, vector3* right, vector3* up ) const noexcept;
	};

	class quaternion
	{
	public:
		float x, y, z, w;

		quaternion( ) noexcept;
		constexpr quaternion( float x, float y, float z, float w ) noexcept;

		[[nodiscard]] static quaternion from_euler( const vector3& euler ) noexcept;
		[[nodiscard]] vector3 rotate_vector( const vector3& v ) const noexcept;
	};

	class matrix3x4
	{
	public:
		float mat[ 3 ][ 4 ];

		[[nodiscard]] const float* operator[]( int i ) const noexcept;
		[[nodiscard]] float* operator[]( int i ) noexcept;
	};

	class matrix4x4
	{
	public:
		float mat[ 4 ][ 4 ];

		[[nodiscard]] const float* operator[]( int index ) const noexcept;
		[[nodiscard]] float* operator[]( int index ) noexcept;
	};

	namespace helpers {

		void angle_vectors_left( const vector3& angles, vector3* forward = nullptr, vector3* left = nullptr, vector3* up = nullptr );
		void angle_vectors_2d( float yaw, math::vector3& forward, math::vector3& right );
		void normalize_angles( vector3& angles );
		void normalize_angle( float& angle );

		[[nodiscard]] vector3 vector_to_angle( const vector3& forward );
		[[nodiscard]] vector3 calculate_angle( const vector3& src, const vector3& dst );
		[[nodiscard]] float angle_distance( const vector3& from, const vector3& to );
		[[nodiscard]] float deg_to_rad( float degrees );
		[[nodiscard]] float rad_to_deg( float radians );
		[[nodiscard]] float normalize_yaw( float yaw );

	} // namespace helpers

} // namespace math