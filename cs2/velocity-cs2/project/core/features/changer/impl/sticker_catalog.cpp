#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include "../runtime.hpp"
#include <charconv>
#include <cctype>

namespace features::changer::runtime {
namespace {
    class item_tokens
    {
    public:
        explicit item_tokens(std::string_view input) : m_input(input) {}

        bool Next(std::string& token)
        {
            token.clear();
            SkipWhitespaceAndComments();

            if (m_position >= m_input.size())
                return false;

            const char c = m_input[m_position];

            if (c == '{' || c == '}')
            {
                token.assign(1, c);
                ++m_position;
                return true;
            }

            if (c == '"')
            {
                ++m_position;

                while (m_position < m_input.size())
                {
                    const char current = m_input[m_position++];

                    if (current == '"')
                        return true;

                    if (current == '\\' && m_position < m_input.size())
                    {
                        const char escaped = m_input[m_position++];

                        switch (escaped)
                        {
                        case 'n': token.push_back('\n'); break;
                        case 'r': token.push_back('\r'); break;
                        case 't': token.push_back('\t'); break;
                        default: token.push_back(escaped); break;
                        }

                        continue;
                    }

                    token.push_back(current);
                }

                return !token.empty();
            }

            const std::size_t start = m_position;

            while (m_position < m_input.size())
            {
                const char current = m_input[m_position];

                if (std::isspace(static_cast<unsigned char>(current)) || current == '{' || current == '}')
                    break;

                ++m_position;
            }

            if (m_position == start)
                return false;

            token.assign(m_input.substr(start, m_position - start));
            return true;
        }

    private:
        void SkipWhitespaceAndComments()
        {
            while (m_position < m_input.size())
            {
                while (m_position < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_position])))
                    ++m_position;

                if (m_position + 1 >= m_input.size() || m_input[m_position] != '/')
                    return;

                if (m_input[m_position + 1] == '/')
                {
                    m_position += 2;

                    while (m_position < m_input.size() && m_input[m_position] != '\n')
                        ++m_position;

                    continue;
                }

                if (m_input[m_position + 1] == '*')
                {
                    m_position += 2;

                    while (m_position + 1 < m_input.size() &&
                        !(m_input[m_position] == '*' && m_input[m_position + 1] == '/'))
                    {
                        ++m_position;
                    }

                    if (m_position + 1 < m_input.size())
                        m_position += 2;

                    continue;
                }

                return;
            }
        }

        std::string_view m_input;
        std::size_t m_position = 0;
    };

    int parse_id(const std::string& value)
    {
        int result = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size() ? result : 0;
    }
    std::string read_item_definitions()
    {
        const auto fs = addresses::globals::file_system;
        if (!fs) return {};
        void* handle = memory::call_vfunc<void*>(fs, 13, "scripts/items/items_game.txt", "rb", "GAME");
        if (!handle) return {};
        struct close_file {
            std::uintptr_t fs;
            void* handle;
            ~close_file() { memory::call_vfunc<void>(fs, 14, handle); }
        } close{fs, handle};
        constexpr std::uint64_t max_items_size = 64 * 1024 * 1024;
        const auto size = memory::call_vfunc<std::uint64_t>(fs, 18, handle);
        if (!size || size > max_items_size) return {};
        std::string result(static_cast<std::size_t>(size), '\0');
        const auto count = memory::call_vfunc<int>(fs, 11, result.data(), static_cast<int>(size), handle);
        return count == static_cast<int>(size) ? result : std::string{};
    }
}

std::vector<sticker_kit> load_sticker_catalog()
{
    const auto definitions = read_item_definitions();
    item_tokens tokens(definitions);
    std::string token;
    while (tokens.Next(token) && token != "sticker_kits") {}
    if (!tokens.Next(token) || token != "{") return {};
    std::vector<sticker_kit> result;
    while (tokens.Next(token) && token != "}")
    {
        const int id = parse_id(token);
        if (!tokens.Next(token) || token != "{") continue;
        std::string name, localized_token, material;
        int depth = 1;
        while (depth > 0 && tokens.Next(token))
        {
            if (token == "{") { ++depth; continue; }
            if (token == "}") { --depth; continue; }
            if (depth != 1) continue;
            const auto key = token;
            if (!tokens.Next(token)) break;
            if (token == "{") { ++depth; continue; }
            if (key == "name") name = token;
            else if (key == "item_name") localized_token = token;
            else if (key == "sticker_material") material = token;
        }
        if (id <= 0 || material.empty()) continue;
        if (!localized_token.empty() && addresses::globals::localize)
        {
            const char* token_name = localized_token.c_str();
            if (*token_name == '#') ++token_name;
            const char* localized = memory::call_vfunc<const char*>(addresses::globals::localize, 17, token_name);
            if (localized && *localized) name = localized;
        }
        constexpr std::string_view prefix = "Sticker | ";
        if (name.starts_with(prefix)) name.erase(0, prefix.size());
        result.push_back({id, std::move(name)});
    }
    std::ranges::sort(result, {}, &sticker_kit::name);
    return result;
}
}
