#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include "../runtime.hpp"
#include "../engine.hpp"
#include "../skinchanger.hpp"
#include "../glove_changer.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace features::changer::runtime {
    std::vector<sticker_kit> load_sticker_catalog();
    namespace {
        constexpr int frame_render_end = 6;
        constexpr int frame_net_update_end = 7;

        struct configuration
        {
            features::changer::cosmetic_config::skin_changer_t skins;
            std::uint16_t ct_agent{};
            std::uint16_t t_agent{};
        };

        std::atomic_bool ready{};
        std::mutex pending_mutex;
        std::optional<configuration> pending;
        configuration current;
        std::vector<sticker_kit> stickers;
        bool was_connected{};
        bool preview_initialized{};
        std::uint16_t preview_definition{};

        void update_local_pawn()
        {
            const bool connected = engine::connected();
            if (was_connected && !connected)
                features::changer::application::Reset();
            was_connected = connected;
        }

        void consume_configuration()
        {
            std::optional<configuration> next;
            {
                std::scoped_lock lock(pending_mutex);
                next.swap(pending);
            }
            if (!next)
                return;

            current = std::move(*next);
            features::changer::cosmetic_config::skin_changer = current.skins;
            force_update();
        }
    }

    bool initialize()
    {
        engine::allocate_attributes = reinterpret_cast<decltype(engine::allocate_attributes)>(
            memory::get_module_export_with_base(addresses::modules::tier0, "MemAlloc_AllocFunc"));
        engine::free_attributes = reinterpret_cast<decltype(engine::free_attributes)>(
            memory::get_module_export_with_base(addresses::modules::tier0, "MemAlloc_FreeFunc"));
        if (!engine::allocate_attributes || !engine::free_attributes || g_econ_item_system.item_defs().empty())
            return false;
        stickers = load_sticker_catalog();
        ready.store(true, std::memory_order_release);
        return true;
    }

    void initialize_preview(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        if (!ready.load(std::memory_order_acquire) || preview_initialized)
            return;

        systems::model_preview::Initialize(device, context);
        preview_initialized = true;
    }

    void shutdown()
    {
        ready.store(false, std::memory_order_release);
        systems::model_preview::Shutdown();
        preview_initialized = false;
        features::changer::application::Reset();
    }

    void configure(features::changer::cosmetic_config::skin_changer_t config, std::uint16_t ct_agent, std::uint16_t t_agent)
    {
        std::scoped_lock lock(pending_mutex);
        pending = configuration{std::move(config), ct_agent, t_agent};
    }

    void before_frame_stage(int stage)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        if (stage == frame_net_update_end)
        {
            consume_configuration();
            update_local_pawn();
            features::changer::application::Run();
        }
        if (stage == frame_render_end)
        {
            update_local_pawn();
            const auto pawn = engine::local_pawn();
            const auto team = pawn ? pawn->getTeam() : engine::team::UNASSIGNED;
            features::changer::cosmetic_config::skin_changer.agentDefinition = team == engine::team::CT ? current.ct_agent : current.t_agent;
            features::changer::application::RunAgentChanger();
        }
    }

    void after_frame_stage(int stage)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        if (stage == frame_net_update_end)
        {
            features::changer::application::ProcessPendingKeychains();
            update_local_pawn();
        }
        if (stage == frame_render_end)
            systems::model_preview::ProcessMainThread();
    }

    void tick_render() { systems::model_preview::TickRenderThread(); }
    void set_interactive_edit(bool active) { features::changer::application::SetInteractiveEditActive(active); }
    void force_update()
    {
        features::changer::application::ForceUpdate();
        features::changer::application::ForceAgentUpdate();
        features::changer::glove_application::ForceUpdate();
    }

    const std::vector<sticker_kit>& sticker_kits() { return stickers; }

    void preview_item(std::uint16_t definition, const features::changer::cosmetic_config::item_skin_t& skin, bool agent, bool editing)
    {
        if (!ready.load(std::memory_order_acquire))
            return;
        if (editing && definition == preview_definition)
        {
            systems::model_preview::KeepAlive();
            return;
        }
        systems::model_preview::Submit(agent ? systems::model_preview::MakeAgentRequest(definition) : systems::model_preview::MakeWeaponRequest(definition, skin));
        preview_definition = definition;
    }

    void close_preview()
    {
        systems::model_preview::Close();
        preview_definition = 0;
    }
} // namespace features::changer::runtime
