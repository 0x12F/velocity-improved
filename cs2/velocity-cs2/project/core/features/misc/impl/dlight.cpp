#include <pch/pch.hpp>

#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>

#include <core/settings.hpp>
#include <core/systems/systems.hpp>
#include <protection/game_addresses.hpp>

#include "../misc.hpp"

namespace features::misc {

	namespace {

		constexpr std::uintptr_t k_min_ptr = 0x10000000ull;
		constexpr std::uintptr_t k_max_ptr = 0x7FFFFFFFFFFFull;
		constexpr std::uint32_t k_light_key = 0x564C4954u; // "VLIT"
		constexpr std::uint32_t k_color_exponent = 5u;
		constexpr float k_rgb_channel_scale = 1.0f / 255.0f;
		constexpr float k_entry_lifetime = 5.0f;

		[[nodiscard]] inline bool valid_ptr( std::uintptr_t p ) noexcept
		{
			return p >= k_min_ptr && p <= k_max_ptr;
		}

	} // namespace

	void dlight::on_present( )
	{
		const std::lock_guard lock( this->m_mutex );

		// Settings are edited on Present. Publish a coherent snapshot for the game
		// thread without touching the engine's light manager from the render thread.
		const auto& cfg = settings::g_misc.m_dlight;
		const auto& c = cfg.color.value;
		this->m_config.enabled = cfg.enabled.value;
		this->m_config.packed_color = static_cast<std::uint32_t>( c.r )
			| ( static_cast<std::uint32_t>( c.g ) << 8u )
			| ( static_cast<std::uint32_t>( c.b ) << 16u )
			| ( k_color_exponent << 24u );
		this->m_config.radius = std::max( cfg.radius.value, 1.0f );
		this->m_config.z_offset = cfg.z_offset.value;
	}

	void dlight::apply_scene_color( std::uintptr_t object ) const
	{
		if ( !object || this->m_scene_object.load( std::memory_order_acquire ) != object )
		{
			return;
		}

		const auto packed = this->m_packed_color.load( std::memory_order_relaxed );
		const auto exponent = static_cast<std::int8_t>( packed >> 24u );
		const auto scale = std::ldexp( 1.0f, exponent )
			* this->m_scene_color_scale.load( std::memory_order_relaxed )
			* k_rgb_channel_scale;
		// Source 2 normalizes RGBExp32 byte channels, then scales them by radius
		// when deriving linear attenuation. Preserve both parts for only our light.
		const math::vector3 color{
			static_cast<float>( packed & 0xFFu ) * scale,
			static_cast<float>( ( packed >> 8u ) & 0xFFu ) * scale,
			static_cast<float>( ( packed >> 16u ) & 0xFFu ) * scale };

		(void) memory::safe_write<math::vector3>( object + 0xE4, color );
	}

	void dlight::retire_entry( )
	{
		if ( valid_ptr( this->m_manager ) && valid_ptr( this->m_entry )
			&& memory::safe_read<std::uint32_t>( this->m_entry + 0x24 ).value_or( 0 ) == k_light_key )
		{
			// The allocator owns the manager lock and releases the scene object
			// attached to an existing keyed entry before clearing the slot.
			if ( const auto alloc_light = PATTERN (patterns::dynamic_light_alloc) )
			{
				(void) memory::call<std::uintptr_t>( alloc_light, this->m_manager, k_light_key, 0 );
			}
		}

		this->m_manager = 0;
		this->m_entry = 0;
		this->m_scene_object.store( 0, std::memory_order_release );
		this->m_packed_color.store( 0, std::memory_order_relaxed );
		this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
		this->m_logged_submission = false;
		this->m_logged_scene = false;
	}

	void dlight::on_level_shutdown( )
	{
		const std::lock_guard lock( this->m_mutex );

		// Retire while the scene system is still alive. Keeping the engine entry
		// across teardown leaves its queued scene object pointing at a dead parent.
		this->retire_entry( );
	}

