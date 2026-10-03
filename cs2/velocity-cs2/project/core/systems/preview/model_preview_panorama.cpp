#include <pch/pch.hpp>
#include <utilities/hooking/hooking.hpp>
#include "layouts.hpp"
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/systems/preview/model_preview_panorama.hpp>

#include <core/systems/preview/model_preview_item.hpp>
#include <core/features/changer/skinchanger.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <Windows.h>
#include <d3d11.h>

namespace
{
    bool install_hook(hooking::jmp& hook, void* target, void* detour)
    {
        if (hook.create(target, detour) && hook.enable()) return true;
        hook.reset();
        return false;
    }

    // Panorama panel/resource names.
    constexpr const char* PANEL_ID_PREFIX = "velocity_model_preview";
    constexpr const char* TEXTURE_NAME_PREFIX = "velocity_model_preview_texture";

    // CreateRenderTargets only runs when a target needs realizing. The optional
    // AcquireLayerRT hook also lets us reacquire an existing target after reinjection.
    constexpr std::uint32_t PREVIEW_PANEL_WIDTH = 1024;
    constexpr std::uint32_t PREVIEW_PANEL_HEIGHT = 768;

    // Fallback cleanup if the menu lifecycle does not explicitly close the preview.
    constexpr ULONGLONG REQUEST_TIMEOUT_MS = 5000;

    // Keep the old frame visible while the replacement composite finishes building.
    constexpr ULONGLONG SIMPLE_SWAP_WARMUP_MS = 170;
    constexpr ULONGLONG COMPLEX_SWAP_WARMUP_MS = 340;

    // Let the old scene retire before its temporary item slot is reused.
    constexpr ULONGLONG SIMPLE_RETIRE_GRACE_MS = 100;
    constexpr ULONGLONG COMPLEX_RETIRE_GRACE_MS = 220;

    struct PanoramaUiPanel;
    struct PanoramaUiEngine;

    struct PanoramaPanel2D
    {
        const void* vmt = nullptr;
        PanoramaUiPanel* uiPanel = nullptr;
    };

    using RunScriptFn = void(__fastcall*)(PanoramaUiEngine*, PanoramaUiPanel*, const char*, const char*, std::uint64_t);
    using CreatePanoramaRenderTargetsFn = void(__fastcall*)(void*);
    using AcquirePanoramaLayerRTFn = void* (__fastcall*)(void*, void*);

    // Extra arguments are forwarded defensively. On Win64, passing unused register
    // arguments is harmless if the current constructor only consumes RCX=this.
    using PreviewPlayerConstructorFn = void* (__fastcall*)(void*, void*, void*, void*);
    using PreviewStickerInModelPanelFn = void(__fastcall*)(void*, const char*, int, void*);
    using ReleaseCompositeMaterialsFn = void(__fastcall*)(features::changer::engine::composite_material_owner*, bool);

    hooking::jmp g_CreatePanoramaRenderTargetsHook{};
    hooking::jmp g_AcquirePanoramaLayerRTHook{};
    hooking::jmp g_PreviewPlayerConstructorHook{};
    hooking::jmp g_PreviewStickerInModelPanelHook{};

    // PreviewStickerInModelPanel exposes the actual CUI_Item3dPanel pointer. We keep
    // the latest weapon panel so a 1 -> 0 sticker transition can release the stale
    // composite on the currently displayed preview entity before the replacement builds.
    std::atomic<void*> g_Item3dPanel{ nullptr };
    std::atomic_int g_PanelCaptureStickerSlot{ -1 };
    std::atomic_bool g_PreviewPanelCaptureArmed{ false };
    std::atomic_bool g_ZeroStickerReleasePending{ false };
    std::atomic_int g_LastEffectiveStickerCount{ 0 };
    std::atomic<std::uint16_t> g_LastStickerDefinitionIndex{ 0 };
    ReleaseCompositeMaterialsFn g_ReleaseCompositeMaterials = nullptr;

    ID3D11Device* g_Device = nullptr;
    ID3D11DeviceContext* g_Context = nullptr;

    PanoramaUiEngine** g_UiEnginePointer = nullptr;
    PanoramaPanel2D** g_MainMenuPanelPointer = nullptr;
    PanoramaPanel2D** g_HudPanelPointer = nullptr;
    RunScriptFn g_RunScript = nullptr;

    std::atomic_bool g_Initialized{ false };
    std::atomic<systems::model_preview::Status> g_Status{ systems::model_preview::Status::Uninitialized };

    std::atomic<std::uint16_t> g_RequestedDefinitionIndex{ 0 };
    std::atomic<systems::model_preview::Presentation> g_RequestedPresentation{ systems::model_preview::Presentation::Weapon };
    std::atomic<ULONGLONG> g_LastRequestAt{ 0 };

    // Stable/displayed panel.
    PanoramaUiPanel* g_CurrentContext = nullptr;
    std::uint16_t g_AppliedDefinitionIndex = 0;
    std::uint64_t g_AppliedItemId = 0;
    std::uint64_t g_AppliedRevision = 0;
    bool g_AppliedComplexCosmetics = false;
    systems::model_preview::Presentation g_AppliedPresentation = systems::model_preview::Presentation::Weapon;

    std::uint64_t g_PanelSessionId = 0;
    char g_CurrentPanelId[96]{};
    std::atomic_bool g_PanelActive{ false };
    std::atomic_bool g_CurrentPanelParked{ false };

    // Hidden candidate panel.
    PanoramaUiPanel* g_BuildContext = nullptr;
    char g_BuildPanelId[96]{};
    char g_BuildTextureName[96]{};
    std::uint16_t g_BuildDefinitionIndex = 0;
    std::uint64_t g_BuildItemId = 0;
    std::atomic_uint64_t g_BuildRevision{ 0 };
    bool g_BuildComplexCosmeticsValue = false;
    systems::model_preview::Presentation g_BuildPresentation = systems::model_preview::Presentation::Weapon;
    std::atomic_bool g_BuildComplexCosmetics{ false };
    std::atomic_bool g_BuildActive{ false };
    std::atomic_bool g_BuildPromoted{ false };

    ULONGLONG g_RetireReleaseAt = 0;

    // Freeze old pixels before the candidate touches the shared vanity scene.
    std::atomic_bool g_FreezeRequested{ false };
    std::atomic_bool g_FreezeComplete{ false };
    std::atomic_bool g_ResetFrameRequested{ false };

    std::atomic_uint32_t g_ActiveGeneration{ 0 };

    // Raw Source2 ResourceBinding_t*. Panorama owns the strong handle. We never
    // mutate its refcount; it is only used as a short pointer chain to the D3D11 SRV.
    std::atomic<void*> g_PreviewBinding{ nullptr };

    // Unlike ResourceBinding_t, the published preview SRV is COM-refcounted by us.
    std::atomic<ID3D11ShaderResourceView*> g_CurrentTexture{ nullptr };
    std::atomic_uint32_t g_CurrentTextureWidth{ 0 };
    std::atomic_uint32_t g_CurrentTextureHeight{ 0 };
    std::atomic<systems::model_preview::Presentation> g_PublishedPresentation{ systems::model_preview::Presentation::Weapon };
    std::atomic_bool g_PublishedPresentationValid{ false };

    // A newly-created MapItemPreviewPanel can briefly render the stock weapon or an
    // empty item while its custom material is being precached/composited. Capture the
    // new SRV immediately, but publish it to the menu only after a short warm-up.
    std::atomic<ID3D11ShaderResourceView*> g_StagedTexture{ nullptr };
    std::atomic_uint32_t g_StagedTextureWidth{ 0 };
    std::atomic_uint32_t g_StagedTextureHeight{ 0 };
    std::atomic<ULONGLONG> g_StagedTextureAt{ 0 };
    std::atomic_uint32_t g_StagedGeneration{ 0 };
    std::atomic_uint32_t g_PromotedGeneration{ 0 };

    std::atomic<float> g_RotationX{ 0.0f };
    std::atomic<float> g_RotationY{ 0.0f };
    std::atomic_bool g_RotationDirty{ false };

    // Panorama preview players are client-only and do not appear in the normal
    // GameEntity enumeration. Keep only the newest constructor-captured player. Old
    // preview players are destroyed/reused when the selected model changes, so retaining
    // a ring of historical pointers eventually writes into retired objects.
    std::atomic<void*> g_CurrentPreviewPlayer{ nullptr };
    std::atomic<void*> g_CurrentPreviewPlayerVtable{ nullptr };
    std::atomic_uint32_t g_CurrentPreviewPlayerGeneration{ 0 };
    std::atomic<ULONGLONG> g_CurrentPreviewPlayerCapturedAt{ 0 };

    // The state below is consumed only from the main-thread preview update. A new
    // constructor generation resets it before that player is touched.
    std::uint32_t g_PlayerRotationObservedGeneration = 0;
    void* g_PlayerRotationSceneNode = nullptr;
    float g_PlayerPreviewBaseYaw = 0.0f;
    bool g_PlayerPreviewBaseYawValid = false;
    float g_PlayerPreviewLastTargetYaw = 0.0f;
    bool g_PlayerPreviewLastTargetYawValid = false;

    // Wait for Valve to establish the preview player's native facing before treating it
    // as the baseline for user rotation.
    constexpr ULONGLONG PLAYER_ROTATION_MIN_SETTLE_MS = 220;
    constexpr ULONGLONG PLAYER_ROTATION_STABLE_MS = 80;
    constexpr ULONGLONG PLAYER_ROTATION_ZERO_FALLBACK_MS = 550;
    constexpr float PLAYER_ROTATION_STABLE_EPSILON = 0.15f;
    ULONGLONG g_PlayerRotationStableSince = 0;
    float g_PlayerRotationStableYaw = 0.0f;
    bool g_PlayerRotationStableYawValid = false;

