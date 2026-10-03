#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

#include <array>
#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace features::changer::cosmetic_config
{
    struct item_skin_t;
}

namespace systems::model_preview
{
    enum class Presentation : std::uint8_t
    {
        Weapon = 0,
        Player
    };

    enum class Status : std::uint8_t
    {
        Uninitialized = 0,
        MissingPatterns,
        WaitingForRequest,
        WaitingForItem,
        WaitingForPanel,
        WaitingForTexture,
        Ready
    };

    struct Sticker
    {
        bool enabled = false;
        int kit = 0;
        float wear = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    struct Keychain
    {
        bool enabled = false;
        int id = 0;
        int seed = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
    };

    struct Request
    {
        Presentation presentation = Presentation::Weapon;
        std::uint16_t definitionIndex = 0;
        std::string modelPath;
        bool counterTerrorist = false;

        int paintKit = 0;
        int rarity = 0;
        int seed = 0;
        float wear = 0.0001f;

        bool statTrakEnabled = false;
        int statTrakKills = 0;
        std::string customName;

        std::array<Sticker, 5> stickers{};
        Keychain keychain{};
    };

    struct Frame
    {
        ID3D11ShaderResourceView* texture = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool valid = false;
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // Main/client thread only. Handles native temp-item creation and Panorama/V8.
    void ProcessMainThread();

    // Present/render thread only. Handles D3D11 SRV snapshot/promotion work.
    void TickRenderThread();

    // Generic preview request entry point.
    void Submit(const Request& request);
    void KeepAlive();
    void Close();
    void SetRotation(float rotationX, float rotationY);

    [[nodiscard]] Frame GetFrame();
    [[nodiscard]] Status GetStatus();
    [[nodiscard]] const char* GetStatusText();

    // Convenience request builders for the current skinchanger callers.
    [[nodiscard]] Request MakeWeaponRequest(std::uint16_t definitionIndex, const features::changer::cosmetic_config::item_skin_t& skin);
    [[nodiscard]] Request MakeAgentRequest(std::uint16_t definitionIndex);
}
