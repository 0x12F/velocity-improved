module;

#include "platform/platform.h"

#include <winhttp.h>

module velocity;

import <string_view>;

/*
 you can use this to log your things to a discord channel. for eg. ragebot logs.
 the intention of this feature is for debugging. not malicious purposes.
 im not responsible for any malicious use of this feature.

*/

namespace discord_webhook {

namespace {

constexpr auto kWebhookUrl = "WEBHOOK_HERE";
constexpr char kPayload[] = R"({"content":"hello"})";
constexpr auto kHeaders =
    L"Accept: application/json\r\n"
    L"Content-Type: application/json\r\n";

class WinhttpHandle {
 public:
  explicit WinhttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}

  ~WinhttpHandle() { Reset(); }

  WinhttpHandle(const WinhttpHandle&) = delete;
  WinhttpHandle& operator=(const WinhttpHandle&) = delete;

  WinhttpHandle(WinhttpHandle&& other) noexcept : handle_(other.Release()) {}

  WinhttpHandle& operator=(WinhttpHandle&& other) noexcept {
    if (this != &other) {
      Reset(other.Release());
    }

    return *this;
  }

  [[nodiscard]] HINTERNET Get() const { return handle_; }

  [[nodiscard]] explicit operator bool() const { return handle_ != nullptr; }

  HINTERNET Release() {
    const auto handle = handle_;
    handle_ = nullptr;
    return handle;
  }

  void Reset(HINTERNET handle = nullptr) {
    if (handle_) {
      WinHttpCloseHandle(handle_);
    }

    handle_ = handle;
  }

 private:
  HINTERNET handle_{};
};

[[nodiscard]] std::wstring Utf8ToWide(const char* value) {
  if (!value || !value[0]) {
    return {};
  }

  const auto length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
  if (length <= 1) {
    return {};
  }

  std::wstring out(static_cast<std::size_t>(length - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value, -1, out.data(), length);
  return out;
}

[[nodiscard]] std::string ReadResponseBody(HINTERNET request) {
  std::string body;

  for (;;) {
    DWORD available{};
    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
      break;
    }

    const auto offset = body.size();
    body.resize(offset + available);

    DWORD read{};
    if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
      body.resize(offset);
      break;
    }

    body.resize(offset + read);
  }

  return body;
}

}  // namespace

bool SendTestMessage() {
  if (!kWebhookUrl[0]) {
    logging::console::Print(
        xs("[warning] discord webhook url is not configured"));
    return false;
  }

  const auto url = Utf8ToWide(kWebhookUrl);
  if (url.empty()) {
    logging::console::Print(
        xs("[warning] discord webhook url could not be converted"));
    return false;
  }

  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0,
                       &components)) {
    logging::console::Print(
        xs("[warning] failed to parse discord webhook url; win32_error={}"),
        GetLastError());
    return false;
  }

  std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength > 0) {
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  WinhttpHandle session(
      WinHttpOpen(L"velocity-cs2/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    logging::console::Print(
        xs("[warning] failed to open discord webhook session; win32_error={}"),
        GetLastError());
    return false;
  }

  WinHttpSetTimeouts(session.Get(), 5000, 5000, 5000, 10000);

  WinhttpHandle connection(
      WinHttpConnect(session.Get(), host.c_str(), components.nPort, 0));
  if (!connection) {
    logging::console::Print(
        xs("[warning] failed to connect discord webhook; win32_error={}"),
        GetLastError());
    return false;
  }

  const auto flags =
      components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  WinhttpHandle request(WinHttpOpenRequest(
      connection.Get(), L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
  if (!request) {
    logging::console::Print(
        xs("[warning] failed to open discord webhook request; win32_error={}"),
        GetLastError());
    return false;
  }

  const auto payload_size = static_cast<DWORD>(sizeof(kPayload) - 1);
  const auto sent =
      WinHttpSendRequest(request.Get(), kHeaders, static_cast<DWORD>(-1),
                         WINHTTP_NO_REQUEST_DATA, 0, payload_size, 0);
  DWORD written{};
  const auto wrote =
      sent && WinHttpWriteData(request.Get(), kPayload, payload_size, &written);
  const auto received = wrote && written == payload_size &&
                        WinHttpReceiveResponse(request.Get(), nullptr);

  if (!received) {
    logging::console::Print(
        xs("[warning] discord webhook request failed; win32_error={} sent={} "
           "wrote={} bytes_written={}"),
        GetLastError(), sent, wrote, written);
    return false;
  }

  DWORD status_code{};
  DWORD status_code_size = sizeof(status_code);
  const auto got_status = WinHttpQueryHeaders(
      request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size,
      WINHTTP_NO_HEADER_INDEX);
  if (!got_status) {
    logging::console::Print(
        xs("[warning] failed to read discord webhook status; win32_error={}"),
        GetLastError());
    return false;
  }

  const auto response_body = ReadResponseBody(request.Get());
  if (status_code != 200 && status_code != 204) {
    logging::console::Print(
        xs("[warning] discord webhook returned http_status={} body={}"),
        status_code, response_body.empty() ? "<empty>" : response_body.c_str());
    return false;
  }

  logging::console::Print(xs("[info] discord webhook message sent"));
  return true;
}

}  // namespace discord_webhook