    float NormalizePreviewYaw(float yaw)
    {
        while (yaw > 180.0f)
            yaw -= 360.0f;

        while (yaw < -180.0f)
            yaw += 360.0f;

        return yaw;
    }

    struct PlayerSceneNodeRotationData
    {
        features::changer::engine::scene_node* sceneNode = nullptr;
        float localYaw = 0.0f;
        float absoluteYaw = 0.0f;
        std::uint8_t dirty = 0;
    };

    bool TryReadPlayerSceneNodeRotationData(void* entity, PlayerSceneNodeRotationData& data)
    {
        data = {};

        __try
        {
            data.sceneNode = static_cast<features::changer::engine::base_entity*>(entity)->m_pGameSceneNode();

            if (!data.sceneNode)
                return false;

            data.localYaw = data.sceneNode->m_angWrappedLocalRotation().y;
            data.absoluteYaw = data.sceneNode->m_angAbsRotation().y;
            data.dirty = data.sceneNode->TransformDirty();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            data = {};
            return false;
        }
    }

    bool TryWritePlayerSceneNodeYaw(features::changer::engine::scene_node* sceneNode, float targetYaw)
    {
        if (!sceneNode)
            return false;

        __try
        {
            sceneNode->m_angWrappedLocalRotation().y = targetYaw;
            sceneNode->TransformDirty() = 1;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryReadObjectVtable(void* object, void*& vtable)
    {
        vtable = nullptr;

        if (!object)
            return false;

        __try
        {
            vtable = *reinterpret_cast<void**>(object);
            return vtable != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtable = nullptr;
            return false;
        }
    }

    void ResetCurrentPreviewPlayerCapture()
    {
        g_CurrentPreviewPlayer.store(nullptr, std::memory_order_release);
        g_CurrentPreviewPlayerVtable.store(nullptr, std::memory_order_release);
        g_CurrentPreviewPlayerCapturedAt.store(0, std::memory_order_release);
        g_CurrentPreviewPlayerGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    void* __fastcall hkPreviewPlayerConstructor(void* previewPlayer, void* arg2, void* arg3, void* arg4)
    {
        const auto original = g_PreviewPlayerConstructorHook.original<PreviewPlayerConstructorFn>();
        void* result = original ? original(previewPlayer, arg2, arg3, arg4) : previewPlayer;

        if (previewPlayer)
        {
            void* vtable = nullptr;
            TryReadObjectVtable(previewPlayer, vtable);
            g_CurrentPreviewPlayerVtable.store(vtable, std::memory_order_release);
            g_CurrentPreviewPlayer.store(previewPlayer, std::memory_order_release);
            g_CurrentPreviewPlayerCapturedAt.store(GetTickCount64(), std::memory_order_release);
            g_CurrentPreviewPlayerGeneration.fetch_add(1, std::memory_order_acq_rel);
        }

        return result;
    }

    bool ApplyPlayerSceneNodeRotation(float requestedYaw)
    {
        const std::uint32_t generation = g_CurrentPreviewPlayerGeneration.load(std::memory_order_acquire);
        void* entity = g_CurrentPreviewPlayer.load(std::memory_order_acquire);
        void* expectedVtable = g_CurrentPreviewPlayerVtable.load(std::memory_order_acquire);

        if (generation != g_PlayerRotationObservedGeneration)
        {
            g_PlayerRotationObservedGeneration = generation;
            g_PlayerRotationSceneNode = nullptr;
            g_PlayerPreviewBaseYaw = 0.0f;
            g_PlayerPreviewBaseYawValid = false;
            g_PlayerPreviewLastTargetYaw = 0.0f;
            g_PlayerPreviewLastTargetYawValid = false;
            g_PlayerRotationStableSince = 0;
            g_PlayerRotationStableYaw = 0.0f;
            g_PlayerRotationStableYawValid = false;
        }

        if (!entity)
            return false;

        void* actualVtable = nullptr;
        if (!TryReadObjectVtable(entity, actualVtable) || !expectedVtable || actualVtable != expectedVtable)
            return false;

        PlayerSceneNodeRotationData data{};
        if (!TryReadPlayerSceneNodeRotationData(entity, data))
            return false;

        // Values outside the expected scene-node state indicate a retired or reused
        // constructor-captured preview object. Never write through that chain.
        if (data.dirty > 1 || !std::isfinite(data.localYaw) || !std::isfinite(data.absoluteYaw) ||
            std::fabs(data.localYaw) > 3600.0f || std::fabs(data.absoluteYaw) > 3600.0f)
            return false;

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG capturedAt = g_CurrentPreviewPlayerCapturedAt.load(std::memory_order_acquire);
        const ULONGLONG captureAge = capturedAt && now >= capturedAt ? now - capturedAt : 0;

        if (g_PlayerRotationSceneNode != data.sceneNode)
        {
            g_PlayerRotationSceneNode = data.sceneNode;
            g_PlayerPreviewBaseYaw = 0.0f;
            g_PlayerPreviewBaseYawValid = false;
            g_PlayerPreviewLastTargetYaw = 0.0f;
            g_PlayerPreviewLastTargetYawValid = false;
            g_PlayerRotationStableSince = now;
            g_PlayerRotationStableYaw = data.localYaw;
            g_PlayerRotationStableYawValid = true;
        }

        if (!g_PlayerPreviewBaseYawValid)
        {
            if (!g_PlayerRotationStableYawValid ||
                std::fabs(NormalizePreviewYaw(data.localYaw - g_PlayerRotationStableYaw)) >= PLAYER_ROTATION_STABLE_EPSILON)
            {
                g_PlayerRotationStableYaw = data.localYaw;
                g_PlayerRotationStableSince = now;
                g_PlayerRotationStableYawValid = true;
            }

            const ULONGLONG stableFor = now >= g_PlayerRotationStableSince ? now - g_PlayerRotationStableSince : 0;
            const bool constructorZero = std::fabs(data.localYaw) < 0.01f && std::fabs(data.absoluteYaw) < 0.01f;
            const bool oldEnough = captureAge >= PLAYER_ROTATION_MIN_SETTLE_MS;
            const bool stableEnough = stableFor >= PLAYER_ROTATION_STABLE_MS;
            const bool zeroAccepted = !constructorZero || captureAge >= PLAYER_ROTATION_ZERO_FALLBACK_MS;

            if (!oldEnough || !stableEnough || !zeroAccepted)
                return false;

            g_PlayerPreviewBaseYaw = data.localYaw;
            g_PlayerPreviewBaseYawValid = true;
            g_PlayerPreviewLastTargetYaw = 0.0f;
            g_PlayerPreviewLastTargetYawValid = false;
        }

        const float targetYaw = NormalizePreviewYaw(g_PlayerPreviewBaseYaw + requestedYaw);
        if (!TryWritePlayerSceneNodeYaw(data.sceneNode, targetYaw))
            return false;

        g_PlayerPreviewLastTargetYaw = targetYaw;
        g_PlayerPreviewLastTargetYawValid = true;
        return true;
    }

    bool CopyPanoramaRenderTargetName(void* renderTarget, char(&output)[256])
    {
        output[0] = '\0';

        if (!renderTarget)
            return false;

        __try
        {
            const char* name = static_cast<const Panorama::RenderTarget*>(renderTarget)->name.Get();

            if (!name || !*name)
                return false;

            strncpy_s(output, sizeof(output), name, _TRUNCATE);
            return output[0] != '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            output[0] = '\0';
            return false;
        }
    }

    bool DescribeSrv(ID3D11ShaderResourceView* view, std::uint32_t& width, std::uint32_t& height)
    {
        width = 0;
        height = 0;

        if (!view)
            return false;

        ID3D11Resource* resource = nullptr;
        view->GetResource(&resource);

        if (!resource)
            return false;

        ID3D11Texture2D* texture = nullptr;
        const HRESULT result = resource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&texture));

        resource->Release();

        if (FAILED(result) || !texture)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        texture->Release();

        width = desc.Width;
        height = desc.Height;
        return width != 0 && height != 0;
    }

    ID3D11ShaderResourceView* CreateSnapshotSrv(ID3D11ShaderResourceView* source)
    {
        if (!source || !g_Device || !g_Context)
            return nullptr;

        ID3D11Resource* resource = nullptr;
        source->GetResource(&resource);

        if (!resource)
            return nullptr;

        ID3D11Texture2D* sourceTexture = nullptr;

        const HRESULT queryResult = resource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&sourceTexture));

        resource->Release();

        if (FAILED(queryResult) || !sourceTexture)
            return nullptr;

        D3D11_TEXTURE2D_DESC textureDesc{};
        sourceTexture->GetDesc(&textureDesc);

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        source->GetDesc(&viewDesc);

        // Same resource shape/format as the composition RT, but owned only as a
        // shader-resource snapshot. It is never bound as Panorama's render target.
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        textureDesc.CPUAccessFlags = 0;
        textureDesc.MiscFlags = 0;

        ID3D11Texture2D* snapshotTexture = nullptr;

        const HRESULT createTextureResult =
            g_Device->CreateTexture2D(&textureDesc, nullptr, &snapshotTexture);

        if (FAILED(createTextureResult) || !snapshotTexture)
        {
            sourceTexture->Release();
            return nullptr;
        }

        g_Context->CopyResource(snapshotTexture, sourceTexture);
        sourceTexture->Release();

        ID3D11ShaderResourceView* snapshotView = nullptr;

        const HRESULT createViewResult =
            g_Device->CreateShaderResourceView(
                snapshotTexture,
                &viewDesc,
                &snapshotView);

        snapshotTexture->Release();

        if (FAILED(createViewResult))
            return nullptr;