	void dlight::on_frame_stage_notify( )
	{
		const std::lock_guard lock( this->m_mutex );

		const auto cfg = this->m_config;
		const auto manager_slot = PATTERN (patterns::dynamic_light_manager);
		const auto manager = manager_slot
			? memory::safe_read<std::uintptr_t>( manager_slot ).value_or( 0 )
			: 0;
		const auto alloc_light = PATTERN (patterns::dynamic_light_alloc);
		const auto light_time = PATTERN (patterns::dynamic_light_time);

		if ( !cfg.enabled )
		{
			if ( manager == this->m_manager )
			{
				this->retire_entry( );
			}
			else
			{
				this->m_manager = 0;
				this->m_entry = 0;
				this->m_scene_object.store( 0, std::memory_order_release );
				this->m_packed_color.store( 0, std::memory_order_relaxed );
				this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
			}
			return;
		}

		if ( !valid_ptr( manager ) || !alloc_light || !light_time )
		{
			this->m_manager = 0;
			this->m_entry = 0;
			this->m_scene_object.store( 0, std::memory_order_release );
			this->m_packed_color.store( 0, std::memory_order_relaxed );
			this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.controller || !local.pawn || !local.is_alive )
		{
			if ( manager == this->m_manager )
			{
				this->retire_entry( );
			}
			else
			{
				this->m_manager = 0;
				this->m_entry = 0;
				this->m_scene_object.store( 0, std::memory_order_release );
				this->m_packed_color.store( 0, std::memory_order_relaxed );
				this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
			}
			return;
		}

		const auto entry_key = valid_ptr( this->m_entry )
			? memory::safe_read<std::uint32_t>( this->m_entry + 0x24 ).value_or( 0 )
			: 0;

		if ( manager != this->m_manager || entry_key != k_light_key )
		{
			this->m_scene_object.store( 0, std::memory_order_release );
			this->m_packed_color.store( 0, std::memory_order_relaxed );
			this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
			this->m_entry = memory::call<std::uintptr_t>( alloc_light, manager, k_light_key, 0 );
			this->m_manager = manager;
		}

		if ( !valid_ptr( this->m_entry ) )
		{
			return;
		}

		const auto game_scene = memory::safe_read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		const auto feet = valid_ptr( game_scene )
			? memory::safe_read<math::vector3>( game_scene + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) )
			: std::nullopt;
		if ( !feet || !std::isfinite( feet->x ) || !std::isfinite( feet->y ) || !std::isfinite( feet->z ) )
		{
			return;
		}

		float curtime{};
		memory::call<float*>( light_time, &curtime, 0 );

		const math::vector3 origin{
			feet->x,
			feet->y,
			feet->z + cfg.z_offset };
		const auto write_ok = memory::safe_write<math::vector3>( this->m_entry + 0x04, origin )
			&& memory::safe_write<float>( this->m_entry + 0x10, cfg.radius )
			&& memory::safe_write<std::uint32_t>( this->m_entry + 0x14, cfg.packed_color )
			// Update before Source 2 copies the entry into its scene-light descriptor.
			&& memory::safe_write<float>( this->m_entry + 0x18, curtime + k_entry_lifetime )
			&& memory::safe_write<float>( this->m_entry + 0x1C, 0.0f );

		if ( !write_ok )
		{
			this->m_manager = 0;
			this->m_entry = 0;
			this->m_scene_object.store( 0, std::memory_order_release );
			this->m_packed_color.store( 0, std::memory_order_relaxed );
			this->m_scene_color_scale.store( 0.0f, std::memory_order_relaxed );
			this->m_logged_submission = false;
			this->m_logged_scene = false;
			return;
		}

		if ( !this->m_logged_submission )
		{
			logging::console::print(
				xs( "[dlight] submitted; manager={:#x} entry={:#x} origin=({:.1f} {:.1f} {:.1f}) radius={:.0f} time={:.2f}" ),
				manager, this->m_entry, origin.x, origin.y, origin.z, cfg.radius, curtime );
			this->m_logged_submission = true;
		}

		this->m_packed_color.store( cfg.packed_color, std::memory_order_relaxed );
		this->m_scene_color_scale.store( cfg.radius, std::memory_order_relaxed );
		const auto scene_object = memory::safe_read<std::uintptr_t>( this->m_entry + 0x48 ).value_or( 0 );
		this->m_scene_object.store( scene_object, std::memory_order_release );
		if ( !this->m_logged_scene && scene_object )
		{
			logging::console::print( xs( "[dlight] scene object active" ) );
			this->m_logged_scene = true;
		}
	}

} // namespace features::misc
