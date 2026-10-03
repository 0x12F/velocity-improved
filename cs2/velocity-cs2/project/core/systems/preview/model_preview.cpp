#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/systems/preview/model_preview.hpp>

#include <core/systems/preview/model_preview_item.hpp>
#include <core/systems/preview/model_preview_panorama.hpp>

namespace systems::model_preview
{
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        return systems::preview_panorama::Initialize(device, context);
    }

    void Shutdown()
    {
        systems::preview_panorama::Shutdown();
    }

    void ProcessMainThread()
    {
        systems::preview_item::ProcessPending();
        systems::preview_panorama::ProcessMainThread();
    }

    void TickRenderThread()
    {
        systems::preview_panorama::TickRenderThread();
    }

    void Submit(const Request& request)
    {
        systems::preview_panorama::Request(request);
    }

    void KeepAlive()
    {
        systems::preview_panorama::KeepAlive();
    }

    void Close()
    {
        systems::preview_panorama::Close();
    }

    void SetRotation(float rotationX, float rotationY)
    {
        systems::preview_panorama::SetRotation(rotationX, rotationY);
    }

    Frame GetFrame()
    {
        return systems::preview_panorama::GetFrame();
    }

    Status GetStatus()
    {
        return systems::preview_panorama::GetStatus();
    }

    const char* GetStatusText()
    {
        return systems::preview_panorama::GetStatusText();
    }

    Request MakeWeaponRequest(std::uint16_t definitionIndex, const features::changer::cosmetic_config::item_skin_t& skin)
    {
        Request request{};
        request.presentation = Presentation::Weapon;
        request.definitionIndex = definitionIndex;
        request.paintKit = skin.paintKit;
        request.seed = skin.seed;
        request.wear = skin.wear;
        request.statTrakEnabled = skin.statTrakEnabled;
        request.statTrakKills = skin.statTrakKills;
        request.customName = skin.customName;

        if (const features::changer::econ_item_system::paint_kit* paintKit = features::changer::g_econ_item_system.find_paint_kit(skin.paintKit))
            request.rarity = paintKit->rarity;

        for (std::size_t i = 0; i < request.stickers.size(); ++i)
        {
            const features::changer::cosmetic_config::skin_sticker_t& source = skin.stickers[i];
            Sticker& destination = request.stickers[i];

            destination.enabled = source.enabled;
            destination.kit = source.kit;
            destination.wear = source.wear;
            destination.scale = source.scale;
            destination.rotation = source.rotation;
            destination.offsetX = source.offsetX;
            destination.offsetY = source.offsetY;
        }

        request.keychain.enabled = skin.keychain.enabled;
        request.keychain.id = skin.keychain.id;
        request.keychain.seed = skin.keychain.seed;
        request.keychain.offsetX = skin.keychain.offsetX;
        request.keychain.offsetY = skin.keychain.offsetY;
        request.keychain.offsetZ = skin.keychain.offsetZ;

        return request;
    }

    Request MakeAgentRequest(std::uint16_t definitionIndex)
    {
        Request request{};
        request.presentation = Presentation::Player;
        request.definitionIndex = definitionIndex;

        if (const features::changer::econ_item_system::item_def* item = features::changer::g_econ_item_system.find_def(definitionIndex))
        {
            request.modelPath = item->model_player;
            request.counterTerrorist = item->team() == 3;
        }

        return request;
    }
}