        return snapshotView;
    }

    void FreezeCurrentFrame()
    {
        ID3D11ShaderResourceView* current =
            g_CurrentTexture.load(std::memory_order_acquire);

        if (!current)
        {
            g_FreezeComplete.store(true, std::memory_order_release);
            return;
        }

        ID3D11ShaderResourceView* snapshot = CreateSnapshotSrv(current);

        if (!snapshot)
        {
            // Failing to snapshot is not fatal. Keep the existing AddRef'd SRV and
            // allow the main-thread state machine to continue.
            g_FreezeComplete.store(true, std::memory_order_release);
            return;
        }

        ID3D11ShaderResourceView* previous =
            g_CurrentTexture.exchange(snapshot, std::memory_order_acq_rel);

        if (previous)
            previous->Release();

        g_FreezeComplete.store(true, std::memory_order_release);
    }

    ID3D11ShaderResourceView* ReadSrvFromBinding(void* binding, bool srgb)
    {
        if (!binding)
            return nullptr;

        __try
        {
            const auto* resource = static_cast<const ResourceBinding_t*>(binding);
            if (resource->strongReferences <= 0)
                return nullptr;

            const auto* texture = static_cast<const Panorama::TextureDx11*>(resource->pData);
            if (!texture)
                return nullptr;

            return srgb ? texture->srgbView : texture->linearView;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void ClearStagedFrame()
    {
        ID3D11ShaderResourceView* previous =
            g_StagedTexture.exchange(nullptr, std::memory_order_acq_rel);

        g_StagedTextureWidth.store(0, std::memory_order_release);
        g_StagedTextureHeight.store(0, std::memory_order_release);
        g_StagedTextureAt.store(0, std::memory_order_release);
        g_StagedGeneration.store(0, std::memory_order_release);

        if (previous)
            previous->Release();
    }

    void StageSrv(ID3D11ShaderResourceView* view)
    {
        if (!view)
            return;

        std::uint32_t width = 0;
        std::uint32_t height = 0;

        if (!DescribeSrv(view, width, height))
            return;

        const std::uint32_t generation =
            g_ActiveGeneration.load(std::memory_order_acquire);

        // AcquireLayerRT fires continuously while the panel is alive. Once this
        // generation is already visible in the menu, ignore further staging attempts.
        if (g_PromotedGeneration.load(std::memory_order_acquire) == generation &&
            g_CurrentTexture.load(std::memory_order_acquire) != nullptr)
        {
            return;
        }

        ID3D11ShaderResourceView* current =
            g_StagedTexture.load(std::memory_order_acquire);

        if (current == view &&
            g_StagedGeneration.load(std::memory_order_acquire) == generation)
        {
            return;
        }

        view->AddRef();

        ID3D11ShaderResourceView* previous =
            g_StagedTexture.exchange(view, std::memory_order_acq_rel);

        g_StagedTextureWidth.store(width, std::memory_order_release);
        g_StagedTextureHeight.store(height, std::memory_order_release);

        if (g_StagedGeneration.exchange(generation, std::memory_order_acq_rel) != generation ||
            g_StagedTextureAt.load(std::memory_order_acquire) == 0)
        {
            g_StagedTextureAt.store(GetTickCount64(), std::memory_order_release);
        }

        if (previous)
            previous->Release();

    }

    void StageSrvFromBinding()
    {
        void* binding = g_PreviewBinding.load(std::memory_order_acquire);

        if (!binding)
            return;

        ID3D11ShaderResourceView* view = ReadSrvFromBinding(binding, false);

        if (!view)
            view = ReadSrvFromBinding(binding, true);

        if (view)
            StageSrv(view);
    }

    void PromoteStagedSrvIfReady()
    {
        if (features::changer::application::IsUpdatePending())
            return;

        const std::uint64_t buildRevision =
            g_BuildRevision.load(std::memory_order_acquire);

        const std::uint64_t latestRevision =
            systems::preview_item::GetLatestRevision();

        // Never publish an intermediate candidate that became obsolete while it was
        // building. The main-thread state machine will discard it and consume the
        // newest queued protobuf instead.
        if (buildRevision == 0 ||
            (latestRevision != 0 && buildRevision != latestRevision))
        {
            return;
        }

        ID3D11ShaderResourceView* staged =
            g_StagedTexture.load(std::memory_order_acquire);

        if (!staged)
            return;

        const std::uint32_t generation =
            g_ActiveGeneration.load(std::memory_order_acquire);

        if (g_StagedGeneration.load(std::memory_order_acquire) != generation)
            return;

        const ULONGLONG stagedAt =
            g_StagedTextureAt.load(std::memory_order_acquire);

        const ULONGLONG now = GetTickCount64();

        const ULONGLONG warmupMs =
            g_BuildComplexCosmetics.load(std::memory_order_acquire)
            ? COMPLEX_SWAP_WARMUP_MS
            : SIMPLE_SWAP_WARMUP_MS;

        if (!stagedAt || now - stagedAt < warmupMs)
            return;

        // Recheck immediately before publishing. Request() runs on the render/UI
        // thread too, so this closes the practical race where a new committed state
        // arrives between the first revision check and the SRV exchange below.
        if (buildRevision != systems::preview_item::GetLatestRevision())
            return;

        staged = g_StagedTexture.exchange(nullptr, std::memory_order_acq_rel);

        if (!staged)
            return;

        const std::uint32_t width =
            g_StagedTextureWidth.exchange(0, std::memory_order_acq_rel);

        const std::uint32_t height =
            g_StagedTextureHeight.exchange(0, std::memory_order_acq_rel);

        g_StagedTextureAt.store(0, std::memory_order_release);
        g_StagedGeneration.store(0, std::memory_order_release);

        // Publish presentation and texture as one logical frame. Mark it invalid while
        // replacing the SRV so a concurrent GetFrame() can only see the old complete
        // publication or the new complete publication, never a cross-presentation mix.
        g_PublishedPresentationValid.store(false, std::memory_order_release);

        ID3D11ShaderResourceView* previous =
            g_CurrentTexture.exchange(staged, std::memory_order_acq_rel);

        g_CurrentTextureWidth.store(width, std::memory_order_release);
        g_CurrentTextureHeight.store(height, std::memory_order_release);
        g_PublishedPresentation.store(
            g_RequestedPresentation.load(std::memory_order_acquire),
            std::memory_order_release);
        g_PromotedGeneration.store(generation, std::memory_order_release);
        g_PublishedPresentationValid.store(true, std::memory_order_release);
        g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);

        if (previous)
            previous->Release();

        g_BuildPromoted.store(true, std::memory_order_release);
    }

    void* ResolvePreviewEntityFromPanel(void* panel)
    {
        if (!panel || !addresses::globals::game_entity_system)
            return nullptr;

        __try
        {
            const std::int32_t rawHandle = static_cast<const Panorama::Item3dPanel*>(panel)->renderEntityHandle;

            if (rawHandle == -1 || rawHandle == -2)
                return nullptr;

            return reinterpret_cast<void*>(systems::g_entities.get_by_index(rawHandle & features::changer::engine::entry_mask));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void ReleasePreviewCompositeForZeroStickerTransition()
    {
        if (!g_ZeroStickerReleasePending.exchange(false, std::memory_order_acq_rel))
            return;

        void* entity = ResolvePreviewEntityFromPanel(g_Item3dPanel.load(std::memory_order_acquire));

        features::changer::engine::composite_material_owner* owner = features::changer::engine::composite_owner(entity);
        if (!owner || !g_ReleaseCompositeMaterials)
            return;

        __try
        {
            // The sticker builder returns without replacing the old material when the
            // list becomes empty. Releasing the current preview owner's composites first
            // lets the derived weapon callback reapply the clean material immediately.
            g_ReleaseCompositeMaterials(owner, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkPreviewStickerInModelPanel(void* inventoryApi, const char* itemId, int slot, void* panel)
    {
        const PreviewStickerInModelPanelFn original = g_PreviewStickerInModelPanelHook.original<PreviewStickerInModelPanelFn>();

        if (!original)
            return;

        if (panel && g_PreviewPanelCaptureArmed.exchange(false, std::memory_order_acq_rel))
            g_Item3dPanel.store(panel, std::memory_order_release);

        original(inventoryApi, itemId, slot, panel);
    }

    void CapturePanoramaRenderTarget(void* renderTarget)
    {
        if (!renderTarget || !g_BuildActive.load(std::memory_order_acquire))
            return;

        char name[256]{};

        if (!CopyPanoramaRenderTargetName(renderTarget, name))
            return;

        // panorama.dll stores exactly the unsuffixed value supplied through
        // composition-layer-texture-name in this CBufferString.
        //
        // Use a unique texture name for each generation. Panorama caches the vtex
        // binding by name, so reusing one name can leave a new panel attached to the
        // previous panel's composition/material while DeleteAsync is still retiring it.
        if (g_BuildTextureName[0] == '\0' ||
            std::strcmp(name, g_BuildTextureName) != 0)
        {
            return;
        }

        __try
        {
            const auto* target = static_cast<const Panorama::RenderTarget*>(renderTarget);
            void* binding = ((target->flags & 0x2u) && target->postProcessBinding) ? target->postProcessBinding : target->colorBinding;

            if (!binding)
                return;

            g_PreviewBinding.store(binding, std::memory_order_release);
            StageSrvFromBinding();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
    }

    void __fastcall hkCreatePanoramaRenderTargets(void* renderTarget)
    {
        const CreatePanoramaRenderTargetsFn original =
            g_CreatePanoramaRenderTargetsHook.original<CreatePanoramaRenderTargetsFn>();

        if (!original)
            return;

        original(renderTarget);
        CapturePanoramaRenderTarget(renderTarget);
    }

    void* __fastcall hkAcquirePanoramaLayerRT(void* cache, void* layerDesc)
    {
        const AcquirePanoramaLayerRTFn original =
            g_AcquirePanoramaLayerRTHook.original<AcquirePanoramaLayerRTFn>();

        if (!original)
            return nullptr;

        void* renderTarget = original(cache, layerDesc);
        CapturePanoramaRenderTarget(renderTarget);
        return renderTarget;
    }

    PanoramaPanel2D* SafeReadPanel(PanoramaPanel2D** panelPointer)
    {
        if (!panelPointer)
            return nullptr;

        __try
        {
            return *panelPointer;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    PanoramaUiPanel* SafeGetUiPanel(PanoramaPanel2D* panel)
    {
        if (!panel)
            return nullptr;

        __try
        {
            return panel->uiPanel;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    PanoramaUiPanel* GetActiveContext()
    {
        const bool inGame =
            addresses::globals::source2engine_to_client &&
            memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 40) &&
            memory::call_vfunc<bool>(addresses::globals::source2engine_to_client, 39);

        PanoramaPanel2D* root = nullptr;

        if (inGame)
            root = SafeReadPanel(g_HudPanelPointer);

        if (!root)
            root = SafeReadPanel(g_MainMenuPanelPointer);

        if (!root)
            root = SafeReadPanel(g_HudPanelPointer);

        return SafeGetUiPanel(root);
    }

    bool RunScript(PanoramaUiPanel* context, const char* script)
    {
        if (!context || !script || !g_UiEnginePointer || !g_RunScript)
            return false;

        __try
        {
            PanoramaUiEngine* engine = *g_UiEnginePointer;

            if (!engine)
                return false;

            static const char originFile = '\0';
            g_RunScript(engine, context, script, &originFile, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CreatePanel(
        PanoramaUiPanel* context,
        const char* panelId,
        const char* textureName,
        std::uint64_t itemId,
        systems::model_preview::Presentation presentation,
        const std::string& modelPath,
        bool counterTerrorist,
        int captureStickerSlot)
    {
        if (presentation == systems::model_preview::Presentation::Player)
            ResetCurrentPreviewPlayerCapture();

        char script[18432]{};

        const float rotationX = g_RotationX.load(std::memory_order_acquire);
        const float rotationY = g_RotationY.load(std::memory_order_acquire);
        // CS2's current MapPlayerPreviewPanel loadout scene uses the CT loadout
        // camera for both T and CT player models. The T-specific camera does not
        // reliably produce a composition layer on this build.
        (void)counterTerrorist;
        constexpr const char* playerCamera = "cam_loadoutmenu_ct";

        const int written = std::snprintf(
            script,
            sizeof(script),
            R"JS((function(){
const root=$.GetContextPanel();

if(typeof InventoryAPI==='undefined'){
    return;
}

if(typeof GameInterfaceAPI==='undefined' || typeof GameInterfaceAPI.GetSettingString!=='function'){
    return;
}

const panelId='%s';
const containerId=panelId+'_container';
const textureName='%s';
const itemId=BigInt('%llu');
const rotationX=%f;
const rotationY=%f;
const playerPreview=%s;
const playerModel='%s';
const playerCamera='%s';
const captureStickerSlot=%d;

if(!InventoryAPI.IsValidItemID(itemId)){
    return;
}

function createContainer(){
    const container=$.CreatePanel('Panel',root,containerId,{
        hittest:false,
        style:'width: %upx; height: %upx; opacity: 0.01; brightness: 0.0; wash-color: #00000000; x: 0px; y: 0px; z-index: -99999;'
    });

    if(container){
        container.hittest=false;
        container.hittestchildren=false;
    }

    return container;
}

function configurePanel(panel){
    if(!panel){
        return;
    }

    panel.hittest=false;
    panel.hittestchildren=false;

    try{
        if(typeof panel.SetRenderInterval==='function'){
            panel.SetRenderInterval(1);
        }
    }catch(e){
    }

    panel.SetAttributeString('velocity_model_preview_source','native-model-preview');

    try{
        panel.SetHideStaticGeometry(true);
        panel.SetHideParticles(true);
        panel.SetTransparentBackground(true);
    }catch(e){
    }
}

function applyRotation(panel,entityName){
    if(!panel){
        return;
    }

    try{
        panel.SetRotation(rotationX,rotationY,0.0);
    }catch(e){
    }

    if(entityName){
        try{
            panel.FireEntityInput(entityName,'SetAngles',String(rotationY)+' '+String(rotationX)+' 0');
        }catch(e){
        }
    }
}

function createPlayerPreview(){
    if(!playerModel){
        return;
    }

    const container=createContainer();

    if(!container){
        return;
    }

    const panel=$.CreatePanel('MapPlayerPreviewPanel',container,panelId,{
        'require-composition-layer':'true',
        'composition-layer-texture-name':textureName,
        'transparent-background':'false',
        'disable-depth-of-field':'false',
        map:'ui/buy_menu',
        camera:playerCamera,
        playermodel:playerModel,
        playername:'vanity_character',
        initial_entity:'vanity_character',
        animgraphcharactermode:'buy-menu',
        player:'true',
        mouse_rotate:'true',
        sync_spawn_addons:'true',
        'pin-fov':'vertical',
        csm_split_plane0_distance_override:'250.0',
        style:'width: 100%%; height: 100%%; opacity: 1.0;'
    });

    if(!panel){
        try{container.DeleteAsync(0.0);}catch(e){}
        return;
    }

    configurePanel(panel);
    panel.Data().loadedMap='ui/buy_menu';

    // Match the native player-preview setup. This is important for T models as
    // some of their scene/addon state is not completed by playermodel alone.
    try{
        if(typeof panel.EquipPlayerWithItem==='function'){
            panel.EquipPlayerWithItem(itemId);
        }
    }catch(e){
    }

    try{
        panel.SetReadyForDisplay(true);
    }catch(e){
    }
}

function getMapName(){
    let mapName=GameInterfaceAPI.GetSettingString('ui_inspect_bkgnd_map');

    if(!mapName || mapName==='mainmenu'){
        mapName=GameInterfaceAPI.GetSettingString('ui_mainmenu_bkgnd_movie');
    }

    if(!mapName){
        mapName='de_dust2';
    }

    if(!mapName.endsWith('_vanity')){
        mapName=mapName+'_vanity';
    }

    return mapName;
}

function bindItem(panel,ready){
    if(!panel || !panel.IsValid()){
        return;
    }

    panel.Data().itemId=itemId;
    panel.Data().active_item_idx=0;

    try{
        panel.SetActiveItem(0);
        panel.SetItemItemId(itemId,'');
    }catch(e){
    }

    applyRotation(panel,'item');

    try{
        panel.SetReadyForDisplay(ready);
    }catch(e){
    }
}

function createWeaponPreview(){
    const mapName=getMapName();

    const container=createContainer();

    if(!container){
        return;
    }

    const panel=$.CreatePanel('MapItemPreviewPanel',container,panelId,{
        'require-composition-layer':'true',
        'composition-layer-texture-name':textureName,
        'transparent-background':'false',
        'disable-depth-of-field':'false',
        hide_while_waiting_for_composite_materials:'true',
        'pin-fov':'vertical',
        camera:'cam_default',
        player:'true',
        map:mapName,
        initial_entity:'item',
        mouse_rotate:'false',
        rotation_limit_x:'360',
        rotation_limit_y:'90',
        auto_rotate_x:'0',
        auto_rotate_y:'0',
        auto_rotate_period_x:'15',
        auto_rotate_period_y:'25',
        auto_recenter:false,
        panzoom_enabled:'true',
        tabindex:'auto',
        selectionpos:'auto',
        style:'width: 100%%; height: 100%%; opacity: 1.0;'
    });

    if(!panel){
        try{container.DeleteAsync(0.0);}catch(e){}
        return;
    }

    configurePanel(panel);
    panel.Data().loadedMap=mapName;

    try{
        panel.SetReadyForDisplay(false);
    }catch(e){
    }

    // Capture the native CUI_Item3dPanel before binding the item. Once bound, CS2 can
    // skip PreviewStickerInModelPanel when the cached sticker kit already matches, which
    // would leave us without the panel pointer needed for the final-sticker cleanup.
    if(captureStickerSlot>=0){
        try{
            if(typeof InventoryAPI.PreviewStickerInModelPanel==='function'){
                InventoryAPI.PreviewStickerInModelPanel(String(itemId),captureStickerSlot,panel);
            }
        }catch(e){
        }
    }

    // Bind exactly once. Repeated SetItemItemId calls restart/overlap Source 2's
    // asynchronous composite-material work and can transiently drop stickers or show
    // the base weapon. The panel's native waiting flag handles the unfinished period.
    bindItem(panel,true);

    for(let i=0;i<=10;i++){
        const suffix=i===0?'':String(i);

        if(i!==0){
            panel.FireEntityInput('light_item'+suffix,'Disable');
            panel.FireEntityInput('light_item_new'+suffix,'Disable');
        }else{
            panel.FireEntityInput('light_item_new','Disable');
        }
    }

    try{
        const category=InventoryAPI.GetLoadoutCategory(itemId);

        if(category==='secondary'){
            panel.SetCSMSplitPlane0DistanceOverride(30.0);
        }else if(category==='smg'){
            panel.SetCSMSplitPlane0DistanceOverride(40.0);
        }else if(category==='rifle'){
            panel.SetCSMSplitPlane0DistanceOverride(55.0);
        }

        if(mapName==='warehouse_vanity' || mapName==='de_train_vanity' || mapName==='ui/acknowledge_item'){
            panel.SetBarnlightShadowScaleOverride(1.0);
        }else{
            panel.SetBarnlightShadowScaleOverride(4.0);
        }
    }catch(e){
    }

    function getWeaponCamera(){
        let defName='';

        try{
            defName=InventoryAPI.GetItemDefinitionName(itemId);
        }catch(e){
        }

        const cameras={
            weapon_awp:'7',
            weapon_aug:'3',
            weapon_sg556:'4',
            weapon_ssg08:'6',
            weapon_ak47:'4',
            weapon_m4a1_silencer:'6',
            weapon_famas:'4',
            weapon_g3sg1:'5',
            weapon_galilar:'3',
            weapon_m4a1:'4',
            weapon_scar20:'5',
            weapon_mp5sd:'3',
            weapon_xm1014:'4',
            weapon_m249:'6',
            weapon_ump45:'3',
            weapon_bizon:'3',
            weapon_mag7:'3',
            weapon_nova:'5',
            weapon_sawedoff:'3',
            weapon_negev:'5',
            weapon_usp_silencer:'2',
            weapon_elite:'2',
            weapon_tec9:'2',
            weapon_revolver:'2',
            weapon_c4:'3',
            weapon_taser:'0'
        };

        if(cameras[defName]!==undefined){
            return cameras[defName];
        }

        let category='';

        try{
            category=InventoryAPI.GetLoadoutCategory(itemId);
        }catch(e){
        }

        if(category==='secondary'){
            return '0';
        }

        if(category==='smg'){
            return '2';
        }

        return '3';
    }

    const camera=getWeaponCamera();

    try{
        panel.TransitionToCamera('cam_'+camera,0);
    }catch(e){
    }
}

function createNow(){
    if(root.FindChildTraverse(panelId) || root.FindChildTraverse(containerId)){
        return;
    }

    if(playerPreview){
        createPlayerPreview();
    }else{
        createWeaponPreview();
    }
}

createNow();
})();)JS",
panelId,
textureName,
static_cast<unsigned long long>(itemId),
rotationX,
rotationY,
presentation == systems::model_preview::Presentation::Player ? "true" : "false",
modelPath.c_str(),
playerCamera,
captureStickerSlot,
PREVIEW_PANEL_WIDTH,
PREVIEW_PANEL_HEIGHT);

        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
            return false;

        return RunScript(context, script);
    }

    bool ApplyRotation(PanoramaUiPanel* context, const char* panelId, systems::model_preview::Presentation presentation, float rotationX, float rotationY)
    {
        if (!context || !panelId || !*panelId)
            return false;

        const bool playerPreview = presentation == systems::model_preview::Presentation::Player;

        if (playerPreview)
        {
            // CUI_Player3dPanel has no continuous SetRotation method. Drive the
            // constructor-captured preview player's features::changer::engine::scene_node local yaw directly
            // and mark the transform dirty so Source 2 rebuilds its world transform.
            return ApplyPlayerSceneNodeRotation(rotationX);
        }

        char script[1536]{};

        const int written = std::snprintf(
            script,
            sizeof(script),
            R"JS((function(){
const p=$.GetContextPanel().FindChildTraverse('%s');
if(!p||!p.IsValid())return;
const yaw=%f;
const pitch=%f;
try{p.SetRotation(yaw,pitch,0.0);}catch(e){}
try{p.SetRotation(yaw,pitch);}catch(e){}
try{p.FireEntityInput('item','SetAngles',String(pitch)+' '+String(yaw)+' 0');}catch(e){}
})();)JS",
panelId,
rotationX,
rotationY);

        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
            return false;

        return RunScript(context, script);
    }

    bool ParkPanel(PanoramaUiPanel* context, const char* panelId)
    {
        if (!context || !panelId || !*panelId)
            return false;

        char script[1024]{};

        const int written = std::snprintf(
            script,
            sizeof(script),
            R"JS((function(){
const root=$.GetContextPanel();
const panelId='%s';
const p=root.FindChildTraverse(panelId);
const c=root.FindChildTraverse(panelId+'_container');
if(p && p.IsValid()){
    try{p.SetReadyForDisplay(false);}catch(e){}
}
if(c && c.IsValid()){
    try{c.style.visibility='collapse';}catch(e){}
}else if(p && p.IsValid()){
    try{p.style.visibility='collapse';}catch(e){}
}
})();)JS",
panelId);

        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
            return false;

        return RunScript(context, script);
    }

    void DeletePanel(PanoramaUiPanel* context, const char* panelId)
    {
        if (!context || !panelId || !*panelId)
            return;

        char script[512]{};

        const int written = std::snprintf(
            script,
            sizeof(script),
            R"JS((function(){
const root=$.GetContextPanel();
const panelId='%s';
const c=root.FindChildTraverse(panelId+'_container');
if(c){
    c.DeleteAsync(0.0);
    return;
}
const p=root.FindChildTraverse(panelId);
if(p)p.DeleteAsync(0.0);
})();)JS",
panelId);

        if (written > 0 && static_cast<std::size_t>(written) < sizeof(script))
            RunScript(context, script);
    }

    void ResetActiveFrame()
    {
        // Invalidate the publication metadata first so GetFrame() can never associate
        // an old presentation's texture with a newly-requested presentation.
        g_PublishedPresentationValid.store(false, std::memory_order_release);
        g_PreviewBinding.store(nullptr, std::memory_order_release);
        ClearStagedFrame();

        ID3D11ShaderResourceView* previous =
            g_CurrentTexture.exchange(nullptr, std::memory_order_acq_rel);

        g_CurrentTextureWidth.store(0, std::memory_order_release);
        g_CurrentTextureHeight.store(0, std::memory_order_release);
        g_PromotedGeneration.store(0, std::memory_order_release);

        if (previous)
            previous->Release();
    }

    template <typename T>
    void RemoveHookIfInstalled(T& hook)
    {
        hook.reset();
    }

}

