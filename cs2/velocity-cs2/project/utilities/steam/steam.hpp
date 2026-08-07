#pragma once

namespace steam {

	class http
	{
	public:
		static bool initialize( );

		static std::uint32_t create_get( const char* url );
		static std::uint32_t create_post( const char* url, const char* content_type, const void* body, std::uint32_t body_size );

		static bool set_header( std::uint32_t req, const char* name, const char* value );
		static bool set_timeout( std::uint32_t req, std::uint32_t seconds );

		static bool send( std::uint32_t req, std::uintptr_t* out_call );
		static bool send_and_wait( std::uint32_t req, std::uint32_t timeout_ms );
		static bool send_and_forget( std::uint32_t req );

		static bool get_response_body( std::uint32_t req, std::vector<std::uint8_t>& out_data );
		static void release( std::uint32_t req );
	};

	class friends
	{
	public:
		static bool initialize( );
		static int get_medium_friend_avatar( std::uint64_t steam_id );
	};

	class user
	{
	public:
		static bool initialize( );
		static std::uint64_t get_steam_id( );
	};

	class utils
	{
	public:
		static bool initialize( );
		static bool get_image_size( int image, std::uint32_t* width, std::uint32_t* height );
		static bool get_image_rgba( int image, std::uint8_t* dest, int dest_size );
	};

} // namespace steam
