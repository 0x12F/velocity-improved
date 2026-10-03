#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/runtime.hpp>
#include "../../rendering.hpp"

namespace rendering {
    namespace {
        using item_system = features::changer::econ_item_system;
        namespace backend = features::changer::runtime;

        struct editor_state
        {
            bool expanded{true};
            int item_index{};
            int section{};
            int sticker_slot{};
            int sticker_index{};
            int last_subtab{-1};
            std::int16_t definition{};
            std::int16_t last_browsing_definition{};
            settings::changer::applied_skin skin;
            settings::changer::applied_skin saved_skin;
            std::string sticker_search;
            float yaw{};
            float pitch{};
            float zoom{1.0f};
            bool rotating{};
        };

        editor_state editor;

        class clipped_clicks
        {
        public:
            explicit clipped_clicks(const xui::rect& bounds) :
                input(xui::ctx().input), clicked(input.mouse_clicked),
                double_clicked(input.mouse_double_clicked), right_clicked(input.rmb_clicked)
            {
                // Native widgets test their own bounds, including portions scrolled
                // outside this child. Those portions must not consume browser clicks.
                if (!input.in_rect(bounds))
                    input.mouse_clicked = input.mouse_double_clicked = input.rmb_clicked = false;
            }

            ~clipped_clicks()
            {
                input.mouse_clicked = clicked;
                input.mouse_double_clicked = double_clicked;
                input.rmb_clicked = right_clicked;
            }

            clipped_clicks(const clipped_clicks&) = delete;
            clipped_clicks& operator=(const clipped_clicks&) = delete;

        private:
            xui::input_state& input;
            bool clicked;
            bool double_clicked;
            bool right_clicked;
        };

        bool toggle(std::string_view name, bool& value)
        {
            const auto label = std::format("{}: {}", name, value ? "on" : "off");
            if (!xui::button(label, xui::layout::item_width(), 22.0f))
                return false;
            value = !value;
            return true;
        }

        const std::vector<const item_system::item_def*>& items_for_tab(int tab)
        {
            const auto& econ = features::changer::g_econ_item_system;
            switch (tab)
            {
            case 1: return econ.knives();
            case 2: return econ.gloves();
            case 3: return econ.agents();
            default: return econ.guns();
            }
        }

        void equip_item(const item_system::item_def& item)
        {
            auto& skins = settings::g_changer.skins.data;
            if (item.category == item_system::item_category::knife || item.category == item_system::item_category::glove)
            {
                const auto& items = item.category == item_system::item_category::knife ?
                    features::changer::g_econ_item_system.knives() : features::changer::g_econ_item_system.gloves();
                for (const auto* other : items)
                    if (other->def_index != item.def_index)
                        skins.erase(other->def_index);
            }
            skins[item.def_index] = editor.skin;
            editor.saved_skin = editor.skin;
        }

        void draw_stickers()
        {
            static constexpr const char* slots[]{"1", "2", "3", "4", "5"};
            xui::combo("sticker slot", editor.sticker_slot, slots, 5);
            auto& sticker = editor.skin.stickers[editor.sticker_slot];
            toggle("sticker", sticker.enabled);
            xui::text_input("search stickers", editor.sticker_search, 64, "search...");

            const auto& kits = backend::sticker_kits();
            std::vector<const char*> names{"none"};
            std::vector<int> ids{0};
            auto search = editor.sticker_search;
            std::ranges::transform(search, search.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const auto& kit : kits)
            {
                auto name = kit.name;
                std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (kit.id == sticker.kit || search.empty() || name.find(search) != std::string::npos)
                {
                    names.push_back(kit.name.c_str());
                    ids.push_back(kit.id);
                }
            }
            // combo overlays hold the selection address until dismissed.
            const auto combo_id = xui::make_id("sticker kit");
            if (!xui::overlays::find(combo_id))
            {
                const auto found = std::ranges::find(ids, sticker.kit);
                editor.sticker_index = found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
            }
            if (xui::combo("sticker kit", editor.sticker_index, names.data(), static_cast<int>(names.size())) &&
                editor.sticker_index >= 0 && editor.sticker_index < static_cast<int>(ids.size()))
            {
                sticker.kit = ids[editor.sticker_index];
                sticker.enabled = sticker.kit != 0;
            }
            xui::slider_float("sticker wear", sticker.wear, 0.0f, 1.0f, "%.3f");
            xui::slider_float("sticker scale", sticker.scale, 0.1f, 5.0f);
            xui::slider_float("sticker rotation", sticker.rotation, -180.0f, 180.0f, "%.0f");
            xui::slider_float("sticker offset x", sticker.offsetX, -0.5f, 0.5f, "%.3f");
            xui::slider_float("sticker offset y", sticker.offsetY, -0.5f, 0.5f, "%.3f");
        }