bool systems::preview_panorama::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return false;

    Shutdown();

    g_PanelSessionId =
        (static_cast<std::uint64_t>(GetTickCount64()) << 16) ^
        static_cast<std::uint64_t>(GetCurrentProcessId());

    g_Device = device;
    g_Context = context;
    g_Device->AddRef();
    g_Context->AddRef();

    const std::uint8_t* uiEnginePattern = reinterpret_cast<std::uint8_t*>(PATTERN(patterns::ui_engine_pointer));
    const std::uint8_t* mainMenuPattern = reinterpret_cast<std::uint8_t*>(PATTERN(patterns::main_menu_panel_pointer));
    const std::uint8_t* hudPattern = reinterpret_cast<std::uint8_t*>(PATTERN(patterns::hud_panel_pointer));
    const std::uint8_t* runScriptPattern = reinterpret_cast<std::uint8_t*>(PATTERN(patterns::run_panorama_script));

    if (uiEnginePattern)
        g_UiEnginePointer = reinterpret_cast<PanoramaUiEngine**>(features::changer::engine::relative_address(const_cast<std::uint8_t*>(uiEnginePattern), 7));

    if (mainMenuPattern)
        g_MainMenuPanelPointer = reinterpret_cast<PanoramaPanel2D**>(features::changer::engine::relative_address(const_cast<std::uint8_t*>(mainMenuPattern), 7));

    if (hudPattern)
        g_HudPanelPointer = reinterpret_cast<PanoramaPanel2D**>(features::changer::engine::relative_address(const_cast<std::uint8_t*>(hudPattern), 3));

    if (runScriptPattern)
        g_RunScript = reinterpret_cast<RunScriptFn>(const_cast<std::uint8_t*>(runScriptPattern));

    if (!g_UiEnginePointer || !g_RunScript || (!g_MainMenuPanelPointer && !g_HudPanelPointer))
    {
        g_Status.store(systems::model_preview::Status::MissingPatterns, std::memory_order_release);
        return false;
    }

    if (!systems::preview_item::Initialize())
    {
        g_Status.store(systems::model_preview::Status::MissingPatterns, std::memory_order_release);
        return false;
    }

    const std::uint8_t* constructorPattern = reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_player_constructor));
    void* constructorAddress = constructorPattern ? const_cast<std::uint8_t*>(constructorPattern) : nullptr;

    if (constructorAddress)
    {
        MEMORY_BASIC_INFORMATION memoryInfo{};
        const bool executableTarget =
            VirtualQuery(constructorAddress, &memoryInfo, sizeof(memoryInfo)) == sizeof(memoryInfo) &&
            memoryInfo.State == MEM_COMMIT &&
            (memoryInfo.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;

        if (executableTarget)
            install_hook(g_PreviewPlayerConstructorHook, constructorAddress, reinterpret_cast<void*>(&hkPreviewPlayerConstructor));
    }

    const std::uint8_t* previewStickerInModelPanelPattern =
        reinterpret_cast<std::uint8_t*>(PATTERN(patterns::preview_sticker_in_model_panel));

    g_ReleaseCompositeMaterials = reinterpret_cast<ReleaseCompositeMaterialsFn>(
        reinterpret_cast<std::uint8_t*>(PATTERN(patterns::release_composite_materials)));

    if (previewStickerInModelPanelPattern)
    {
        install_hook(g_PreviewStickerInModelPanelHook, 
            const_cast<std::uint8_t*>(previewStickerInModelPanelPattern),
            reinterpret_cast<void*>(&hkPreviewStickerInModelPanel));
    }

    const std::uint8_t* createRenderTargetsPattern =
        reinterpret_cast<std::uint8_t*>(PATTERN(patterns::panorama_create_render_targets));

    const std::uint8_t* acquireLayerRTPattern =
        reinterpret_cast<std::uint8_t*>(PATTERN(patterns::panorama_acquire_layer_rt));

    if (!createRenderTargetsPattern)
    {
        g_Status.store(systems::model_preview::Status::MissingPatterns, std::memory_order_release);
        return false;
    }

    if (!install_hook(g_CreatePanoramaRenderTargetsHook, 
        const_cast<std::uint8_t*>(createRenderTargetsPattern),
        reinterpret_cast<void*>(&hkCreatePanoramaRenderTargets)))
    {
        g_Status.store(systems::model_preview::Status::MissingPatterns, std::memory_order_release);
        return false;
    }

    // Optional cache-hit hook; the creation hook remains sufficient if unavailable.
    if (acquireLayerRTPattern)
    {
        install_hook(g_AcquirePanoramaLayerRTHook, 
            const_cast<std::uint8_t*>(acquireLayerRTPattern),
            reinterpret_cast<void*>(&hkAcquirePanoramaLayerRT));
    }

    g_Initialized.store(true, std::memory_order_release);
    g_Status.store(systems::model_preview::Status::WaitingForRequest, std::memory_order_release);

    return true;
}

