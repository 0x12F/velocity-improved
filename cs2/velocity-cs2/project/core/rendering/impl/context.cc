module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;

namespace rendering {

bool Context::TryBindUiAssets() {
  if (this->m_ui_assets_ready_) {
    return true;
  }

  g_fonts.Initialize();

  this->m_ui_assets_ready_ = true;
  return true;
}

bool Context::Initialize(IDXGISwapChain* swap_chain) {
  if (this->m_initialized_) {
    return true;
  }

  if (FAILED(
          swap_chain->GetDevice(__uuidof(ID3D11Device),
                                reinterpret_cast<void**>(&this->m_device_)))) {
    return false;
  }

  this->m_device_->GetImmediateContext(&this->m_context_);

  DXGI_SWAP_CHAIN_DESC desc{};
  swap_chain->GetDesc(&desc);

  this->m_window_ = desc.OutputWindow;

  this->CreateRtv(swap_chain);
  this->SetupZdraw(this->m_window_);

  features::changer::runtime::InitializePreview(this->m_device_,
                                                this->m_context_);
  g_menu.InitializeGraphics();
  this->TryBindUiAssets();

  this->m_initialized_ = true;
  return true;
}

void Context::Shutdown() {
  if (this->m_rtv_) {
    this->m_rtv_->Release();
    this->m_rtv_ = nullptr;
  }

  if (this->m_context_) {
    this->m_context_->Release();
    this->m_context_ = nullptr;
  }

  if (this->m_device_) {
    this->m_device_->Release();
    this->m_device_ = nullptr;
  }

  this->m_window_ = nullptr;
  this->m_initialized_ = false;
}

void Context::OnPresent(IDXGISwapChain* swap_chain) {
  if (!this->m_initialized_) [[unlikely]] {
    if (!this->Initialize(swap_chain)) [[unlikely]] {
      return;
    }
  }

  this->TryBindUiAssets();
  features::changer::runtime::TickRender();
  features::misc::g_dlight.OnPresent();

  m_context_->OMSetRenderTargets(1, &this->m_rtv_, nullptr);

  xdraw::begin_frame(true);
  {
    auto& dl = xdraw::get(xdraw::layer::bottom);

    if (this->m_ui_assets_ready_ && systems::g_local.Get().IsValid() &&
        systems::g_view.HasCamera()) {
      features::misc::g_impacts.OnRenderEarly(dl);
      features::combat::g_misc.Antiaim().OnRender(dl);
      features::movement::g_edgebug.OnRender(dl);
      features::esp::item::g_overlay.OnRender(dl);
      features::esp::projectile::g_overlay.OnRender(
          dl, xdraw::get(xdraw::layer::middle));
      features::esp::player::g_overlay.OnRender(dl);
      features::misc::g_projectile_trajectory.OnRender(dl);
      features::combat::g_rage.OnRender(dl);
      features::combat::g_legit.OnRender(dl);
      features::misc::g_impacts.OnRender(dl);
      features::misc::g_hud.OnRender(dl);
      features::esp::other::g_overlay.OnRender(dl);
    }

    if (this->m_ui_assets_ready_) {
      g_widgets.Draw();
    }

    g_menu.Draw();
  }
  xdraw::end_frame();
  features::changer::PublishConfiguration();
  features::changer::runtime::SetInteractiveEdit(
      g_menu.IsOpen() &&
      (xui::ctx().active_slider != 0 || xui::ctx().active_text_input != 0 ||
       xui::ctx().active_slider_edit != 0));
}

void Context::OnResizeBuffers() {
  if (this->m_rtv_) {
    this->m_rtv_->Release();
    this->m_rtv_ = nullptr;
  }
}

void Context::OnResizeBuffersPost(IDXGISwapChain* swap_chain) {
  this->CreateRtv(swap_chain);
}

void Context::CreateRtv(IDXGISwapChain* swap_chain) {
  ID3D11Texture2D* back_buffer{nullptr};
  if (SUCCEEDED(
          swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&back_buffer)))) {
    this->m_device_->CreateRenderTargetView(back_buffer, nullptr,
                                            &this->m_rtv_);

    D3D11_TEXTURE2D_DESC back_buffer_desc{};
    back_buffer->GetDesc(&back_buffer_desc);

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(back_buffer_desc.Width);
    viewport.Height = static_cast<float>(back_buffer_desc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    this->m_context_->RSSetViewports(1, &viewport);

    back_buffer->Release();
  }
}

void Context::SetupZdraw(HWND window) {
  if (!xdraw::initialize(this->m_device_, this->m_context_)) {
    return;
  }

  xui::initialize(window);
}

}  // namespace rendering
