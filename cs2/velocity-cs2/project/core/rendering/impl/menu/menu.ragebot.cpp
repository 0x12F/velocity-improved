#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "paws" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up" };

	} // namespace detail

	void menu::draw_ragebot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& lg = s.m_lagcomp;

		auto& wg = rb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_aimbot", col_w ) )
		{
			xui::checkbox( "enabled", rb.enabled );
			xui::checkbox( "silent", wg.silent );
			xui::checkbox( "no spread", wg.no_spread );
			xui::checkbox( "double tap", wg.doubletap );
			xui::checkbox( "force shot in air", wg.force_shot_air );
			xui::checkbox( "force shot on ground", wg.force_shot );
			xui::checkbox( "extrapolation", lg.extrapolation );
			xui::slider_int( "max extrapolate", lg.max_extrapolate_ticks, 1, 16, "%d tick(s)" );
			xui::slider_float( "max fov", wg.max_fov, 1.0f, 180.0f, "%.0f°" );

			xui::slider_int( "hit chance", wg.hitchance, 25, 100, "%d%%" );
			xui::slider_int( "min damage", wg.min_damage, 5, 125, "%d" );
			/*xui::slider_int( "max backtrack", s.m_lagcomp.max_backtrack_ticks, 1, 16, "%d tick(s)" );*/

			xui::checkbox( "hit chance override", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "value##hc", wg.hitchance_override_value, 0, 100, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "min damage override", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				xui::slider_int( "value##md", wg.min_damage_override_value, 0, 130, "%d" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_extras", col_w, 190.0f, true ) )
		{
			xui::checkbox( "force b-aim", wg.body_aim );
		xui::checkbox( "dynamic point scale", wg.dynamic_pointscale );
		xui::checkbox( "debug multipoints", wg.debug_multipoints );
		xui::slider_float( "point scale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
		xui::multicombo( "hitboxes", wg.hitboxes, detail::hitbox_names, 6 );

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_antiaim", col_w ) )
		{
			xui::checkbox( "anti aim", aa.enabled );

			xui::combo( "pitch", aa.pitch.value, detail::pitch_items, 3 );

			xui::checkbox( "compensate roll", aa.auto_yaw_adjust );
			xui::checkbox( "force left", aa.manual_left );
			xui::checkbox( "force right", aa.manual_right );
			xui::checkbox( "hide onshot", aa.hide_shots );
			xui::checkbox( "avoid backstab", aa.avoid_backstab );
			xui::checkbox( "direction indicator", aa.direction_indicator );

			if ( xui::begin_popup( "##aa_indicator", 220.0f ) )
			{
				xui::color_picker( "color##aa_ind", aa.direction_indicator_color );
				xui::checkbox( "glow##aa_ind", aa.direction_indicator_glow );
				xui::slider_float( "glow strength##aa_ind", aa.direction_indicator_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_otherbots", col_w ) )
		{
			xui::checkbox( "auto revolver", autos.revolver );

			xui::checkbox( "zeusbot", zb.enabled );
			if ( xui::begin_popup( "##zb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##zb", zb.max_fov, 1, 180, "%.0f°" );
				xui::checkbox( "drop after##zb", zb.drop_after );
				xui::end_popup( );
			}

			xui::checkbox( "knifebot", kb.enabled );
			if ( xui::begin_popup( "##kb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##kb", kb.max_fov, 1, 180, "%.0f°" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_peek", col_w ) )
		{
			xui::checkbox( "quick peek", qp.enabled );
			if ( xui::begin_popup( "##qp_colors", 220.0f ) )
			{
				xui::color_picker( "base color##qp", qp.color );
				xui::color_picker( "retracting color##qp", qp.retrack_color );
				xui::end_popup( );
			}

			xui::checkbox( "duck peek", dp.enabled );

			xui::end_child( );
		}
	}

} // namespace rendering