void systems::preview_panorama::Shutdown()
{
    g_Initialized.store(false, std::memory_order_release);

    // Do not call Panorama RunScript from the DLL unload thread. All V8 interaction
    // is confined to ProcessMainThread(); the hidden preview panel can be discarded by
    // Panorama/root teardown naturally during unload.
    g_PanelActive.store(false, std::memory_order_release);
    g_CurrentPanelParked.store(false, std::memory_order_release);
    g_Status.store(systems::model_preview::Status::Uninitialized, std::memory_order_release);

    ResetActiveFrame();

    RemoveHookIfInstalled(g_AcquirePanoramaLayerRTHook);
    RemoveHookIfInstalled(g_CreatePanoramaRenderTargetsHook);
    RemoveHookIfInstalled(g_PreviewPlayerConstructorHook);
    RemoveHookIfInstalled(g_PreviewStickerInModelPanelHook);

    systems::preview_item::Shutdown();

    if (g_Context)
    {
        g_Context->Release();
        g_Context = nullptr;
    }

    if (g_Device)
    {
        g_Device->Release();
        g_Device = nullptr;
    }

    g_UiEnginePointer = nullptr;
    g_MainMenuPanelPointer = nullptr;
    g_HudPanelPointer = nullptr;
    g_RunScript = nullptr;

    g_CurrentContext = nullptr;
    g_AppliedDefinitionIndex = 0;
    g_AppliedItemId = 0;
    g_AppliedRevision = 0;
    g_AppliedComplexCosmetics = false;
    g_AppliedPresentation = systems::model_preview::Presentation::Weapon;
    g_PanelSessionId = 0;
    g_CurrentPanelId[0] = '\0';

    g_BuildContext = nullptr;
    g_BuildPanelId[0] = '\0';
    g_BuildTextureName[0] = '\0';
    g_BuildDefinitionIndex = 0;
    g_BuildItemId = 0;
    g_BuildRevision.store(0, std::memory_order_release);
    g_BuildComplexCosmeticsValue = false;
    g_BuildPresentation = systems::model_preview::Presentation::Weapon;
    g_BuildComplexCosmetics.store(false, std::memory_order_release);
    g_BuildActive.store(false, std::memory_order_release);
    g_BuildPromoted.store(false, std::memory_order_release);
    g_RetireReleaseAt = 0;

    g_RotationX.store(0.0f, std::memory_order_release);
    g_RotationY.store(0.0f, std::memory_order_release);
    g_RotationDirty.store(false, std::memory_order_release);
    g_CurrentPreviewPlayer.store(nullptr, std::memory_order_release);
    g_CurrentPreviewPlayerVtable.store(nullptr, std::memory_order_release);
    g_CurrentPreviewPlayerGeneration.store(0, std::memory_order_release);
    g_CurrentPreviewPlayerCapturedAt.store(0, std::memory_order_release);
    g_PlayerRotationObservedGeneration = 0;
    g_PlayerRotationSceneNode = nullptr;
    g_PlayerPreviewBaseYaw = 0.0f;
    g_PlayerPreviewBaseYawValid = false;
    g_PlayerPreviewLastTargetYaw = 0.0f;
    g_PlayerPreviewLastTargetYawValid = false;
    g_PlayerRotationStableSince = 0;
    g_PlayerRotationStableYaw = 0.0f;
    g_PlayerRotationStableYawValid = false;

    g_PublishedPresentation.store(systems::model_preview::Presentation::Weapon, std::memory_order_release);
    g_PublishedPresentationValid.store(false, std::memory_order_release);

    g_RequestedDefinitionIndex.store(0, std::memory_order_release);
    g_RequestedPresentation.store(systems::model_preview::Presentation::Weapon, std::memory_order_release);
    g_LastRequestAt.store(0, std::memory_order_release);
    g_ActiveGeneration.store(0, std::memory_order_release);
    g_PromotedGeneration.store(0, std::memory_order_release);
    g_FreezeRequested.store(false, std::memory_order_release);
    g_FreezeComplete.store(false, std::memory_order_release);
    g_ResetFrameRequested.store(false, std::memory_order_release);

    g_Item3dPanel.store(nullptr, std::memory_order_release);
    g_PanelCaptureStickerSlot.store(-1, std::memory_order_release);
    g_PreviewPanelCaptureArmed.store(false, std::memory_order_release);
    g_ZeroStickerReleasePending.store(false, std::memory_order_release);
    g_LastEffectiveStickerCount.store(0, std::memory_order_release);
    g_LastStickerDefinitionIndex.store(0, std::memory_order_release);
    g_ReleaseCompositeMaterials = nullptr;
}

