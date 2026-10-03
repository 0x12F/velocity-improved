#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace features::changer::agent_models {
    inline std::string lowercase(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    inline std::string family_for(std::string_view agent, std::string_view fallback)
    {
        constexpr std::string_view families[]{
            "tm_jungle_raider", "tm_phoenix_heavy", "tm_professional", "tm_separatist",
            "tm_anarchist", "tm_jumpsuit", "tm_phoenix", "tm_balkan", "tm_pirate", "tm_leet",
            "ctm_gendarmerie", "ctm_heavy", "ctm_diver", "ctm_gign", "ctm_gsg9",
            "ctm_swat", "ctm_sas", "ctm_st6", "ctm_fbi", "ctm_idf"
        };
        for (const auto family : families)
            if (agent.starts_with(family))
                return std::string(family);

        const auto last = fallback.find_last_of("/\\");
        if (last == std::string_view::npos || last == 0)
            return {};
        const auto parent = fallback.find_last_of("/\\", last - 1);
        const auto begin = parent == std::string_view::npos ? 0 : parent + 1;
        const auto family = fallback.substr(begin, last - begin);
        return family.starts_with("tm_") || family.starts_with("ctm_") ? std::string(family) : std::string{};
    }

    // Preserve the donor's agent-specific resolution without a second item catalog.
    template<typename ModelExists>
    std::string resolve(std::string_view item, const std::string& fallback, const std::string& display, ModelExists exists)
    {
        constexpr std::string_view prefix = "customplayer_";
        const bool fallback_exists = !fallback.empty() && exists(fallback);
        if (!item.starts_with(prefix))
            return fallback_exists ? fallback : std::string{};

        const auto agent = item.substr(prefix.size());
        const auto slash = fallback.find_last_of("/\\");
        auto stem = fallback.substr(slash == std::string::npos ? 0 : slash + 1);
        if (stem.ends_with(".vmdl"))
            stem.resize(stem.size() - 5);
        if (fallback_exists && lowercase(stem) == lowercase(std::string(agent)))
            return fallback;

        const auto family = family_for(agent, fallback);
        if (!family.empty())
        {
            const auto model = "agents/models/" + family + "/" + std::string(agent) + ".vmdl";
            if (exists(model))
                return model;
        }

        // Generic faction fallbacks can make distinct legacy agents look identical.
        const auto name = lowercase(display);
        const bool default_agent = item == "customplayer_t_map_based" || item == "customplayer_ct_map_based"
            || name == "default t agent" || name == "default ct agent" || name == "default agent";
        return !default_agent && fallback_exists ? fallback : std::string{};
    }
}
