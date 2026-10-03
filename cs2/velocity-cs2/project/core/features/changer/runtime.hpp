#pragma once

#include <core/features/changer/cosmetic_configuration.hpp>
#include <core/systems/preview/model_preview.hpp>
#include <string>
#include <vector>

namespace features::changer::runtime {

    struct sticker_kit
    {
        int id{};
        std::string name;
    };

    bool initialize();
    void initialize_preview(ID3D11Device* device, ID3D11DeviceContext* context);
    void shutdown();
    void before_frame_stage(int stage);
    void after_frame_stage(int stage);
    void tick_render();

    // Published by the render thread; consumed only on the client thread.
    void configure(features::changer::cosmetic_config::skin_changer_t config, std::uint16_t ct_agent, std::uint16_t t_agent);
    void set_interactive_edit(bool active);
    void force_update();
    const std::vector<sticker_kit>& sticker_kits();

    void preview_item(std::uint16_t definition, const features::changer::cosmetic_config::item_skin_t& skin, bool agent, bool editing);
    void close_preview();

} // namespace features::changer::runtime
