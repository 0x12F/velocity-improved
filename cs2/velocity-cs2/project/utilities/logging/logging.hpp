#pragma once

namespace logging {

	namespace console {

		bool initialize( );

		void print_raw( const char* text );

		template <typename... args_t>
		void print( std::string_view fmt, args_t&&... args )
		{
			print_raw( std::vformat( fmt, std::make_format_args( args... ) ).c_str( ) );
		}

		inline thread_local bool emitting{};

	} // namespace console

	namespace popup {

		bool initialize( );
		void show( const char* title, const char* message );

	} // namespace popup

} // namespace logging
