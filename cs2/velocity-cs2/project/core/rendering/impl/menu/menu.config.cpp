#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		std::string search_buf{};
		std::vector<std::wstring> config_list{};
		auto selected{ -1 };
		auto needs_refresh{ true };
		auto confirm_delete{ false };
		auto confirm_reset{ false };
		auto confirm_timer{ 0.0f };

		static inline bool copy_to_clipboard( const std::string& text )
		{
			if ( !OpenClipboard( nullptr ) )
			{
				return false;
			}

			EmptyClipboard( );

			const auto size = ( text.size( ) + 1 ) * sizeof( char );

			auto mem = GlobalAlloc( GMEM_MOVEABLE, size );
			if ( !mem )
			{
				CloseClipboard( );
				return false;
			}

			auto dest = GlobalLock( mem );
			if ( dest )
			{
				std::memcpy( dest, text.c_str( ), size );
				GlobalUnlock( mem );
			}

			SetClipboardData( CF_TEXT, mem );
			CloseClipboard( );
			return true;
		}

		static inline std::string paste_from_clipboard( )
		{
			if ( !OpenClipboard( nullptr ) )
			{
				return {};
			}

			std::string result{};

			auto mem = GetClipboardData( CF_TEXT );
			if ( mem )
			{
				auto data = static_cast< const char* >( GlobalLock( mem ) );
				if ( data )
				{
					result = data;
					GlobalUnlock( mem );
				}
			}

			CloseClipboard( );
			return result;
		}

		static inline void wide_to_utf8( const std::wstring& wide, char* out, int out_size )
		{
			WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), -1, out, out_size, nullptr, nullptr );
		}

		static inline std::wstring utf8_to_wide( const std::string& utf8 )
		{
			wchar_t buf[ 128 ]{};
			MultiByteToWideChar( CP_UTF8, 0, utf8.c_str( ), -1, buf, 128 );
			return buf;
		}

		static inline bool config_matches_search( const std::wstring& wname )
		{
			if ( detail::search_buf.empty( ) )
			{
				return true;
			}

			char narrow[ 128 ]{};
			wide_to_utf8( wname, narrow, sizeof( narrow ) );

			std::string lower_name{ narrow };
			std::string lower_search{ detail::search_buf };

			for ( auto& c : lower_name )
			{
				c = static_cast< char >( std::tolower( c ) );
			}

			for ( auto& c : lower_search )
			{
				c = static_cast< char >( std::tolower( c ) );
			}

			return lower_name.find( lower_search ) != std::string::npos;
		}

		static inline std::string selected_name( )
		{
			if ( detail::selected < 0 || detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				return {};
			}

			char narrow[ 128 ]{};
			wide_to_utf8( detail::config_list[ detail::selected ], narrow, sizeof( narrow ) );
			return narrow;
		}

		static inline void reset_defaults( )
		{
			auto& reg = config::detail::get_registry( );

			for ( auto& f : reg.fields )
			{
				char key_str[ 12 ];
				std::snprintf( key_str, sizeof( key_str ), "%08x", f.key );

				auto def = reg.defaults.find( key_str );
				if ( def != reg.defaults.end( ) )
				{
					config::serial::json_to_field( *def, f );
				}
			}

			settings::finalize_binds( );
		}

	} // namespace detail

	void menu::draw_config( float group_w )
	{
		( void )group_w;

		if ( detail::needs_refresh )
		{
			detail::config_list = config::registry::list( );
			detail::needs_refresh = false;

			if ( detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				detail::selected = -1;
			}
		}

		const auto dt = xdraw::delta_time( );

		if ( detail::confirm_delete || detail::confirm_reset )
		{
			detail::confirm_timer += dt;

			if ( detail::confirm_timer > 3.0f )
			{
				detail::confirm_delete = false;
				detail::confirm_reset = false;
			}
		}

		auto& dl = xui::draw::current( );
		const auto& s = xui::ctx( ).style;
		const auto& input = xui::ctx( ).input;

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		if ( !xui::begin_child( "##cfg_panel", this->m_body_w, this->m_body_h, false ) )
		{
			return;
		}

		xui::text_input( "##cfg_search", detail::search_buf, 64, "search configs..." );

		constexpr auto btn_h{ 28.0f };
		const auto [ avail_w, avail_h ] = xui::layout::avail( );
		const auto list_h = std::max( 80.0f, avail_h - btn_h - s.item_spacing_y );

		if ( xui::begin_child( "##cfg_list", avail_w, list_h, true ) )
		{
			const auto row_w = xui::layout::avail( ).first;
			constexpr auto row_h{ 28.0f };
			auto visible_rows{ 0 };

			for ( auto i = 0; i < static_cast< int >( detail::config_list.size( ) ); ++i )
			{
				const auto& wname = detail::config_list[ i ];

				if ( !detail::config_matches_search( wname ) )
				{
					continue;
				}

				char narrow[ 128 ]{};
				detail::wide_to_utf8( wname, narrow, sizeof( narrow ) );

				const auto row = xui::layout::item( row_w, row_h );
				const auto is_selected = ( detail::selected == i );
				const auto is_hovered = input.in_rect( row );

				if ( is_hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
				{
					detail::selected = i;
					detail::confirm_delete = false;
					detail::confirm_reset = false;
					config::registry::load( wname );
					settings::finalize_binds( );
				}

				const auto hover_anim = xui::anim::lerp( xui::fnv1a( "cfgrow" ) + i, is_hovered ? 1.0f : 0.0f, 14.0f );
				const auto sel_anim = xui::anim::lerp( xui::fnv1a( "cfgsel" ) + i, is_selected ? 1.0f : 0.0f, 10.0f );

				if ( sel_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_accent.alpha( static_cast< std::uint8_t >( 70.0f * sel_anim ) ), xdraw::corner_radius{ 6.0f } );
				}
				else if ( hover_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_elevated.alpha( static_cast< std::uint8_t >( 255.0f * hover_anim * 0.5f ) ), xdraw::corner_radius{ 6.0f } );
				}

				const auto [ tw, th ] = xdraw::measure_text( narrow );
				const auto text_col = is_selected
					? xui::lerp( tokens::col_text, tokens::col_accent, sel_anim )
					: xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );

				dl.text( row.x + 10.0f, row.y + ( row.h - th ) * 0.5f, narrow, text_col );

				visible_rows++;
			}

			if ( visible_rows == 0 )
			{
				const auto row = xui::layout::item( row_w, row_h );
				dl.text( row.x + 10.0f, row.y + 6.0f, detail::config_list.empty( ) ? "no configs found" : "no matches", tokens::col_text_dim );
			}

			xui::end_child( );
		}

		const auto btn_w = ( avail_w - s.item_spacing_x * 4.0f ) / 5.0f;
		const auto has_selection = detail::selected >= 0 && detail::selected < static_cast< int >( detail::config_list.size( ) );
		const auto save_name = has_selection ? detail::selected_name( ) : detail::search_buf;
		const auto can_save = !save_name.empty( );

		if ( xui::button( "save", btn_w, btn_h ) && can_save )
		{
			config::registry::save( detail::utf8_to_wide( save_name ) );
			detail::needs_refresh = true;
		}

		xui::layout::same_line( );

		if ( detail::confirm_reset )
		{
			if ( xui::button( "confirm", btn_w, btn_h ) )
			{
				detail::reset_defaults( );
				detail::confirm_reset = false;
			}
		}
		else if ( xui::button( "reset", btn_w, btn_h ) )
		{
			detail::confirm_reset = true;
			detail::confirm_delete = false;
			detail::confirm_timer = 0.0f;
		}

		xui::layout::same_line( );

		if ( detail::confirm_delete )
		{
			if ( xui::button( "confirm", btn_w, btn_h ) && has_selection )
			{
				config::registry::remove( detail::config_list[ detail::selected ] );
				detail::selected = -1;
				detail::needs_refresh = true;
				detail::confirm_delete = false;
			}
		}
		else if ( xui::button( "delete", btn_w, btn_h ) && has_selection )
		{
			detail::confirm_delete = true;
			detail::confirm_reset = false;
			detail::confirm_timer = 0.0f;
		}

		xui::layout::same_line( );

		if ( xui::button( "import", btn_w, btn_h ) )
		{
			const auto clip = detail::paste_from_clipboard( );
			if ( !clip.empty( ) )
			{
				const auto result = config::import_auto( clip );
				if ( result.success )
				{
					settings::finalize_binds( );

					std::wstring wname{};

					if ( !result.name.empty( ) )
					{
						wname = detail::utf8_to_wide( result.name );
					}
					else
					{
						SYSTEMTIME st{};
						GetLocalTime( &st );
						wchar_t buf[ 128 ]{};
						std::swprintf( buf, 128, L"import_%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
						wname = buf;
					}

					config::registry::save( wname );
					detail::needs_refresh = true;
				}
			}
		}

		xui::layout::same_line( );

		if ( xui::button( "export", btn_w, btn_h ) )
		{
			const auto name = has_selection ? detail::selected_name( ) : detail::search_buf;
			const auto code = config::export_share_words( name );
			if ( !code.empty( ) )
			{
				detail::copy_to_clipboard( code );
			}
		}

		xui::end_child( );
	}

} // namespace rendering