        void draw_keychain()
        {
            auto& keychain = editor.skin.keychain;
            toggle("charm", keychain.enabled);
            xui::slider_int("charm id", keychain.id, 0, 10000);
            xui::slider_int("charm seed", keychain.seed, 0, 100000);
            xui::slider_float("charm offset x", keychain.offsetX, -20.0f, 20.0f, "%.3f");
            xui::slider_float("charm offset y", keychain.offsetY, -20.0f, 20.0f, "%.3f");
            xui::slider_float("charm offset z", keychain.offsetZ, -20.0f, 20.0f, "%.3f");
        }

        void draw_item_options(const item_system::item_def& item)
        {
            if (item.category == item_system::item_category::agent)
            {
                xui::text("equip agents in the browser below", tokens::col_text_dim);
                return;
            }
            static constexpr const char* sections[]{"finish", "stickers", "charm"};
            const bool gun = item.category == item_system::item_category::gun;
            xui::combo("options", editor.section, sections, gun ? 3 : 1);
            if (!gun)
                editor.section = 0;

            if (editor.section == 1)
                draw_stickers();
            else if (editor.section == 2)
                draw_keychain();
            else
            {
                xui::slider_float("wear##editor", editor.skin.wear, 0.0001f, 1.0f, "%.4f");
                xui::slider_int("seed##editor", editor.skin.seed, 0, 1000);
                if (item.category != item_system::item_category::glove)
                {
                    toggle("stattrak", editor.skin.stattrak);
                    xui::slider_int("stattrak kills", editor.skin.stattrak_kills, 0, 999999);
                    xui::text_input("name tag", editor.skin.custom_name, 63, "custom name...");
                }
                if (xui::button("apply item", xui::layout::item_width(), 22.0f))
                    equip_item(item);
                if (xui::button("refresh skins", xui::layout::item_width(), 22.0f))
                    backend::force_update();
            }
            if (editor.skin != editor.saved_skin)
                equip_item(item);
        }

        void draw_preview(const item_system::item_def& item, const xui::rect& area)
        {
            const bool editing = xui::ctx().active_slider != 0 || xui::ctx().active_text_input != 0 || xui::ctx().active_slider_edit != 0;
            backend::preview_item(item.def_index, features::changer::make_skin_configuration(editor.skin),
                item.category == item_system::item_category::agent, editing);

            auto& draw = xui::draw::current();
            const auto& input = xui::ctx().input;
            const bool hovered = input.in_rect(area) && !xui::ctx().overlay_blocking();
            if (hovered && input.mouse_clicked)
                editor.rotating = true;
            if (!input.mouse_down)
                editor.rotating = false;
            if (editor.rotating)
            {
                editor.yaw += input.mouse_delta_x() * 0.5f;
                editor.pitch = std::clamp(editor.pitch + input.mouse_delta_y() * 0.5f, -80.0f, 80.0f);
                systems::model_preview::SetRotation(editor.yaw, editor.pitch);
            }
            if (hovered)
                editor.zoom = std::clamp(editor.zoom + input.scroll_delta * 0.1f, 0.5f, 3.0f);
            if (hovered && input.mouse_double_clicked)
            {
                editor.yaw = editor.pitch = 0.0f;
                editor.zoom = 1.0f;
                systems::model_preview::SetRotation(0.0f, 0.0f);
            }

            draw.rect_filled(area.x, area.y, area.w, area.h, tokens::col_card, xdraw::corner_radius{6.0f});
            const auto frame = systems::model_preview::GetFrame();
            if (frame.valid && frame.texture && frame.width && frame.height)
            {
                const float scale = std::min(area.w / frame.width, area.h / frame.height) * editor.zoom;
                const float width = frame.width * scale;
                const float height = frame.height * scale;
                draw.push_clip(area.x, area.y, area.w, area.h);
                draw.image(area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height, frame.texture);
                draw.pop_clip();
            }
            else
            {
                const auto status = xui::truncate(systems::model_preview::GetStatusText(), area.w - 16.0f);
                draw.text(area.x + 8.0f, area.y + area.h * 0.5f, status, tokens::col_text_dim);
            }
            draw.text(area.x + 8.0f, area.bottom() - 20.0f, "drag to rotate / scroll to zoom", tokens::col_text_dim);
        }
    }