void systems::preview_panorama::ProcessMainThread()
{
    if (!g_Initialized.load(std::memory_order_acquire))
        return;

    ReleasePreviewCompositeForZeroStickerTransition();

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG lastRequest = g_LastRequestAt.load(std::memory_order_acquire);

    if (g_RetireReleaseAt != 0 && now >= g_RetireReleaseAt)
    {
        g_RetireReleaseAt = 0;
        systems::preview_item::ReleaseForNextRequest();
    }

    if (!lastRequest || now - lastRequest > REQUEST_TIMEOUT_MS)
    {
        PanoramaUiPanel* activeContext = GetActiveContext();

        if (g_BuildActive.exchange(false, std::memory_order_acq_rel) &&
            g_BuildContext == activeContext)
        {
            DeletePanel(g_BuildContext, g_BuildPanelId);
        }

        if (g_PanelActive.exchange(false, std::memory_order_acq_rel) &&
            g_CurrentContext == activeContext)
        {
            DeletePanel(g_CurrentContext, g_CurrentPanelId);
        }

        g_BuildPromoted.store(false, std::memory_order_release);
        g_BuildComplexCosmetics.store(false, std::memory_order_release);
        g_CurrentPanelParked.store(false, std::memory_order_release);

        g_CurrentContext = nullptr;
        g_BuildContext = nullptr;

        g_AppliedDefinitionIndex = 0;
            g_AppliedItemId = 0;
        g_AppliedRevision = 0;
        g_AppliedComplexCosmetics = false;
        g_AppliedPresentation = systems::model_preview::Presentation::Weapon;

        g_BuildDefinitionIndex = 0;
        g_BuildItemId = 0;
        g_BuildRevision.store(0, std::memory_order_release);
        g_BuildComplexCosmeticsValue = false;
        g_BuildPresentation = systems::model_preview::Presentation::Weapon;

        g_CurrentPanelId[0] = '\0';
            g_BuildPanelId[0] = '\0';
        g_BuildTextureName[0] = '\0';

        g_RetireReleaseAt = 0;
        g_PreviewBinding.store(nullptr, std::memory_order_release);
        g_Item3dPanel.store(nullptr, std::memory_order_release);
        g_PanelCaptureStickerSlot.store(-1, std::memory_order_release);
        g_PreviewPanelCaptureArmed.store(false, std::memory_order_release);
        g_ZeroStickerReleasePending.store(false, std::memory_order_release);
        g_LastEffectiveStickerCount.store(0, std::memory_order_release);
        g_LastStickerDefinitionIndex.store(0, std::memory_order_release);
        ClearStagedFrame();

        g_ResetFrameRequested.store(true, std::memory_order_release);
        systems::preview_item::ReleaseForNextRequest();
        g_Status.store(systems::model_preview::Status::WaitingForRequest, std::memory_order_release);
        return;
    }

    // Hold player yaw every main-thread update, not only while the mouse is moving.
    // The preview animation can rewrite local yaw between updates, so the requested
    // rotation has to be reapplied while the player preview is active.
    const bool playerPreviewActive =
        (g_PanelActive.load(std::memory_order_acquire) &&
            g_AppliedPresentation == systems::model_preview::Presentation::Player) ||
        (g_BuildActive.load(std::memory_order_acquire) &&
            g_BuildPresentation == systems::model_preview::Presentation::Player);

    if (playerPreviewActive)
        ApplyPlayerSceneNodeRotation(g_RotationX.load(std::memory_order_acquire));

    // Freeze the current preview while the live weapon is refreshing. Both paths touch
    // custom-material state, so keep the last completed SRV visible until the replacement
    // preview has finished building.
    if (features::changer::application::IsUpdatePending())
    {
        const std::uint64_t latestRevision = systems::preview_item::GetLatestRevision();
        const bool hasPendingPreviewChange =
            latestRevision != 0 && latestRevision != g_AppliedRevision;

        if (hasPendingPreviewChange &&
            g_CurrentTexture.load(std::memory_order_acquire) &&
            g_PanelActive.load(std::memory_order_acquire) &&
            !g_CurrentPanelParked.load(std::memory_order_acquire))
        {
            // Request() normally performs this snapshot immediately from Present. Keep
            // this fallback in case the request arrived before a valid displayed SRV.
            if (!g_FreezeComplete.load(std::memory_order_acquire))
            {
                g_FreezeRequested.store(true, std::memory_order_release);
            }
            else
            {
                PanoramaUiPanel* activeContext = GetActiveContext();

                if (g_CurrentContext &&
                    activeContext &&
                    g_CurrentContext == activeContext &&
                    g_CurrentPanelId[0] != '\0')
                {
                    ParkPanel(g_CurrentContext, g_CurrentPanelId);
                    g_CurrentPanelParked.store(true, std::memory_order_release);
                    g_FreezeComplete.store(false, std::memory_order_release);

                }
            }
        }

        if (g_CurrentTexture.load(std::memory_order_acquire))
            g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
        else
            g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);

        return;
    }

    PanoramaUiPanel* context = GetActiveContext();

    if (!context)
    {
        g_Status.store(systems::model_preview::Status::WaitingForPanel, std::memory_order_release);
        return;
    }

    // A newer request can arrive while the hidden candidate is still compositing. Drop
    // that candidate, keep the current frame visible and let ProcessPending build the
    // latest request instead.
    if (g_BuildActive.load(std::memory_order_acquire))
    {
        const std::uint64_t buildRevision =
            g_BuildRevision.load(std::memory_order_acquire);

        const std::uint64_t latestRevision =
            systems::preview_item::GetLatestRevision();

        if (buildRevision != 0 &&
            latestRevision != 0 &&
            buildRevision != latestRevision)
        {
            const bool staleWasComplex = g_BuildComplexCosmeticsValue;

            if (g_BuildContext == context && g_BuildPanelId[0] != '\0')
                DeletePanel(g_BuildContext, g_BuildPanelId);

            const ULONGLONG graceMs =
                staleWasComplex ? COMPLEX_RETIRE_GRACE_MS : SIMPLE_RETIRE_GRACE_MS;

            g_BuildPromoted.store(false, std::memory_order_release);
            g_BuildActive.store(false, std::memory_order_release);
            g_BuildComplexCosmetics.store(false, std::memory_order_release);
            g_BuildComplexCosmeticsValue = false;
            g_BuildPresentation = systems::model_preview::Presentation::Weapon;
            g_BuildContext = nullptr;
            g_BuildPanelId[0] = '\0';
            g_BuildTextureName[0] = '\0';
            g_BuildDefinitionIndex = 0;
        g_BuildItemId = 0;
            g_BuildRevision.store(0, std::memory_order_release);
            g_PreviewBinding.store(nullptr, std::memory_order_release);
            ClearStagedFrame();

            // DeleteAsync retires the scene asynchronously. Keep the temp-item slot locked
            // for a short grace period so rapid sticker edits cannot reuse it while the
            // discarded scene still references its material state.
            const ULONGLONG releaseAt = now + graceMs;
            if (g_RetireReleaseAt == 0 || releaseAt > g_RetireReleaseAt)
                g_RetireReleaseAt = releaseAt;

            if (g_CurrentTexture.load(std::memory_order_acquire))
                g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
            else
                g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);

            return;
        }
    }

    if (g_BuildPromoted.exchange(false, std::memory_order_acq_rel))
    {
        const bool oldWasComplex = g_AppliedComplexCosmetics;
        const bool hadOldPanel = g_PanelActive.load(std::memory_order_acquire);

        if (hadOldPanel &&
            g_CurrentContext &&
            g_CurrentContext == g_BuildContext &&
            g_CurrentPanelId[0] != '\0')
        {
            DeletePanel(g_CurrentContext, g_CurrentPanelId);
        }

        g_CurrentContext = g_BuildContext;
        g_AppliedDefinitionIndex = g_BuildDefinitionIndex;
        g_AppliedItemId = g_BuildItemId;
        g_AppliedRevision = g_BuildRevision.load(std::memory_order_acquire);
        g_AppliedComplexCosmetics = g_BuildComplexCosmeticsValue;
        g_AppliedPresentation = g_BuildPresentation;

        strncpy_s(g_CurrentPanelId, sizeof(g_CurrentPanelId), g_BuildPanelId, _TRUNCATE);

        g_PanelActive.store(true, std::memory_order_release);
        g_CurrentPanelParked.store(false, std::memory_order_release);
        g_BuildActive.store(false, std::memory_order_release);
        g_BuildComplexCosmetics.store(false, std::memory_order_release);
        g_PreviewBinding.store(nullptr, std::memory_order_release);

        g_BuildContext = nullptr;
        g_BuildPanelId[0] = '\0';
        g_BuildTextureName[0] = '\0';
        g_BuildDefinitionIndex = 0;
        g_BuildItemId = 0;
        g_BuildRevision.store(0, std::memory_order_release);
        g_BuildComplexCosmeticsValue = false;
        g_BuildPresentation = systems::model_preview::Presentation::Weapon;

        const bool complexRetirement =
            hadOldPanel && (oldWasComplex || g_AppliedComplexCosmetics);

        const ULONGLONG graceMs =
            complexRetirement ? COMPLEX_RETIRE_GRACE_MS : SIMPLE_RETIRE_GRACE_MS;

        g_RetireReleaseAt = now + graceMs;

        g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
        return;
    }

    if (g_BuildActive.load(std::memory_order_acquire))
    {
        if (g_RotationDirty.exchange(false, std::memory_order_acq_rel))
        {
            const float rotationX = g_RotationX.load(std::memory_order_acquire);
            const float rotationY = g_RotationY.load(std::memory_order_acquire);

            if (g_PanelActive.load(std::memory_order_acquire))
                ApplyRotation(g_CurrentContext, g_CurrentPanelId, g_AppliedPresentation, rotationX, rotationY);

            ApplyRotation(g_BuildContext, g_BuildPanelId, g_BuildPresentation, rotationX, rotationY);
        }

        if (g_CurrentTexture.load(std::memory_order_acquire))
            g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
        else
            g_Status.store(systems::model_preview::Status::WaitingForTexture, std::memory_order_release);

        return;
    }

    const std::uint16_t requestedDefinitionIndex =
        g_RequestedDefinitionIndex.load(std::memory_order_acquire);

    if (requestedDefinitionIndex == 0)
    {
        g_Status.store(systems::model_preview::Status::WaitingForRequest, std::memory_order_release);
        return;
    }

    const systems::preview_item::Result nativeItem = systems::preview_item::GetResult();

    if (!nativeItem.valid || nativeItem.itemId == 0)
    {
        if (!g_PanelActive.load(std::memory_order_acquire))
            g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);

        return;
    }

    if (!nativeItem.current)
    {
        // A stale candidate may already be inside DeleteAsync retirement. Keep the
        // native result locked until that grace expires; otherwise the next creation
        // can reuse one of Valve's two temp slots while the old scene still owns it.
        if (g_RetireReleaseAt != 0)
        {
            if (g_CurrentTexture.load(std::memory_order_acquire))
                g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
            else
                g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);

            return;
        }

        // If Panorama never consumed the obsolete item, the next temp-item allocation can
        // rotate back onto the ID used by the displayed panel. Freeze/park it first so
        // that reuse cannot change the visible frame.
        const bool hasDisplayedFrame =
            g_CurrentTexture.load(std::memory_order_acquire) != nullptr;

        if (hasDisplayedFrame &&
            g_PanelActive.load(std::memory_order_acquire) &&
            !g_CurrentPanelParked.load(std::memory_order_acquire))
        {
            if (!g_FreezeComplete.load(std::memory_order_acquire))
            {
                g_FreezeRequested.store(true, std::memory_order_release);
                return;
            }

            if (g_FreezeComplete.exchange(false, std::memory_order_acq_rel) &&
                g_CurrentContext == context)
            {
                ParkPanel(g_CurrentContext, g_CurrentPanelId);
                g_CurrentPanelParked.store(true, std::memory_order_release);
            }
        }

        if (systems::preview_item::IsResultLocked())
        {

            systems::preview_item::ReleaseForNextRequest();
        }

        if (g_CurrentTexture.load(std::memory_order_acquire))
            g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
        else
            g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);

        return;
    }

    const bool needsCandidate =
        !g_PanelActive.load(std::memory_order_acquire) ||
        context != g_CurrentContext ||
        nativeItem.itemId != g_AppliedItemId ||
        nativeItem.revision != g_AppliedRevision ||
        nativeItem.definitionIndex != g_AppliedDefinitionIndex ||
        nativeItem.presentation != g_AppliedPresentation;

    if (needsCandidate)
    {
        const bool hasDisplayedFrame =
            g_CurrentTexture.load(std::memory_order_acquire) != nullptr;

        if (hasDisplayedFrame &&
            g_PanelActive.load(std::memory_order_acquire) &&
            !g_CurrentPanelParked.load(std::memory_order_acquire) &&
            !g_FreezeComplete.load(std::memory_order_acquire))
        {
            g_FreezeRequested.store(true, std::memory_order_release);
            return;
        }

        if (g_FreezeComplete.exchange(false, std::memory_order_acq_rel) &&
            g_PanelActive.load(std::memory_order_acquire) &&
            g_CurrentContext == context)
        {
            ParkPanel(g_CurrentContext, g_CurrentPanelId);
            g_CurrentPanelParked.store(true, std::memory_order_release);
        }

        g_BuildContext = context;
        g_BuildDefinitionIndex = nativeItem.definitionIndex;
        g_BuildItemId = nativeItem.itemId;
        g_BuildRevision.store(nativeItem.revision, std::memory_order_release);
        g_BuildComplexCosmeticsValue = nativeItem.complexCosmetics;
        g_BuildPresentation = nativeItem.presentation;

        g_BuildComplexCosmetics.store(g_BuildComplexCosmeticsValue, std::memory_order_release);

        const std::uint32_t generation =
            g_ActiveGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

        std::snprintf(
            g_BuildPanelId,
            sizeof(g_BuildPanelId),
            "%s_%llX_%u",
            PANEL_ID_PREFIX,
            static_cast<unsigned long long>(g_PanelSessionId),
            generation);

        std::snprintf(
            g_BuildTextureName,
            sizeof(g_BuildTextureName),
            "%s_%llX_%u",
            TEXTURE_NAME_PREFIX,
            static_cast<unsigned long long>(g_PanelSessionId),
            generation);

        g_PreviewBinding.store(nullptr, std::memory_order_release);
        ClearStagedFrame();
        g_BuildPromoted.store(false, std::memory_order_release);
        g_BuildActive.store(true, std::memory_order_release);

        const int captureStickerSlot =
            g_PanelCaptureStickerSlot.load(std::memory_order_acquire);

        g_PreviewPanelCaptureArmed.store(
            captureStickerSlot >= 0,
            std::memory_order_release);

        const bool panelCreated = CreatePanel(
            context,
            g_BuildPanelId,
            g_BuildTextureName,
            nativeItem.itemId,
            nativeItem.presentation,
            nativeItem.modelPath,
            nativeItem.counterTerrorist,
            captureStickerSlot);

        if (!panelCreated)
        {
            g_PreviewPanelCaptureArmed.store(false, std::memory_order_release);
            g_BuildActive.store(false, std::memory_order_release);
            g_BuildComplexCosmetics.store(false, std::memory_order_release);
            g_BuildPanelId[0] = '\0';
            g_BuildTextureName[0] = '\0';
            g_BuildContext = nullptr;
            g_BuildRevision.store(0, std::memory_order_release);
            g_BuildComplexCosmeticsValue = false;
            g_BuildPresentation = systems::model_preview::Presentation::Weapon;

            if (g_CurrentTexture.load(std::memory_order_acquire))
                g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
            else
                g_Status.store(systems::model_preview::Status::WaitingForPanel, std::memory_order_release);

            return;
        }

        if (g_CurrentTexture.load(std::memory_order_acquire))
            g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
        else
            g_Status.store(systems::model_preview::Status::WaitingForTexture, std::memory_order_release);

        return;
    }

    if (g_RotationDirty.exchange(false, std::memory_order_acq_rel))
    {
        ApplyRotation(
            g_CurrentContext,
            g_CurrentPanelId,
            g_AppliedPresentation,
            g_RotationX.load(std::memory_order_acquire),
            g_RotationY.load(std::memory_order_acquire));
    }

    if (g_CurrentTexture.load(std::memory_order_acquire))
        g_Status.store(systems::model_preview::Status::Ready, std::memory_order_release);
    else
        g_Status.store(systems::model_preview::Status::WaitingForTexture, std::memory_order_release);
}

