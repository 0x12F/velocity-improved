#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/random/random.hpp>
#include <utilities/logging/logging.hpp>

#include "../systems.hpp"

namespace systems {

	//void legit_input::press( std::uintptr_t button_bit, float when )
	//{
	//	this->m_pending_press = true;
	//	this->m_press_button = button_bit;
	//	this->m_press_when = when;
	//}

	//void legit_input::release( std::uintptr_t button_bit, float when )
	//{
	//	this->m_pending_release = true;
	//	this->m_release_button = button_bit;
	//	this->m_release_when = when;
	//}

	void legit_input::add_mouse_delta( float pitch_degrees, float yaw_degrees )
	{
		this->m_pending_pitch += pitch_degrees;
		this->m_pending_yaw += yaw_degrees;
	}

	//void legit_input::on_create_move( input::usercmd* cmd )
	//{
	//	struct engine_event
	//	{
	//		std::uintptr_t button_bit;
	//		std::uint8_t state;
	//		std::uint8_t pad0[ 3 ];
	//		float when;
	//		float analog_fwd;
	//		float analog_left;
	//		float pitch_delta;
	//		float yaw_delta;
	//	};

	//	if ( !this->m_pending_press && !this->m_pending_release )
	//	{
	//		return;
	//	}

	//	constexpr auto slot{ 0u };

	//	auto base = cmd->csgo_user_cmd.mutable_base( );
	//	if ( !base )
	//	{
	//		this->m_pending_press = false;
	//		this->m_pending_release = false;
	//		return;
	//	}

	//	auto subtick_moves = base->mutable_subtick_moves( );
	//	const auto sb = addresses::globals::csgo_input + 600 + 2344 * slot;
	//	auto& event_count = *reinterpret_cast< int* >( sb + 44 );
	//	auto events = reinterpret_cast< engine_event* >( sb + 64 );

	//	if ( this->m_pending_press )
	//	{
	//		auto when = this->m_press_when;
	//		if ( when < 0.0f || when >= 1.0f )
	//		{
	//			when = this->resolve_when( );
	//		}

	//		if ( event_count < 64 )
	//		{
	//			engine_event ev{};
	//			ev.button_bit = this->m_press_button;
	//			ev.state = 1;
	//			ev.when = when;
	//			events[ event_count ] = ev;
	//			event_count++;
	//		}

	//		//memory::write<int>( addresses::globals::csgo_input + 3008, -1 );

	//		memory::call<void>( addresses::functions::read_frame_input, addresses::globals::csgo_input, slot );

	//		const auto frame_count = memory::read<int>( addresses::globals::csgo_input + 3016 );
	//		const auto frame_data = memory::read<std::uintptr_t>( addresses::globals::csgo_input + 3024 );

	//		if ( frame_count > 0 )
	//		{
	//			const auto new_entry_idx = frame_count - 1;
	//			const auto frame_entry_ptr = frame_data + 96 * static_cast< std::size_t >( new_entry_idx );

	//			constexpr auto nan_dbl{ std::numeric_limits<double>::quiet_NaN( ) };
	//			constexpr auto nan_int{ 0x7fc00000 };

	//			memory::call<std::uintptr_t>( addresses::functions::append_input_history_entry, reinterpret_cast< std::uintptr_t >( cmd ), frame_entry_ptr, 1, nan_dbl, nan_int, systems::g_local.pawn( ) );
	//		}

	//		const auto attack_frame_idx = memory::read<int>( addresses::globals::csgo_input + 3008 );
	//		if ( attack_frame_idx != -1 && frame_count > 0 )
	//		{
	//			const auto attack_frame_ptr = frame_data + 96 * static_cast< std::size_t >( attack_frame_idx );
	//			const auto cmd_local_idx = memory::read<int>( attack_frame_ptr + 84 );
	//			cmd->csgo_user_cmd.set_attack1_start_history_index( cmd_local_idx );
	//		}

	//		auto step = g_input.acquire_subtick_step( subtick_moves );
	//		if ( step )
	//		{
	//			step->set_button( this->m_press_button );
	//			step->set_pressed( true );
	//			step->set_when( when );
	//		}

	//		cmd->buttons.value |= this->m_press_button;
	//		cmd->buttons.value_changed |= this->m_press_button;
	//		cmd->buttons.value_scroll |= this->m_press_button;

	//		this->m_pending_press = false;
	//	}

	//	if ( this->m_pending_release )
	//	{
	//		auto when = this->m_release_when;
	//		if ( when < 0.0f || when >= 1.0f )
	//		{
	//			when = this->resolve_when( );
	//		}

	//		auto step = g_input.acquire_subtick_step( subtick_moves );
	//		if ( step )
	//		{
	//			step->set_button( this->m_release_button );
	//			step->set_pressed( false );
	//			step->set_when( when );
	//		}

	//		cmd->buttons.value &= ~this->m_release_button;
	//		cmd->buttons.value_changed |= this->m_release_button;
	//		cmd->buttons.value_scroll &= ~this->m_release_button;

	//		this->m_pending_release = false;
	//	}
	//}

	void legit_input::on_process_input_event( std::uintptr_t csgo_input, int slot )
	{
		if ( slot != 0 )
		{
			return;
		}

		if ( this->m_pending_pitch == 0.0f && this->m_pending_yaw == 0.0f )
		{
			return;
		}

		auto& va_pitch = *reinterpret_cast< float* >( csgo_input + 1672 );
		auto& va_yaw = *reinterpret_cast< float* >( csgo_input + 1676 );

		va_pitch += this->m_pending_pitch;
		va_yaw += this->m_pending_yaw;
		va_pitch = std::clamp( va_pitch, -89.0f, 89.0f );

		this->m_pending_pitch = 0.0f;
		this->m_pending_yaw = 0.0f;
	}

	//float legit_input::resolve_when( ) const
	//{
	//	const auto now = memory::call<double>( addresses::functions::plat_float_time );
	//	const auto tick = static_cast< int >( now / cstypes::tick_interval );
	//	const auto tick_start = static_cast< double >( tick ) * cstypes::tick_interval;
	//	auto frac = static_cast< float >( ( now - tick_start ) / cstypes::tick_interval );

	//	if ( frac < 0.0f )
	//	{
	//		frac = 0.0f;
	//	}

	//	if ( frac >= 1.0f )
	//	{
	//		frac = std::nextafter( 1.0f, 0.0f );
	//	}

	//	return frac;
	//}

} // namespace systems