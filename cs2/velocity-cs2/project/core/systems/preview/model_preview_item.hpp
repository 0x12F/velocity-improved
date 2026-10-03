#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

#include <cstdint>
#include <string>

#include <core/systems/preview/model_preview.hpp>

namespace systems::preview_item
{
    struct Result
    {
        std::uint64_t itemId = 0;
        std::uint16_t definitionIndex = 0;
        std::string modelPath;
        bool counterTerrorist = false;
        int paintKit = 0;
        std::uint64_t revision = 0;
        bool complexCosmetics = false;
        systems::model_preview::Presentation presentation = systems::model_preview::Presentation::Weapon;

        bool valid = false;
        bool current = false;
    };

    bool Initialize();
    void Shutdown();

    // Render/UI thread only. Serializes and copies request data, no game econ calls.
    void Request(const systems::model_preview::Request& request);

    // Main/client thread only.
    void ProcessPending();
    void ReleaseForNextRequest();

    [[nodiscard]] bool IsResultLocked();
    [[nodiscard]] std::uint64_t GetLatestRevision();
    [[nodiscard]] Result GetResult();
}