void systems::preview_panorama::TickRenderThread()
{
    if (!g_Initialized.load(std::memory_order_acquire))
        return;

    if (g_ResetFrameRequested.exchange(false, std::memory_order_acq_rel))
        ResetActiveFrame();

    if (g_FreezeRequested.load(std::memory_order_acquire))
    {
        FreezeCurrentFrame();
        g_FreezeRequested.store(false, std::memory_order_release);
    }

    // The hook callbacks usually stage immediately. This fallback handles cache-hit
    // cases where the ResourceBinding exists but no fresh CreateRenderTargets callback
    // occurs in the current Present interval.
    StageSrvFromBinding();
    PromoteStagedSrvIfReady();
}

void systems::preview_panorama::Request(const systems::model_preview::Request& request)
{
    const systems::model_preview::Presentation previousPresentation =
        g_RequestedPresentation.exchange(request.presentation, std::memory_order_acq_rel);
    const std::uint16_t previousDefinitionIndex =
        g_RequestedDefinitionIndex.exchange(request.definitionIndex, std::memory_order_acq_rel);

    const bool presentationChanged = previousPresentation != request.presentation;

    g_LastRequestAt.store(GetTickCount64(), std::memory_order_release);

    int captureStickerSlot = -1;
    int effectiveStickerCount = 0;

    if (request.presentation == systems::model_preview::Presentation::Weapon)
    {
        for (std::size_t slot = 0; slot < request.stickers.size(); ++slot)
        {
            if (!request.stickers[slot].enabled || request.stickers[slot].kit <= 0)
                continue;

            ++effectiveStickerCount;

            if (captureStickerSlot < 0)
                captureStickerSlot = static_cast<int>(slot);
        }
    }

    const int previousStickerCount =
        g_LastEffectiveStickerCount.exchange(effectiveStickerCount, std::memory_order_acq_rel);
    const std::uint16_t previousStickerDefinition =
        g_LastStickerDefinitionIndex.exchange(request.definitionIndex, std::memory_order_acq_rel);

    // CS2's sticker builder does not replace the existing composite when the new
    // sticker list is empty, so queue one native release for a same-weapon N -> 0.
    const bool finalStickerRemoved =
        request.presentation == systems::model_preview::Presentation::Weapon &&
        previousPresentation == systems::model_preview::Presentation::Weapon &&
        previousDefinitionIndex == request.definitionIndex &&
        previousStickerDefinition == request.definitionIndex &&
        previousStickerCount > 0 &&
        effectiveStickerCount == 0;

    if (finalStickerRemoved)
    {
        g_ZeroStickerReleasePending.store(true, std::memory_order_release);
    }

    g_PanelCaptureStickerSlot.store(captureStickerSlot, std::memory_order_release);

    // A texture from a different presentation must never be reused as a placeholder.
    // For example, switching from a weapon editor to a player/model editor used to show
    // the last gun for a frame or two while the new Panorama composition layer started.
    // Clear only on a presentation-class change; same-class requests still retain the
    // previous completed frame while their replacement builds.
    if (presentationChanged)
    {
        g_FreezeRequested.store(false, std::memory_order_release);
        g_FreezeComplete.store(false, std::memory_order_release);
        ResetActiveFrame();
        g_Status.store(systems::model_preview::Status::WaitingForItem, std::memory_order_release);
    }

    const std::uint64_t previousRevision = systems::preview_item::GetLatestRevision();
    systems::preview_item::Request(request);
    const std::uint64_t latestRevision = systems::preview_item::GetLatestRevision();

    // Request() runs from Present, so snapshot the displayed RT before the next client
    // frame can start rebuilding shared custom-material state. Same-presentation requests
    // keep that completed frame until the replacement is promoted.
    if (!presentationChanged &&
        latestRevision != 0 &&
        latestRevision != previousRevision &&
        g_CurrentTexture.load(std::memory_order_acquire) &&
        g_PanelActive.load(std::memory_order_acquire) &&
        !g_CurrentPanelParked.load(std::memory_order_acquire))
    {
        g_FreezeRequested.store(false, std::memory_order_release);
        g_FreezeComplete.store(false, std::memory_order_release);
        FreezeCurrentFrame();
    }
}

