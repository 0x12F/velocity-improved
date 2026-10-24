module;

#include "platform/platform.h"


#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <string_view>;
import <type_traits>;

namespace steam {

namespace detail {

inline std::uintptr_t http{};
inline std::uintptr_t utils{};

struct HttpRequestCompleted {
  std::uint32_t m_request;
  std::uintptr_t m_context_value;
  bool m_request_successful;
  int m_status_code;
  std::uint32_t m_body_size;
};

enum HttpMethod { kHttpMethodGet = 1, kHttpMethodPost = 3 };

static bool WaitForCall(std::uintptr_t call, std::uint32_t timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  auto timeout = std::chrono::milliseconds(timeout_ms);

  while (true) {
    if (std::chrono::steady_clock::now() - start > timeout) {
      return false;
    }

    auto failed{false};

    if (memory::Call<bool>(
            MODULE_EXPORT(
                "steam_api64.dll:SteamAPI_ISteamUtils_IsAPICallCompleted"),
            utils, call, &failed)) {
      if (failed) {
        return false;
      }

      auto result = HttpRequestCompleted{};
      auto got_result = memory::Call<bool>(
          MODULE_EXPORT(
              "steam_api64.dll:SteamAPI_ISteamUtils_GetAPICallResult"),
          utils, call, &result, sizeof(result), 2101, &failed);

      return got_result && !failed && result.m_request_successful;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace detail

bool Http::Initialize() {
  detail::http = memory::Call<std::uintptr_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamHTTP_v003"));
  detail::utils = memory::Call<std::uintptr_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamUtils_v010"));

  return detail::http != 0 && detail::utils != 0;
}

std::uint32_t Http::CreateGet(const char* url) {
  return memory::Call<std::uint32_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_CreateHTTPRequest"),
      detail::http, detail::kHttpMethodGet, url);
}

std::uint32_t Http::CreatePost(const char* url, const char* content_type,
                               const void* body, std::uint32_t body_size) {
  const auto req = memory::Call<std::uint32_t>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_CreateHTTPRequest"),
      detail::http, detail::kHttpMethodPost, url);
  if (req) {
    memory::Call<bool>(
        MODULE_EXPORT(
            "steam_api64.dll:SteamAPI_ISteamHTTP_SetHTTPRequestRawPostBody"),
        detail::http, req, content_type, (std::uint8_t*)body, body_size);
  }

  return req;
}

bool Http::SetHeader(std::uint32_t req, const char* name, const char* value) {
  return memory::Call<bool>(
      MODULE_EXPORT(
          "steam_api64.dll:SteamAPI_ISteamHTTP_SetHTTPRequestHeaderValue"),
      detail::http, req, name, value);
}

bool Http::SetTimeout(std::uint32_t req, std::uint32_t seconds) {
  return memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_"
                    "SetHTTPRequestNetworkActivityTimeout"),
      detail::http, req, seconds);
}

bool Http::Send(std::uint32_t req, std::uintptr_t* out_call) {
  return memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_SendHTTPRequest"),
      detail::http, req, out_call);
}

bool Http::SendAndWait(std::uint32_t req, std::uint32_t timeout_ms) {
  auto call{0ull};

  if (!Send(req, &call)) {
    return false;
  }

  return detail::WaitForCall(call, timeout_ms);
}

bool Http::SendAndForget(std::uint32_t req) {
  if (!req) {
    return false;
  }

  auto call{0ull};
  return memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_SendHTTPRequest"),
      detail::http, req, &call);
}

bool Http::GetResponseBody(std::uint32_t req,
                           std::vector<std::uint8_t>& out_data) {
  auto size{0u};

  if (!memory::Call<bool>(
          MODULE_EXPORT(
              "steam_api64.dll:SteamAPI_ISteamHTTP_GetHTTPResponseBodySize"),
          detail::http, req, &size) ||
      size == 0) {
    return false;
  }

  out_data.resize(size);

  return memory::Call<bool>(
      MODULE_EXPORT(
          "steam_api64.dll:SteamAPI_ISteamHTTP_GetHTTPResponseBodyData"),
      detail::http, req, out_data.data(), size);
}

void Http::Release(std::uint32_t req) {
  memory::Call<bool>(
      MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamHTTP_ReleaseHTTPRequest"),
      detail::http, req);
}

}  // namespace steam
