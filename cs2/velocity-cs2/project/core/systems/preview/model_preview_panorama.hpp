#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

#include <core/systems/preview/model_preview.hpp>

namespace systems::preview_panorama
{
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    void ProcessMainThread();
    void TickRenderThread();

    void Request(const systems::model_preview::Request& request);
    void SetRotation(float rotationX, float rotationY);
    void KeepAlive();
    void Close();

    [[nodiscard]] systems::model_preview::Frame GetFrame();
    [[nodiscard]] systems::model_preview::Status GetStatus();
    [[nodiscard]] const char* GetStatusText();
}
