module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cctype>;
import <charconv>;
import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace features::changer::runtime {
namespace {
class ItemTokens {
 public:
  explicit ItemTokens(std::string_view input) : m_input_(input) {}

  bool Next(std::string& token) {
    token.clear();
    SkipWhitespaceAndComments();

    if (m_position_ >= m_input_.size()) return false;

    const char c = m_input_[m_position_];

    if (c == '{' || c == '}') {
      token.assign(1, c);
      ++m_position_;
      return true;
    }

    if (c == '"') {
      ++m_position_;

      while (m_position_ < m_input_.size()) {
        const char current = m_input_[m_position_++];

        if (current == '"') return true;

        if (current == '\\' && m_position_ < m_input_.size()) {
          const char escaped = m_input_[m_position_++];

          switch (escaped) {
            case 'n':
              token.push_back('\n');
              break;
            case 'r':
              token.push_back('\r');
              break;
            case 't':
              token.push_back('\t');
              break;
            default:
              token.push_back(escaped);
              break;
          }

          continue;
        }

        token.push_back(current);
      }

      return !token.empty();
    }

    const std::size_t start = m_position_;

    while (m_position_ < m_input_.size()) {
      const char current = m_input_[m_position_];

      if (std::isspace(static_cast<unsigned char>(current)) || current == '{' ||
          current == '}')
        break;

      ++m_position_;
    }

    if (m_position_ == start) return false;

    token.assign(m_input_.substr(start, m_position_ - start));
    return true;
  }

 private:
  void SkipWhitespaceAndComments() {
    while (m_position_ < m_input_.size()) {
      while (m_position_ < m_input_.size() &&
             std::isspace(static_cast<unsigned char>(m_input_[m_position_])))
        ++m_position_;

      if (m_position_ + 1 >= m_input_.size() || m_input_[m_position_] != '/')
        return;

      if (m_input_[m_position_ + 1] == '/') {
        m_position_ += 2;

        while (m_position_ < m_input_.size() && m_input_[m_position_] != '\n')
          ++m_position_;

        continue;
      }

      if (m_input_[m_position_ + 1] == '*') {
        m_position_ += 2;

        while (m_position_ + 1 < m_input_.size() &&
               !(m_input_[m_position_] == '*' &&
                 m_input_[m_position_ + 1] == '/')) {
          ++m_position_;
        }

        if (m_position_ + 1 < m_input_.size()) m_position_ += 2;

        continue;
      }

      return;
    }
  }

  std::string_view m_input_;
  std::size_t m_position_ = 0;
};

int ParseId(const std::string& value) {
  int result = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  return error == std::errc{} && end == value.data() + value.size() ? result
                                                                    : 0;
}
std::string ReadItemDefinitions() {
  const auto fs = addresses::globals::file_system;
  if (!fs) return {};
  void* handle = memory::CallVfunc<void*>(
      fs, 13, "scripts/items/items_game.txt", "rb", "GAME");
  if (!handle) return {};
  struct CloseFile {
    std::uintptr_t fs;
    void* handle;
    ~CloseFile() { memory::CallVfunc<void>(fs, 14, handle); }
  } close{fs, handle};
  constexpr std::uint64_t kMaxItemsSize = 64 * 1024 * 1024;
  const auto size = memory::CallVfunc<std::uint64_t>(fs, 18, handle);
  if (!size || size > kMaxItemsSize) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  const auto count = memory::CallVfunc<int>(fs, 11, result.data(),
                                            static_cast<int>(size), handle);
  return count == static_cast<int>(size) ? result : std::string{};
}
}  // namespace

std::vector<StickerKit> LoadStickerCatalog() {
  const auto definitions = ReadItemDefinitions();
  ItemTokens tokens(definitions);
  std::string token;
  while (tokens.Next(token) && token != "sticker_kits") {
  }
  if (!tokens.Next(token) || token != "{") return {};
  std::vector<StickerKit> result;
  while (tokens.Next(token) && token != "}") {
    const int id = ParseId(token);
    if (!tokens.Next(token) || token != "{") continue;
    std::string name, localized_token, material;
    int depth = 1;
    while (depth > 0 && tokens.Next(token)) {
      if (token == "{") {
        ++depth;
        continue;
      }
      if (token == "}") {
        --depth;
        continue;
      }
      if (depth != 1) continue;
      const auto key = token;
      if (!tokens.Next(token)) break;
      if (token == "{") {
        ++depth;
        continue;
      }
      if (key == "name")
        name = token;
      else if (key == "item_name")
        localized_token = token;
      else if (key == "sticker_material")
        material = token;
    }
    if (id <= 0 || material.empty()) continue;
    if (!localized_token.empty() && addresses::globals::localize) {
      const char* token_name = localized_token.c_str();
      if (*token_name == '#') ++token_name;
      const char* localized = memory::CallVfunc<const char*>(
          addresses::globals::localize, 17, token_name);
      if (localized && *localized) name = localized;
    }
    constexpr std::string_view kPrefix = "Sticker | ";
    if (name.starts_with(kPrefix)) name.erase(0, kPrefix.size());
    result.push_back({id, std::move(name)});
  }
  std::ranges::sort(result, {}, &StickerKit::name);
  return result;
}
}  // namespace features::changer::runtime
