#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../steam.hpp"

namespace steam {

	namespace detail {

		inline std::uintptr_t http {};
		inline std::uintptr_t utils {};

		struct http_request_completed {
			std::uint32_t m_request;
			std::uintptr_t m_context_value;
			bool m_request_successful;
			int m_status_code;
			std::uint32_t m_body_size;
		};

		enum http_method {
			http_method_get = 1,
			http_method_post = 3
		};

		static bool wait_for_call (std::uintptr_t call, std::uint32_t timeout_ms) {
			auto start = std::chrono::steady_clock::now ();
			auto timeout = std::chrono::milliseconds (timeout_ms);

			while (true) {
				if (std::chrono::steady_clock::now () - start > timeout) {
					return false;
				}

				auto failed {false};

				if (memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamUtils_IsAPICallCompleted"), utils, call, &failed)) {
					if (failed) {
						return false;
					}

					auto result = http_request_completed {};
					auto got_result = memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamUtils_GetAPICallResult"), utils, call, &result, sizeof (result), 2101, &failed);

					return got_result && !failed && result.m_request_successful;
				}

				std::this_thread::sleep_for (std::chrono::milliseconds (10));
			}
		}

	} // namespace detail

	bool http::initialize () {
		detail::http = memory::call<std::uintptr_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_SteamHTTP_v003"));
		detail::utils = memory::call<std::uintptr_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_SteamUtils_v010"));

		return detail::http != 0 && detail::utils != 0;
	}

	std::uint32_t http::create_get (const char* url) {
		return memory::call<std::uint32_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_CreateHTTPRequest"), detail::http, detail::http_method_get, url);
	}

	std::uint32_t http::create_post (const char* url, const char* content_type, const void* body, std::uint32_t body_size) {
		const auto req = memory::call<std::uint32_t> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_CreateHTTPRequest"), detail::http, detail::http_method_post, url);
		if (req) {
			memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_SetHTTPRequestRawPostBody"), detail::http, req, content_type, (std::uint8_t*) body, body_size);
		}

		return req;
	}

	bool http::set_header (std::uint32_t req, const char* name, const char* value) {
		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_SetHTTPRequestHeaderValue"), detail::http, req, name, value);
	}

	bool http::set_timeout (std::uint32_t req, std::uint32_t seconds) {
		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_SetHTTPRequestNetworkActivityTimeout"), detail::http, req, seconds);
	}

	bool http::send (std::uint32_t req, std::uintptr_t* out_call) {
		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_SendHTTPRequest"), detail::http, req, out_call);
	}

	bool http::send_and_wait (std::uint32_t req, std::uint32_t timeout_ms) {
		auto call {0ull};

		if (!send (req, &call)) {
			return false;
		}

		return detail::wait_for_call (call, timeout_ms);
	}

	bool http::send_and_forget (std::uint32_t req) {
		if (!req) {
			return false;
		}

		auto call {0ull};
		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_SendHTTPRequest"), detail::http, req, &call);
	}

	bool http::get_response_body (std::uint32_t req, std::vector<std::uint8_t>& out_data) {
		auto size {0u};

		if (!memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_GetHTTPResponseBodySize"), detail::http, req, &size) || size == 0) {
			return false;
		}

		out_data.resize (size);

		return memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_GetHTTPResponseBodyData"), detail::http, req, out_data.data (), size);
	}

	void http::release (std::uint32_t req) {
		memory::call<bool> (MODULE_EXPORT ("steam_api64.dll:SteamAPI_ISteamHTTP_ReleaseHTTPRequest"), detail::http, req);
	}

} // namespace steam