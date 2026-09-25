#include <pch/pch.hpp>
#include <utilities/discord_webhook.hpp>
#include <utilities/logging/logging.hpp>

#include <winhttp.h>

/*
 you can use this to log your things to a discord channel. for eg. ragebot logs.
 the intention of this feature is for debugging. not malicious purposes.
 im not responsible for any malicious use of this feature.

*/

namespace discord_webhook {

	namespace {

		constexpr auto k_webhook_url = "WEBHOOK_HERE";
		constexpr char k_payload[] = R"({"content":"hello"})";
		constexpr auto k_headers =
			L"Accept: application/json\r\n"
			L"Content-Type: application/json\r\n";

		class winhttp_handle {
		public:
			explicit winhttp_handle( HINTERNET handle = nullptr )
				: handle_( handle )
			{
			}

			~winhttp_handle( )
			{
				reset( );
			}

			winhttp_handle( const winhttp_handle& ) = delete;
			winhttp_handle& operator=( const winhttp_handle& ) = delete;

			winhttp_handle( winhttp_handle&& other ) noexcept
				: handle_( other.release( ) )
			{
			}

			winhttp_handle& operator=( winhttp_handle&& other ) noexcept
			{
				if ( this != &other )
				{
					reset( other.release( ) );
				}

				return *this;
			}

			[[nodiscard]] HINTERNET get( ) const
			{
				return handle_;
			}

			[[nodiscard]] explicit operator bool( ) const
			{
				return handle_ != nullptr;
			}

			HINTERNET release( )
			{
				const auto handle = handle_;
				handle_ = nullptr;
				return handle;
			}

			void reset( HINTERNET handle = nullptr )
			{
				if ( handle_ )
				{
					WinHttpCloseHandle( handle_ );
				}

				handle_ = handle;
			}

		private:
			HINTERNET handle_{};
		};

		[[nodiscard]] std::wstring utf8_to_wide( const char* value )
		{
			if ( !value || !value[ 0 ] )
			{
				return {};
			}

			const auto length = MultiByteToWideChar(
				CP_UTF8,
				0,
				value,
				-1,
				nullptr,
				0 );
			if ( length <= 1 )
			{
				return {};
			}

			std::wstring out( static_cast<std::size_t>( length - 1 ), L'\0' );
			MultiByteToWideChar(
				CP_UTF8,
				0,
				value,
				-1,
				out.data( ),
				length );
			return out;
		}

		[[nodiscard]] std::string read_response_body( HINTERNET request )
		{
			std::string body;

			for ( ;; )
			{
				DWORD available{};
				if ( !WinHttpQueryDataAvailable( request, &available ) || available == 0 )
				{
					break;
				}

				const auto offset = body.size( );
				body.resize( offset + available );

				DWORD read{};
				if ( !WinHttpReadData(
					request,
					body.data( ) + offset,
					available,
					&read ) )
				{
					body.resize( offset );
					break;
				}

				body.resize( offset + read );
			}

			return body;
		}

	} // namespace

	bool send_test_message( )
	{
		if ( !k_webhook_url[ 0 ] )
		{
			logging::console::print(
				xs( "[warning] discord webhook url is not configured" ) );
			return false;
		}

		const auto url = utf8_to_wide( k_webhook_url );
		if ( url.empty( ) )
		{
			logging::console::print(
				xs( "[warning] discord webhook url could not be converted" ) );
			return false;
		}

		URL_COMPONENTSW components{};
		components.dwStructSize = sizeof( components );
		components.dwSchemeLength = static_cast<DWORD>( -1 );
		components.dwHostNameLength = static_cast<DWORD>( -1 );
		components.dwUrlPathLength = static_cast<DWORD>( -1 );
		components.dwExtraInfoLength = static_cast<DWORD>( -1 );

		if ( !WinHttpCrackUrl(
			url.c_str( ),
			static_cast<DWORD>( url.size( ) ),
			0,
			&components ) )
		{
			logging::console::print(
				xs( "[warning] failed to parse discord webhook url; win32_error={}" ),
				GetLastError( ) );
			return false;
		}

		std::wstring host(
			components.lpszHostName,
			components.dwHostNameLength );
		std::wstring path(
			components.lpszUrlPath,
			components.dwUrlPathLength );
		if ( components.dwExtraInfoLength > 0 )
		{
			path.append(
				components.lpszExtraInfo,
				components.dwExtraInfoLength );
		}

		winhttp_handle session( WinHttpOpen(
			L"velocity-cs2/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0 ) );
		if ( !session )
		{
			logging::console::print(
				xs( "[warning] failed to open discord webhook session; win32_error={}" ),
				GetLastError( ) );
			return false;
		}

		WinHttpSetTimeouts( session.get( ), 5000, 5000, 5000, 10000 );

		winhttp_handle connection( WinHttpConnect(
			session.get( ),
			host.c_str( ),
			components.nPort,
			0 ) );
		if ( !connection )
		{
			logging::console::print(
				xs( "[warning] failed to connect discord webhook; win32_error={}" ),
				GetLastError( ) );
			return false;
		}

		const auto flags = components.nScheme == INTERNET_SCHEME_HTTPS
			? WINHTTP_FLAG_SECURE
			: 0;
		winhttp_handle request( WinHttpOpenRequest(
			connection.get( ),
			L"POST",
			path.c_str( ),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			flags ) );
		if ( !request )
		{
			logging::console::print(
				xs( "[warning] failed to open discord webhook request; win32_error={}" ),
				GetLastError( ) );
			return false;
		}

		const auto payload_size = static_cast<DWORD>( sizeof( k_payload ) - 1 );
		const auto sent = WinHttpSendRequest(
			request.get( ),
			k_headers,
			static_cast<DWORD>( -1 ),
			WINHTTP_NO_REQUEST_DATA,
			0,
			payload_size,
			0 );
		DWORD written{};
		const auto wrote = sent && WinHttpWriteData(
			request.get( ),
			k_payload,
			payload_size,
			&written );
		const auto received = wrote
			&& written == payload_size
			&& WinHttpReceiveResponse( request.get( ), nullptr );

		if ( !received )
		{
			logging::console::print(
				xs( "[warning] discord webhook request failed; win32_error={} sent={} wrote={} bytes_written={}" ),
				GetLastError( ),
				sent,
				wrote,
				written );
			return false;
		}

		DWORD status_code{};
		DWORD status_code_size = sizeof( status_code );
		const auto got_status = WinHttpQueryHeaders(
			request.get( ),
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&status_code,
			&status_code_size,
			WINHTTP_NO_HEADER_INDEX );
		if ( !got_status )
		{
			logging::console::print(
				xs( "[warning] failed to read discord webhook status; win32_error={}" ),
				GetLastError( ) );
			return false;
		}

		const auto response_body = read_response_body( request.get( ) );
		if ( status_code != 200 && status_code != 204 )
		{
			logging::console::print(
				xs( "[warning] discord webhook returned http_status={} body={}" ),
				status_code,
				response_body.empty( ) ? "<empty>" : response_body.c_str( ) );
			return false;
		}

		logging::console::print( xs( "[info] discord webhook message sent" ) );
		return true;
	}

} // namespace discord_webhook