    float menu::draw_skin_editor(float x, float y, float width, float height, std::int16_t browsing_definition) const
    {
        constexpr float header_height = 32.0f;
        xui::layout::set_cursor(x - m_x, y - m_y);
        if (xui::button(editor.expanded ? "- skin options & preview" : "+ skin options & preview", width * 0.5f, 24.0f))
            editor.expanded = !editor.expanded;
        xui::layout::same_line();
        xui::checkbox("enabled##skinchanger", settings::g_changer.enabled);
        if (!editor.expanded)
        {
            backend::close_preview();
            return header_height;
        }

        const float panel_height = std::min(210.0f, height * 0.52f);
        const float column_width = (width - tokens::gap) * 0.5f;
        const auto& items = items_for_tab(m_subtab);
        if (items.empty())
        {
            backend::close_preview();
            return header_height;
        }

        if (editor.last_subtab != m_subtab)
        {
            editor.item_index = 0;
            editor.section = 0;
            editor.last_subtab = m_subtab;
            editor.last_browsing_definition = 0;
        }
        if (browsing_definition != editor.last_browsing_definition)
        {
            const auto found = std::ranges::find_if(items, [=](const auto* item) { return item->def_index == browsing_definition; });
            if (found != items.end())
                editor.item_index = static_cast<int>(found - items.begin());
            editor.last_browsing_definition = browsing_definition;
        }
        editor.item_index = std::clamp(editor.item_index, 0, static_cast<int>(items.size()) - 1);

        xui::layout::set_cursor(x - m_x, y + header_height - m_y);
        if (xui::begin_child(std::format("##skin_editor_options_{}", m_subtab), column_width, panel_height, true))
        {
            const clipped_clicks clip_input(xui::layout::current_window()->bounds);
            std::vector<const char*> names;
            for (const auto* item : items)
                names.push_back(item->localized_name.c_str());
            xui::combo("preview item", editor.item_index, names.data(), static_cast<int>(names.size()));
            const auto& item = *items[editor.item_index];
            const auto found = settings::g_changer.skins.data.find(item.def_index);
            const auto saved = found != settings::g_changer.skins.data.end() ? found->second : settings::changer::applied_skin{};
            if (editor.definition != item.def_index || editor.saved_skin != saved)
            {
                editor.skin = editor.saved_skin = saved;
                if (editor.definition != item.def_index)
                {
                    editor.definition = item.def_index;
                    editor.yaw = editor.pitch = 0.0f;
                    editor.zoom = 1.0f;
                    editor.rotating = false;
                    systems::model_preview::SetRotation(0.0f, 0.0f);
                }
            }
            draw_item_options(item);
            xui::end_child();
        }
        draw_preview(*items[editor.item_index], {x + column_width + tokens::gap, y + header_height, column_width, panel_height});
        return header_height + panel_height + tokens::gap;
    }
} // namespace rendering