void systems::preview_panorama::SetRotation(float rotationX, float rotationY)
{
    rotationX = std::fmod(rotationX, 360.0f);

    if (rotationX < -180.0f)
        rotationX += 360.0f;
    else if (rotationX > 180.0f)
        rotationX -= 360.0f;

    rotationY = std::clamp(rotationY, -80.0f, 80.0f);

    g_RotationX.store(rotationX, std::memory_order_release);
    g_RotationY.store(rotationY, std::memory_order_release);
    g_RotationDirty.store(true, std::memory_order_release);
}

void systems::preview_panorama::KeepAlive()
{
    if (!g_Initialized.load(std::memory_order_acquire))
        return;

    g_LastRequestAt.store(GetTickCount64(), std::memory_order_release);
}

void systems::preview_panorama::Close()
{
    if (!g_Initialized.load(std::memory_order_acquire))
        return;

    // Request immediate teardown on the next main-thread preview tick. Reuse the
    // existing timeout cleanup path instead of touching Panorama from the WndProc.
    g_LastRequestAt.store(0, std::memory_order_release);
}

systems::model_preview::Frame systems::preview_panorama::GetFrame()
{
    systems::model_preview::Frame frame{};

    if (!g_PublishedPresentationValid.load(std::memory_order_acquire))
        return frame;

    const systems::model_preview::Presentation requestedPresentation =
        g_RequestedPresentation.load(std::memory_order_acquire);

    const systems::model_preview::Presentation publishedPresentation =
        g_PublishedPresentation.load(std::memory_order_acquire);

    if (publishedPresentation != requestedPresentation)
        return frame;

    frame.texture = g_CurrentTexture.load(std::memory_order_acquire);
    frame.width = g_CurrentTextureWidth.load(std::memory_order_acquire);
    frame.height = g_CurrentTextureHeight.load(std::memory_order_acquire);

    // Recheck publication validity after reading the SRV/dimensions. Promotion and
    // reset explicitly invalidate before mutating these fields.
    frame.valid =
        g_PublishedPresentationValid.load(std::memory_order_acquire) &&
        frame.texture != nullptr &&
        frame.width != 0 &&
        frame.height != 0;

    return frame;
}

systems::model_preview::Status systems::preview_panorama::GetStatus()
{
    const systems::model_preview::Status status = g_Status.load(std::memory_order_acquire);

    if (status == systems::model_preview::Status::Ready)
    {
        if (!g_PublishedPresentationValid.load(std::memory_order_acquire) ||
            g_PublishedPresentation.load(std::memory_order_acquire) !=
            g_RequestedPresentation.load(std::memory_order_acquire))
        {
            return systems::model_preview::Status::WaitingForTexture;
        }
    }

    return status;
}

const char* systems::preview_panorama::GetStatusText()
{
    switch (GetStatus())
    {
    case systems::model_preview::Status::MissingPatterns:
        return "3D preview: Panorama signatures are out of date.";
    case systems::model_preview::Status::WaitingForRequest:
        return "3D preview: waiting for preview request...";
    case systems::model_preview::Status::WaitingForItem:
        return "3D preview: building native preview item...";
    case systems::model_preview::Status::WaitingForPanel:
        return "3D preview: waiting for active Panorama root...";
    case systems::model_preview::Status::WaitingForTexture:
        return "3D preview: waiting for Panorama composition texture...";
    case systems::model_preview::Status::Ready:
        return "3D preview: ready";
    default:
        return "3D preview: not initialized.";
    }
}
