#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include "../runtime.hpp"

namespace features::changer {

    features::changer::cosmetic_config::item_skin_t make_skin_configuration(const settings::changer::applied_skin& skin)
    {
        features::changer::cosmetic_config::item_skin_t result;
        result.enabled = true;
        result.paintKit = skin.paint_kit_id;
        result.seed = skin.seed;
        result.wear = skin.wear;
        result.statTrakEnabled = skin.stattrak;
        result.statTrakKills = skin.stattrak_kills;
        result.customName = skin.custom_name;
        result.stickers = skin.stickers;
        result.keychain = skin.keychain;
        return result;
    }

    void publish_configuration()
    {
        // UI and preset loads live on the render thread. The backend never reads
        // these mutable maps directly from FrameStageNotify.
        static std::unordered_map<std::int16_t, settings::changer::applied_skin> previous;
        static int previous_ct = -1;
        static int previous_t = -1;
        static bool previous_enabled{};
        const auto& settings = settings::g_changer;
        const auto& skins = settings.skins.data;
        if (previous == skins && previous_ct == settings.agents.ct_def &&
            previous_t == settings.agents.t_def && previous_enabled == settings.enabled.value)
            return;

        features::changer::cosmetic_config::skin_changer_t next;
        next.enabled = settings.enabled.value;
        for (const auto& [definition, skin] : skins)
        {
            const auto* item = g_econ_item_system.find_def(definition);
            if (!item)
                continue;
            auto converted = make_skin_configuration(skin);
            switch (item->category)
            {
            case econ_item_system::item_category::gun:
                next.weapons.emplace(definition, std::move(converted));
                break;
            case econ_item_system::item_category::knife:
                next.knives.emplace(definition, std::move(converted));
                next.knifeDefinition = definition;
                break;
            case econ_item_system::item_category::glove:
                next.gloves.emplace(definition, std::move(converted));
                next.gloveDefinition = definition;
                break;
            default:
                break;
            }
        }
        runtime::configure(std::move(next), settings.agents.ct_def, settings.agents.t_def);
        previous = skins;
        previous_ct = settings.agents.ct_def;
        previous_t = settings.agents.t_def;
        previous_enabled = settings.enabled.value;
    }
} // namespace features::changer
