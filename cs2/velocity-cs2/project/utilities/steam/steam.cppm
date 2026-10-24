module;

#include "platform/platform.h"

export module velocity:utilities.steam.steam;

import <cstdint>;
import <vector>;

export {
namespace steam {

class Http {
 public:
  static bool Initialize();

  static std::uint32_t CreateGet(const char* url);
  static std::uint32_t CreatePost(const char* url, const char* content_type,
                                  const void* body, std::uint32_t body_size);

  static bool SetHeader(std::uint32_t req, const char* name, const char* value);
  static bool SetTimeout(std::uint32_t req, std::uint32_t seconds);

  static bool Send(std::uint32_t req, std::uintptr_t* out_call);
  static bool SendAndWait(std::uint32_t req, std::uint32_t timeout_ms);
  static bool SendAndForget(std::uint32_t req);

  static bool GetResponseBody(std::uint32_t req,
                              std::vector<std::uint8_t>& out_data);
  static void Release(std::uint32_t req);
};

class Friends {
 public:
  static bool Initialize();
  static int GetMediumFriendAvatar(std::uint64_t steam_id);
};

class User {
 public:
  static bool Initialize();
  static std::uint64_t GetSteamId();
};

class Utils {
 public:
  static bool Initialize();
  static bool GetImageSize(int image, std::uint32_t* width,
                           std::uint32_t* height);
  static bool GetImageRgba(int image, std::uint8_t* dest, int dest_size);
};

}  // namespace steam
}  // export
