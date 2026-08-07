#define NOMINMAX
#include <pch/pch.hpp>
#include "xui.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {

	struct overlay_storage
	{
		std::vector<std::unique_ptr<xui::overlay>> list{};
		std::unordered_set<std::uintptr_t> touched_this_frame{};
		std::unordered_set<std::uintptr_t> touched_last_frame{};
	};

	static overlay_storage& get_overlays( )
	{
		static overlay_storage s{};
		return s;
	}

	static std::unordered_map<std::uintptr_t, float>& get_anim_states( )
	{
		static std::unordered_map<std::uintptr_t, float> s{};
		return s;
	}

	struct double_click_state
	{
		std::chrono::steady_clock::time_point last_time{};
		float last_x{};
		float last_y{};
	};

	static double_click_state& get_double_click( )
	{
		static double_click_state s{};
		return s;
	}

	static xui::context& get_ctx( )
	{
		static xui::context c{};
		return c;
	}

	static HWND& get_hwnd( )
	{
		static HWND h{ nullptr };
		return h;
	}

	struct highlight_state
	{
		std::string label{};
		std::chrono::steady_clock::time_point expires_at{};
	};

	static highlight_state& get_highlight_state( )
	{
		static highlight_state s{};
		return s;
	}

	static bool clipboard_copy( HWND hwnd, const std::string& text )
	{
		if ( !OpenClipboard( hwnd ) )
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

	static std::string clipboard_paste( HWND hwnd )
	{
		if ( !OpenClipboard( hwnd ) )
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

	static float* resolve_style_var( xui::style& s, xui::style_var var )
	{
		switch ( var )
		{
		case xui::style_var::rounding: return &s.rounding;
		case xui::style_var::checkbox_rounding: return &s.checkbox_rounding;
		case xui::style_var::slider_rounding: return &s.slider_rounding;
		case xui::style_var::button_rounding: return &s.button_rounding;
		case xui::style_var::keybind_rounding: return &s.keybind_rounding;
		case xui::style_var::combo_rounding: return &s.combo_rounding;
		case xui::style_var::popup_rounding: return &s.popup_rounding;
		case xui::style_var::combo_popup_rounding: return &s.combo_popup_rounding;
		case xui::style_var::picker_popup_rounding: return &s.picker_popup_rounding;
		case xui::style_var::color_swatch_rounding: return &s.color_swatch_rounding;
		case xui::style_var::text_input_rounding: return &s.text_input_rounding;
		case xui::style_var::window_pad_x: return &s.window_pad_x;
		case xui::style_var::window_pad_y: return &s.window_pad_y;
		case xui::style_var::item_spacing_x: return &s.item_spacing_x;
		case xui::style_var::item_spacing_y: return &s.item_spacing_y;
		case xui::style_var::frame_pad_x: return &s.frame_pad_x;
		case xui::style_var::frame_pad_y: return &s.frame_pad_y;
		case xui::style_var::border_thickness: return &s.border_thickness;
		case xui::style_var::checkbox_size: return &s.checkbox_size;
		case xui::style_var::slider_h: return &s.slider_h;
		case xui::style_var::keybind_w: return &s.keybind_w;
		case xui::style_var::keybind_h: return &s.keybind_h;
		case xui::style_var::combo_h: return &s.combo_h;
		case xui::style_var::combo_item_h: return &s.combo_item_h;
		case xui::style_var::swatch_w: return &s.swatch_w;
		case xui::style_var::swatch_h: return &s.swatch_h;
		case xui::style_var::text_input_h: return &s.text_input_h;
		default: return nullptr;
		}
	}

	static xdraw::color* resolve_style_color( xui::style& s, xui::style_col idx )
	{
		switch ( idx )
		{
		case xui::style_col::window_bg: return &s.window_bg;
		case xui::style_col::window_border: return &s.window_border;
		case xui::style_col::child_bg: return &s.child_bg;
		case xui::style_col::child_border: return &s.child_border;
		case xui::style_col::checkbox_bg: return &s.checkbox_bg;
		case xui::style_col::checkbox_border: return &s.checkbox_border;
		case xui::style_col::checkbox_mark: return &s.checkbox_mark;
		case xui::style_col::checkbox_mark_icon: return &s.checkbox_mark_icon;
		case xui::style_col::slider_track: return &s.slider_track;
		case xui::style_col::slider_fill: return &s.slider_fill;
		case xui::style_col::button_bg: return &s.button_bg;
		case xui::style_col::button_border: return &s.button_border;
		case xui::style_col::button_hovered: return &s.button_hovered;
		case xui::style_col::button_active: return &s.button_active;
		case xui::style_col::keybind_bg: return &s.keybind_bg;
		case xui::style_col::keybind_border: return &s.keybind_border;
		case xui::style_col::keybind_waiting: return &s.keybind_waiting;
		case xui::style_col::combo_bg: return &s.combo_bg;
		case xui::style_col::combo_border: return &s.combo_border;
		case xui::style_col::combo_arrow: return &s.combo_arrow;
		case xui::style_col::combo_hovered: return &s.combo_hovered;
		case xui::style_col::combo_popup_bg: return &s.combo_popup_bg;
		case xui::style_col::combo_popup_border: return &s.combo_popup_border;
		case xui::style_col::combo_popup_item_hovered: return &s.combo_popup_item_hovered;
		case xui::style_col::combo_popup_item_selected: return &s.combo_popup_item_selected;
		case xui::style_col::popup_bg: return &s.popup_bg;
		case xui::style_col::popup_border: return &s.popup_border;
		case xui::style_col::picker_bg: return &s.picker_bg;
		case xui::style_col::picker_border: return &s.picker_border;
		case xui::style_col::picker_popup_bg: return &s.picker_popup_bg;
		case xui::style_col::picker_popup_border: return &s.picker_popup_border;
		case xui::style_col::text_input_bg: return &s.text_input_bg;
		case xui::style_col::text_input_border: return &s.text_input_border;
		case xui::style_col::separator: return &s.separator;
		case xui::style_col::text: return &s.text;
		case xui::style_col::text_dim: return &s.text_dim;
		case xui::style_col::accent: return &s.accent;
		default: return nullptr;
		}
	}

} // namespace

namespace xui {

	constexpr rect rect::offset( float dx, float dy ) const noexcept
	{
		return { x + dx, y + dy, w, h };
	}

	constexpr rect rect::expand( float amount ) const noexcept
	{
		return { x - amount, y - amount, w + amount * 2.0f, h + amount * 2.0f };
	}

	constexpr rect rect::shrink( float amount ) const noexcept
	{
		return expand( -amount );
	}

	constexpr rect rect::intersect( const rect& o ) const noexcept
	{
		const auto x0 = ( x > o.x ) ? x : o.x;
		const auto y0 = ( y > o.y ) ? y : o.y;
		const auto x1 = ( right( ) < o.right( ) ) ? right( ) : o.right( );
		const auto y1 = ( bottom( ) < o.bottom( ) ) ? bottom( ) : o.bottom( );
		return { x0, y0, ( x1 > x0 ) ? ( x1 - x0 ) : 0.0f, ( y1 > y0 ) ? ( y1 - y0 ) : 0.0f };
	}


	std::pair<std::string_view, std::string_view> parse_label( std::string_view label ) noexcept
	{
		const auto pos = label.find( "##" );
		if ( pos == std::string_view::npos )
		{
			return { label, label };
		}

		return { label.substr( 0, pos ), label };
	}

	void set_highlight_target( std::string_view label, float duration_seconds )
	{
		auto& hl = get_highlight_state( );
		hl.label = std::string( label );
		hl.expires_at = std::chrono::steady_clock::now( ) + std::chrono::duration_cast< std::chrono::steady_clock::duration >( std::chrono::duration<float>( std::max( duration_seconds, 0.1f ) ) );
	}

	xdraw::color lighten( xdraw::color c, float factor ) noexcept
	{
		return xdraw::color
		{
			static_cast< std::uint8_t >( std::min( c.r * factor, 255.0f ) ),
			static_cast< std::uint8_t >( std::min( c.g * factor, 255.0f ) ),
			static_cast< std::uint8_t >( std::min( c.b * factor, 255.0f ) ),
			c.a
		};
	}

	xdraw::color darken( xdraw::color c, float factor ) noexcept
	{
		return xdraw::color
		{
			static_cast< std::uint8_t >( c.r * factor ),
			static_cast< std::uint8_t >( c.g * factor ),
			static_cast< std::uint8_t >( c.b * factor ),
			c.a
		};
	}

	xdraw::color alpha_mod( xdraw::color c, float a ) noexcept
	{
		return xdraw::color{ c.r, c.g, c.b, static_cast< std::uint8_t >( a * 255.0f ) };
	}

	xdraw::color lerp( xdraw::color a, xdraw::color b, float t ) noexcept
	{
		return xdraw::color
		{
			static_cast< std::uint8_t >( a.r + ( b.r - a.r ) * t ),
			static_cast< std::uint8_t >( a.g + ( b.g - a.g ) * t ),
			static_cast< std::uint8_t >( a.b + ( b.b - a.b ) * t ),
			static_cast< std::uint8_t >( a.a + ( b.a - a.a ) * t )
		};
	}

	hsv rgb_to_hsv( xdraw::color c ) noexcept
	{
		const auto r = c.r / 255.0f;
		const auto g = c.g / 255.0f;
		const auto b = c.b / 255.0f;

		const auto max_c = std::max( { r, g, b } );
		const auto min_c = std::min( { r, g, b } );
		const auto delta = max_c - min_c;

		hsv result{};
		result.v = max_c;
		result.s = ( max_c != 0.0f ) ? ( delta / max_c ) : 0.0f;
		result.a = c.a / 255.0f;

		if ( delta != 0.0f )
		{
			if ( max_c == r )
			{
				result.h = 60.0f * std::fmod( ( g - b ) / delta, 6.0f );
			}
			else if ( max_c == g )
			{
				result.h = 60.0f * ( 2.0f + ( b - r ) / delta );
			}
			else
			{
				result.h = 60.0f * ( 4.0f + ( r - g ) / delta );
			}
		}

		if ( result.h < 0.0f )
		{
			result.h += 360.0f;
		}

		return result;
	}

	xdraw::color hsv_to_rgb( const hsv& c ) noexcept
	{
		const auto ch = c.v * c.s;
		const auto hp = c.h / 60.0f;
		const auto x = ch * ( 1.0f - std::abs( std::fmod( hp, 2.0f ) - 1.0f ) );
		const auto m = c.v - ch;

		float r1, g1, b1;

		if ( hp < 1.0f ) { r1 = ch; g1 = x; b1 = 0.0f; }
		else if ( hp < 2.0f ) { r1 = x; g1 = ch; b1 = 0.0f; }
		else if ( hp < 3.0f ) { r1 = 0.0f; g1 = ch; b1 = x; }
		else if ( hp < 4.0f ) { r1 = 0.0f; g1 = x; b1 = ch; }
		else if ( hp < 5.0f ) { r1 = x; g1 = 0.0f; b1 = ch; }
		else { r1 = ch; g1 = 0.0f; b1 = x; }

		return xdraw::color
		{
			static_cast< std::uint8_t >( ( r1 + m ) * 255.0f ),
			static_cast< std::uint8_t >( ( g1 + m ) * 255.0f ),
			static_cast< std::uint8_t >( ( b1 + m ) * 255.0f ),
			static_cast< std::uint8_t >( c.a * 255.0f )
		};
	}

	xdraw::color hsv_to_rgb( float h, float s, float v, float a ) noexcept
	{
		return hsv_to_rgb( hsv{ h, s, v, a } );
	}

	std::string color_to_hex( xdraw::color c, bool include_alpha ) noexcept
	{
		char buf[ 16 ]{};

		if ( include_alpha && c.a != 255 )
		{
			std::snprintf( buf, sizeof( buf ), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a );
		}
		else
		{
			std::snprintf( buf, sizeof( buf ), "#%02X%02X%02X", c.r, c.g, c.b );
		}

		return std::string{ buf };
	}

	std::optional<xdraw::color> hex_to_color( std::string_view hex ) noexcept
	{
		if ( hex.empty( ) )
		{
			return std::nullopt;
		}

		if ( hex.front( ) == '#' )
		{
			hex.remove_prefix( 1 );
		}

		while ( !hex.empty( ) && ( hex.back( ) == ' ' || hex.back( ) == '\r' || hex.back( ) == '\n' || hex.back( ) == '\t' ) )
		{
			hex.remove_suffix( 1 );
		}

		const auto parse_byte = [ ]( char hi, char lo ) -> std::optional<std::uint8_t>
			{
				const auto nibble = [ ]( char c ) -> int
					{
						if ( c >= '0' && c <= '9' ) return c - '0';
						if ( c >= 'a' && c <= 'f' ) return 10 + c - 'a';
						if ( c >= 'A' && c <= 'F' ) return 10 + c - 'A';
						return -1;
					};

				const auto h = nibble( hi );
				const auto l = nibble( lo );

				if ( h < 0 || l < 0 )
				{
					return std::nullopt;
				}

				return static_cast< std::uint8_t >( h * 16 + l );
			};

		if ( hex.size( ) == 6 )
		{
			const auto r = parse_byte( hex[ 0 ], hex[ 1 ] );
			const auto g = parse_byte( hex[ 2 ], hex[ 3 ] );
			const auto b = parse_byte( hex[ 4 ], hex[ 5 ] );

			if ( r && g && b )
			{
				return xdraw::color{ *r, *g, *b, 255 };
			}
		}
		else if ( hex.size( ) == 8 )
		{
			const auto r = parse_byte( hex[ 0 ], hex[ 1 ] );
			const auto g = parse_byte( hex[ 2 ], hex[ 3 ] );
			const auto b = parse_byte( hex[ 4 ], hex[ 5 ] );
			const auto a = parse_byte( hex[ 6 ], hex[ 7 ] );

			if ( r && g && b && a )
			{
				return xdraw::color{ *r, *g, *b, *a };
			}
		}
		else if ( hex.size( ) == 3 )
		{
			const auto nibble = [ ]( char c ) -> int
				{
					if ( c >= '0' && c <= '9' ) return c - '0';
					if ( c >= 'a' && c <= 'f' ) return 10 + c - 'a';
					if ( c >= 'A' && c <= 'F' ) return 10 + c - 'A';
					return -1;
				};

			const auto r = nibble( hex[ 0 ] );
			const auto g = nibble( hex[ 1 ] );
			const auto b = nibble( hex[ 2 ] );

			if ( r >= 0 && g >= 0 && b >= 0 )
			{
				return xdraw::color
				{
					static_cast< std::uint8_t >( r * 17 ),
					static_cast< std::uint8_t >( g * 17 ),
					static_cast< std::uint8_t >( b * 17 ),
					255
				};
			}
		}

		return std::nullopt;
	}

	namespace ease {

		float linear( float t ) noexcept { return t; }
		float in_quad( float t ) noexcept { return t * t; }
		float out_quad( float t ) noexcept { return t * ( 2.0f - t ); }
		float in_out_quad( float t ) noexcept { return t < 0.5f ? 2.0f * t * t : -1.0f + ( 4.0f - 2.0f * t ) * t; }
		float in_cubic( float t ) noexcept { return t * t * t; }
		float out_cubic( float t ) noexcept { const auto f = t - 1.0f; return f * f * f + 1.0f; }
		float in_out_cubic( float t ) noexcept { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 3.0f ) / 2.0f; }
		float smoothstep( float t ) noexcept { return t * t * ( 3.0f - 2.0f * t ); }

	} // namespace ease

	std::span<const wchar_t> input_state::chars( ) const noexcept
	{
		return std::span( this->char_queue.data( ), this->char_count );
	}

	std::span<const int> input_state::key_presses( ) const noexcept
	{
		return std::span( this->key_press_queue.data( ), this->key_press_count );
	}

	std::span<const int> input_state::key_releases( ) const noexcept
	{
		return std::span( this->key_release_queue.data( ), this->key_release_count );
	}

	bool input_state::key_down( int vk ) const noexcept
	{
		const auto it = this->keys.find( vk );
		return it != this->keys.end( ) && it->second;
	}

	bool input_state::shift_held( ) const noexcept
	{
		return this->key_down( VK_SHIFT ) || this->key_down( VK_LSHIFT ) || this->key_down( VK_RSHIFT );
	}

	bool input_state::ctrl_held( ) const noexcept
	{
		return this->key_down( VK_CONTROL ) || this->key_down( VK_LCONTROL ) || this->key_down( VK_RCONTROL );
	}

	void input_state::push_char( wchar_t c )
	{
		if ( this->char_count < this->char_queue.size( ) )
		{
			this->char_queue[ this->char_count++ ] = c;
		}
	}

	void input_state::push_key_press( int vk )
	{
		if ( this->key_press_count < this->key_press_queue.size( ) )
		{
			this->key_press_queue[ this->key_press_count++ ] = vk;
		}
	}

	void input_state::push_key_release( int vk )
	{
		if ( this->key_release_count < this->key_release_queue.size( ) )
		{
			this->key_release_queue[ this->key_release_count++ ] = vk;
		}
	}

	void input_state::clear_frame( )
	{
		this->prev_mouse_x = this->mouse_x;
		this->prev_mouse_y = this->mouse_y;

		this->mouse_clicked = false;
		this->mouse_released = false;
		this->mouse_double_clicked = false;

		this->rmb_clicked = false;
		this->rmb_released = false;

		this->scroll_delta = 0.0f;

		this->char_count = 0;
		this->key_press_count = 0;
		this->key_release_count = 0;
	}

	bool feed_wndproc( input_state& s, UINT msg, WPARAM wp, LPARAM lp )
	{
		switch ( msg )
		{
		case WM_MOUSEMOVE:
			s.mouse_x = static_cast< float >( static_cast< short >( LOWORD( lp ) ) );
			s.mouse_y = static_cast< float >( static_cast< short >( HIWORD( lp ) ) );
			return true;

		case WM_LBUTTONDOWN:
			s.mouse_down = true;
			s.mouse_clicked = true;
			return true;

		case WM_LBUTTONUP:
			s.mouse_down = false;
			s.mouse_released = true;
			return true;

		case WM_RBUTTONDOWN:
			s.rmb_down = true;
			s.rmb_clicked = true;
			return true;

		case WM_RBUTTONUP:
			s.rmb_down = false;
			s.rmb_released = true;
			return true;

		case WM_MOUSEWHEEL:
		{
			const auto delta = static_cast< float >( GET_WHEEL_DELTA_WPARAM( wp ) ) / static_cast< float >( WHEEL_DELTA );
			s.scroll_delta += delta;
			return true;
		}

		case WM_CHAR:
		{
			const auto c = static_cast< wchar_t >( wp );
			if ( c >= 32 && c != 127 )
			{
				s.push_char( c );
			}

			return true;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			auto vk = static_cast< int >( wp );
			const auto scancode = static_cast< UINT >( ( lp >> 16 ) & 0xFF );
			const bool extended = ( lp >> 24 ) & 1;

			switch ( vk )
			{
			case VK_SHIFT:
				vk = static_cast< int >( MapVirtualKeyW( scancode, MAPVK_VSC_TO_VK_EX ) );
				break;
			case VK_CONTROL:
				vk = extended ? VK_RCONTROL : VK_LCONTROL;
				break;
			case VK_MENU:
				vk = extended ? VK_RMENU : VK_LMENU;
				break;
			}

			if ( !s.key_down( vk ) )
			{
				s.push_key_press( vk );
				s.keys[ vk ] = true;
			}

			return true;
		}

		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			auto vk = static_cast< int >( wp );
			const auto scancode = static_cast< UINT >( ( lp >> 16 ) & 0xFF );
			const bool extended = ( lp >> 24 ) & 1;

			switch ( vk )
			{
			case VK_SHIFT:
				vk = static_cast< int >( MapVirtualKeyW( scancode, MAPVK_VSC_TO_VK_EX ) );
				break;
			case VK_CONTROL:
				vk = extended ? VK_RCONTROL : VK_LCONTROL;
				break;
			case VK_MENU:
				vk = extended ? VK_RMENU : VK_LMENU;
				break;
			}

			s.push_key_release( vk );
			s.keys[ vk ] = false;
			return true;
		}

		case WM_MBUTTONDOWN:
			s.push_key_press( VK_MBUTTON );
			s.keys[ VK_MBUTTON ] = true;
			return true;

		case WM_MBUTTONUP:
			s.push_key_release( VK_MBUTTON );
			s.keys[ VK_MBUTTON ] = false;
			return true;

		case WM_XBUTTONDOWN:
		{
			const auto button = GET_XBUTTON_WPARAM( wp );
			const auto vk = ( button == XBUTTON1 ) ? VK_XBUTTON1 : VK_XBUTTON2;
			s.push_key_press( vk );
			s.keys[ vk ] = true;
			return true;
		}

		case WM_XBUTTONUP:
		{
			const auto button = GET_XBUTTON_WPARAM( wp );
			const auto vk = ( button == XBUTTON1 ) ? VK_XBUTTON1 : VK_XBUTTON2;
			s.push_key_release( vk );
			s.keys[ vk ] = false;
			return true;
		}

		default:
			return false;
		}
	}

	void push_style_var( style_var var, float value )
	{
		auto& c = get_ctx( );
		auto ptr = resolve_style_var( c.style, var );

		if ( !ptr )
		{
			return;
		}

		c.style_var_stack.push_back( { var, *ptr } );
		*ptr = value;
	}

	void pop_style_var( int count )
	{
		auto& c = get_ctx( );

		for ( int i = 0; i < count && !c.style_var_stack.empty( ); ++i )
		{
			const auto& [var, prev] = c.style_var_stack.back( );
			auto ptr = resolve_style_var( c.style, var );

			if ( ptr )
			{
				*ptr = prev;
			}

			c.style_var_stack.pop_back( );
		}
	}

	void push_style_color( style_col idx, xdraw::color col )
	{
		auto& c = get_ctx( );
		auto ptr = resolve_style_color( c.style, idx );

		if ( !ptr )
		{
			return;
		}

		c.style_color_stack.push_back( { idx, *ptr } );
		*ptr = col;
	}

	void pop_style_color( int count )
	{
		auto& c = get_ctx( );

		for ( int i = 0; i < count && !c.style_color_stack.empty( ); ++i )
		{
			const auto& [idx, prev] = c.style_color_stack.back( );
			auto ptr = resolve_style_color( c.style, idx );

			if ( ptr )
			{
				*ptr = prev;
			}

			c.style_color_stack.pop_back( );
		}
	}

	namespace anim {

		float lerp( std::uintptr_t id, float target, float speed, float initial )
		{
			auto& states = get_anim_states( );
			auto it = states.find( id );

			if ( it == states.end( ) )
			{
				it = states.emplace( id, initial ).first;
			}

			const auto dt = xdraw::delta_time( );
			it->second = it->second + ( target - it->second ) * std::min( speed * dt, 1.0f );
			return it->second;
		}

		float smooth( std::uintptr_t id, float target, float speed, float initial )
		{
			const auto raw = lerp( id, target, speed, initial );
			return raw * raw * ( 3.0f - 2.0f * raw );
		}

		float get( std::uintptr_t id, float fallback )
		{
			const auto& states = get_anim_states( );
			const auto it = states.find( id );
			return ( it != states.end( ) ) ? it->second : fallback;
		}

		void set( std::uintptr_t id, float value )
		{
			get_anim_states( )[ id ] = value;
		}

		void remove( std::uintptr_t id )
		{
			get_anim_states( ).erase( id );
		}

		void clear_all( )
		{
			get_anim_states( ).clear( );
		}

	} // namespace anim

	namespace binds {

		namespace {

			struct bind_registry
			{
				std::vector<xui::setting*> settings{};
				std::unordered_map<int, bool> prev_key_state{};
				std::uintptr_t listening_setting{};
				std::uint64_t activation_counter{};
			};

			bind_registry& get_bind_registry( )
			{
				static bind_registry r{};
				return r;
			}

		} // the bind_registry namespace

		void register_setting( setting* s )
		{
			if ( !s )
			{
				return;
			}

			auto& reg = get_bind_registry( );

			for ( const auto existing : reg.settings )
			{
				if ( existing == s )
				{
					return;
				}
			}

			if ( s->bind.key != 0 && s->bind.mode == bind_mode::toggle )
			{
				s->bind.active = s->value;
			}

			reg.settings.push_back( s );
		}

		void unregister_setting( setting* s )
		{
			auto& reg = get_bind_registry( );
			auto& v = reg.settings;
			v.erase( std::remove( v.begin( ), v.end( ), s ), v.end( ) );
		}

		void process( const input_state& input )
		{
			auto& reg = get_bind_registry( );

			for ( auto* s : reg.settings )
			{
				if ( !s || s->bind.key == 0 )
				{
					continue;
				}

				const auto vk = s->bind.key;
				const auto held = input.key_down( vk );
				const auto was_held = reg.prev_key_state[ vk ];
				const auto just_pressed = held && !was_held;

				switch ( s->bind.mode )
				{
				case bind_mode::toggle:
				{
					if ( just_pressed )
					{
						if ( s->bind.excludes && s->bind.excludes->value )
						{
							s->bind.excludes->bind.active = false;
							s->bind.excludes->value = false;
						}

						s->bind.active = !s->bind.active;
						s->value = s->bind.active;
					}

					break;
				}
				case bind_mode::hold_on:
				{
					s->bind.active = held;
					s->value = held;

					if ( s->bind.active && s->bind.excludes )
					{
						s->bind.excludes->bind.active = false;
						s->bind.excludes->value = false;
					}

					break;
				}
				case bind_mode::hold_off:
				{
					s->bind.active = !held;
					s->value = !held;
					break;
				}
				}
			}

			for ( auto* s : reg.settings )
			{
				if ( !s || !s->bind.excludes )
				{
					continue;
				}

				if ( s->value && s->bind.excludes->value )
				{
					s->bind.excludes->value = false;
					s->bind.excludes->bind.active = false;
				}
			}

			for ( auto* s : reg.settings )
			{
				if ( s && s->bind.key != 0 )
				{
					reg.prev_key_state[ s->bind.key ] = input.key_down( s->bind.key );
				}
			}
		}

		std::span<setting* const> all( )
		{
			return get_bind_registry( ).settings;
		}

		std::size_t count( )
		{
			return get_bind_registry( ).settings.size( );
		}

		void clear_all( )
		{
			auto& reg = get_bind_registry( );
			reg.settings.clear( );
			reg.prev_key_state.clear( );
			reg.listening_setting = 0;
			reg.activation_counter = 0;
		}

		std::uintptr_t listening_id( )
		{
			return get_bind_registry( ).listening_setting;
		}

		const char* mode_name( bind_mode m ) noexcept
		{
			switch ( m )
			{
			case bind_mode::toggle:   return "toggle";
			case bind_mode::hold_on:  return "hold on";
			case bind_mode::hold_off: return "hold off";
			default:                  return "unknown";
			}
		}

	} // namespace binds

	popup_overlay::popup_overlay( std::uintptr_t id, const rect& anchor, float width ) : overlay{ id, anchor }, m_width{ width } { }

	bool popup_overlay::hit_test( float x, float y ) const
	{
		return this->get_popup( ).contains( x, y ) || this->m_anchor.contains( x, y );
	}

	bool popup_overlay::process_input( const input_state& input )
	{
		if ( this->m_closing )
		{
			return false;
		}

		const auto popup = this->get_popup( );
		if ( input.mouse_clicked && !popup.contains( input.mouse_x, input.mouse_y ) && !this->m_anchor.contains( input.mouse_x, input.mouse_y ) )
		{
			this->m_closing = true;
			return true;
		}

		return popup.contains( input.mouse_x, input.mouse_y );
	}

	void popup_overlay::render( const style&, const input_state& ) {}

	void popup_overlay::tick( )
	{
		const auto dt = xdraw::delta_time( );
		const auto speed = this->m_closing ? 16.0f : 14.0f;
		const auto target = this->m_closing ? 0.0f : 1.0f;

		this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );
		if ( this->m_open_anim < 0.01f && this->m_closing )
		{
			this->m_closed = true;
		}
	}

	void popup_overlay::set_content_h( float h ) { this->m_content_h = h; }
	float popup_overlay::open_anim( ) const { return this->m_open_anim; }
	bool popup_overlay::closing( ) const { return this->m_closing; }

	rect popup_overlay::get_popup( ) const
	{
		const auto clamped = std::min( this->m_content_h, 400.0f );
		return { this->m_anchor.x, this->m_anchor.bottom( ) + 4.0f, this->m_width, clamped };
	}

	namespace overlays {

		bool is_open( std::uintptr_t id )
		{
			auto& store = get_overlays( );

			for ( const auto& o : store.list )
			{
				if ( o && o->id( ) == id && !o->is_closing( ) )
				{
					return true;
				}
			}

			return false;
		}

		overlay* find( std::uintptr_t id )
		{
			auto& store = get_overlays( );

			for ( auto& o : store.list )
			{
				if ( o && o->id( ) == id )
				{
					return o.get( );
				}
			}

			return nullptr;
		}

		void close( std::uintptr_t id )
		{
			auto& store = get_overlays( );

			for ( auto& o : store.list )
			{
				if ( o && o->id( ) == id )
				{
					o->request_close( );
					break;
				}
			}
		}

		void close_all( )
		{
			auto& store = get_overlays( );

			for ( auto& o : store.list )
			{
				if ( o )
				{
					o->request_close( );
				}
			}
		}

		void touch( std::uintptr_t owner_id )
		{
			get_overlays( ).touched_this_frame.insert( owner_id );
		}

		bool wants_input( float x, float y )
		{
			auto& store = get_overlays( );

			for ( const auto& o : store.list )
			{
				if ( o && o->hit_test( x, y ) )
				{
					return true;
				}
			}

			return false;
		}

		bool has_any( )
		{
			return !get_overlays( ).list.empty( );
		}

		bool has_any_except( std::uintptr_t exclude )
		{
			auto& store = get_overlays( );

			for ( const auto& o : store.list )
			{
				if ( o && o->id( ) != exclude && !o->is_closed( ) )
				{
					return true;
				}
			}

			return false;
		}

		void process( const input_state& input )
		{
			auto& store = get_overlays( );

			for ( auto it = store.list.rbegin( ); it != store.list.rend( ); ++it )
			{
				if ( *it && ( *it )->process_input( input ) )
				{
					return;
				}
			}
		}

		void render( const style& style, const input_state& input )
		{
			auto& store = get_overlays( );

			for ( auto& o : store.list )
			{
				if ( o )
				{
					o->render( style, input );
				}
			}
		}

		void sweep( )
		{
			auto& store = get_overlays( );

			for ( auto& o : store.list )
			{
				if ( !o || o->is_closing( ) || o->is_closed( ) )
				{
					continue;
				}

				const auto id = o->id( );

				if ( store.touched_last_frame.contains( id ) && !store.touched_this_frame.contains( id ) )
				{
					o->request_close( );
				}
			}

			store.list.erase( std::remove_if( store.list.begin( ), store.list.end( ), [ ]( const std::unique_ptr<overlay>& o ) { return !o || o->is_closed( ); } ), store.list.end( ) );
			store.touched_last_frame = std::move( store.touched_this_frame );
			store.touched_this_frame.clear( );
		}

		void add( std::unique_ptr<overlay> ov )
		{
			get_overlays( ).list.push_back( std::move( ov ) );
		}

	} // namespace overlays

	void push_id( std::uintptr_t id )
	{
		get_ctx( ).id_stack.push_back( id );
	}

	void push_id( std::string_view sv )
	{
		push_id( fnv1a( sv ) );
	}

	void push_id( const void* ptr )
	{
		push_id( reinterpret_cast< std::uintptr_t >( ptr ) );
	}

	void pop_id( )
	{
		auto& stack = get_ctx( ).id_stack;
		if ( !stack.empty( ) )
		{
			stack.pop_back( );
		}
	}

	std::uintptr_t make_id( std::string_view label )
	{
		auto h = fnv1a( label );

		for ( const auto parent : get_ctx( ).id_stack )
		{
			h ^= parent;
			h *= 1099511628211ull;
		}

		return h;
	}

	namespace draw {

		xdraw::draw_list& current( )
		{
			auto& c = get_ctx( );
			if ( !c.layer_stack.empty( ) )
			{
				return xdraw::get( c.layer_stack.back( ) );
			}

			return xdraw::get( );
		}

		void push_layer( xdraw::layer l )
		{
			get_ctx( ).layer_stack.push_back( l );
		}

		void pop_layer( )
		{
			auto& stack = get_ctx( ).layer_stack;
			if ( !stack.empty( ) )
			{
				stack.pop_back( );
			}
		}

	} // namespace draw

	namespace layout {

		window_state* current_window( )
		{
			auto& wins = get_ctx( ).windows;
			return wins.empty( ) ? nullptr : &wins.back( );
		}

		const window_state* current_window_const( )
		{
			const auto& wins = get_ctx( ).windows;
			return wins.empty( ) ? nullptr : &wins.back( );
		}

		std::pair<float, float> avail( )
		{
			const auto win = current_window_const( );
			if ( !win )
			{
				return { 0.0f, 0.0f };
			}

			const auto& s = get_ctx( ).style;
			const auto max_x = win->bounds.w - s.window_pad_x;
			const auto max_y = win->bounds.h - s.window_pad_y;

			auto next_x = win->cursor_x;
			auto next_y = win->cursor_y;

			if ( win->line_h > 0.0f )
			{
				next_y += win->line_h + s.item_spacing_y;
			}

			return
			{
				std::max( 0.0f, max_x - next_x ),
				std::max( 0.0f, max_y - next_y )
			};
		}

		float item_width( int count )
		{
			const auto [aw, ah] = avail( );

			if ( count <= 0 )
			{
				return aw;
			}

			const auto spacing = get_ctx( ).style.item_spacing_x;
			return ( aw - spacing * ( count - 1 ) ) / static_cast< float >( count );
		}

		rect item( float w, float h, float label_h )
		{
			auto win = current_window( );
			if ( !win )
			{
				return { 0, 0, 0, 0 };
			}

			if ( win->line_h > 0.0f )
			{
				auto spacing = get_ctx( ).style.item_spacing_y;

				if ( label_h > 0.0f )
				{
					spacing = std::max( spacing * 0.5f, spacing - label_h * 0.25f );
				}

				win->cursor_y += win->line_h + spacing;
			}

			const auto local = rect{ win->cursor_x, win->cursor_y, w, h };
			win->last_item = local;
			win->line_h = h;
			win->content_h = std::max( win->content_h, win->cursor_y + h );

			return rect
			{
				std::floorf( local.x + win->bounds.x ),
				std::floorf( local.y + win->bounds.y ),
				std::floorf( local.w ),
				std::floorf( local.h )
			};
		}

		void same_line( float offset )
		{
			auto win = current_window( );
			if ( !win || win->line_h <= 0.0f )
			{
				return;
			}

			const auto spacing = ( offset == 0.0f ) ? get_ctx( ).style.item_spacing_x : offset;
			win->cursor_x = win->last_item.x + win->last_item.w + spacing;
			win->cursor_y = win->last_item.y;
			win->line_h = 0.0f;
		}

		void new_line( )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			if ( win->line_h > 0.0f )
			{
				win->cursor_y += win->line_h + get_ctx( ).style.item_spacing_y;
			}

			win->cursor_x = get_ctx( ).style.window_pad_x;
			win->line_h = 0.0f;
		}

		void spacing( float amount )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			if ( amount <= 0.0f )
			{
				amount = get_ctx( ).style.item_spacing_y;
			}

			win->cursor_y += amount;
		}

		void indent( float amount )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			if ( amount <= 0.0f )
			{
				amount = get_ctx( ).style.window_pad_x;
			}

			win->cursor_x += amount;
		}

		void unindent( float amount )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			if ( amount <= 0.0f )
			{
				amount = get_ctx( ).style.window_pad_x;
			}

			win->cursor_x = std::max( get_ctx( ).style.window_pad_x, win->cursor_x - amount );
		}

		void separator( )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			const auto& s = get_ctx( ).style;
			const auto sep_pad = s.item_spacing_y * 0.5f;

			if ( win->line_h > 0.0f )
			{
				win->cursor_y += win->line_h + s.item_spacing_y;
				win->line_h = 0.0f;
			}

			const auto max_x = win->bounds.w - s.window_pad_x;
			const auto w = std::max( 0.0f, max_x - win->cursor_x );
			const auto abs = item( w, 1.0f + sep_pad * 2.0f );

			draw::current( ).line( abs.x, abs.y + sep_pad, abs.x + w, abs.y + sep_pad, s.separator, 1.0f );
		}

		void set_cursor( float x, float y )
		{
			auto win = current_window( );
			if ( !win )
			{
				return;
			}

			win->cursor_x = x;
			win->cursor_y = y;
			win->line_h = 0.0f;
		}

		std::pair<float, float> get_cursor( )
		{
			const auto win = current_window_const( );
			if ( !win )
			{
				return { 0.0f, 0.0f };
			}

			return { win->cursor_x, win->cursor_y };
		}

	} // namespace layout

	bool context::overlay_blocking( ) const
	{
		if ( this->inside_overlay != null_id )
		{
			return overlays::has_any_except( this->inside_overlay );
		}

		return overlays::has_any( );
	}

	context& ctx( )
	{
		return get_ctx( );
	}

	std::string_view truncate( std::string_view text, float max_width )
	{
		const auto [tw, th] = xdraw::measure_text( text );
		if ( tw <= max_width )
		{
			return text;
		}

		const auto [ew, eh] = xdraw::measure_text( "..." );
		if ( max_width < ew )
		{
			return "...";
		}

		const auto avail = max_width - ew;

		std::size_t lo{ 0 };
		std::size_t hi = text.size( );
		std::size_t best{ 0 };

		while ( lo <= hi && hi != static_cast< std::size_t >( -1 ) )
		{
			const auto mid = lo + ( hi - lo ) / 2;
			const auto [cw, ch] = xdraw::measure_text( text.substr( 0, mid ) );

			if ( cw <= avail )
			{
				best = mid;
				lo = mid + 1;
			}
			else
			{
				hi = mid - 1;
			}
		}

		auto& scratch = get_ctx( ).truncation_scratch;
		scratch.clear( );
		scratch.reserve( best + 3 );
		scratch.assign( text.data( ), best );
		scratch.append( "..." );
		return scratch;
	}

	const char* vk_name( int key )
	{
		if ( key == 0 )
		{
			return "none";
		}

		if ( key >= 0x41 && key <= 0x5A )
		{
			static char buf[ 2 ]{};
			buf[ 0 ] = static_cast< char >( key );
			buf[ 1 ] = '\0';
			return buf;
		}

		if ( key >= 0x30 && key <= 0x39 )
		{
			static char buf[ 2 ]{};
			buf[ 0 ] = static_cast< char >( key );
			buf[ 1 ] = '\0';
			return buf;
		}

		switch ( key )
		{
		case VK_LBUTTON: return "lmb";
		case VK_RBUTTON: return "rmb";
		case VK_MBUTTON: return "mmb";
		case VK_XBUTTON1: return "mb4";
		case VK_XBUTTON2: return "mb5";
		case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "shift";
		case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "ctrl";
		case VK_MENU: case VK_LMENU: case VK_RMENU: return "alt";
		case VK_SPACE: return "space";
		case VK_RETURN: return "enter";
		case VK_ESCAPE: return "esc";
		case VK_TAB: return "tab";
		case VK_CAPITAL: return "caps";
		case VK_INSERT: return "insert";
		case VK_DELETE: return "delete";
		case VK_HOME: return "home";
		case VK_END: return "end";
		case VK_PRIOR: return "pgup";
		case VK_NEXT: return "pgdn";
		case VK_LEFT: return "left";
		case VK_RIGHT: return "right";
		case VK_UP: return "up";
		case VK_DOWN: return "down";
		case VK_F1: return "f1";
		case VK_F2: return "f2";
		case VK_F3: return "f3";
		case VK_F4: return "f4";
		case VK_F5: return "f5";
		case VK_F6: return "f6";
		case VK_F7: return "f7";
		case VK_F8: return "f8";
		case VK_F9: return "f9";
		case VK_F10: return "f10";
		case VK_F11: return "f11";
		case VK_F12: return "f12";
		default: return "unknown";
		}
	}

	bool initialize( HWND hwnd )
	{
		get_hwnd( ) = hwnd;
		return hwnd != nullptr;
	}

	void begin( )
	{
		auto& c = get_ctx( );

		{
			auto& dc = get_double_click( );
			c.input.mouse_double_clicked = false;

			if ( c.input.mouse_clicked )
			{
				const auto now = std::chrono::steady_clock::now( );
				const auto elapsed = std::chrono::duration<float>( now - dc.last_time ).count( );

				if ( elapsed < 0.4f )
				{
					const auto dx = c.input.mouse_x - dc.last_x;
					const auto dy = c.input.mouse_y - dc.last_y;

					if ( dx * dx + dy * dy < 25.0f )
					{
						c.input.mouse_double_clicked = true;
					}
				}

				dc.last_time = now;
				dc.last_x = c.input.mouse_x;
				dc.last_y = c.input.mouse_y;
			}
		}

		binds::process( c.input );

		c.windows.clear( );
		c.id_stack.clear( );
	}

	void end( )
	{
		auto& c = get_ctx( );

		if ( overlays::has_any( ) )
		{
			overlays::process( c.input );
		}

		overlays::render( c.style, c.input );
		overlays::sweep( );

		if ( c.input.mouse_released )
		{
			c.active_window = null_id;
			c.active_resize = null_id;
			c.active_slider = null_id;
			c.active_child_scroll = null_id;
		}

		c.input.clear_frame( );
	}

	bool wndproc( UINT msg, WPARAM wp, LPARAM lp )
	{
		return feed_wndproc( get_ctx( ).input, msg, wp, lp );
	}

	bool begin_window( std::string_view title, float& x, float& y, float& w, float& h, bool resizable, float min_w, float min_h, float reveal )
	{
		auto& c = get_ctx( );
		const auto id = make_id( title );
		const auto& s = c.style;
		const auto& input = c.input;
		const auto r = s.rounding;
		auto abs = rect{ std::floorf( x ), std::floorf( y ), std::floorf( w ), std::floorf( h ) };

		const auto reveal_clamped = std::clamp( reveal, 0.0f, 1.0f );
		const auto allow_window_drag = reveal_clamped >= 0.98f;

		constexpr auto grip_size{ 16.0f };
		const auto grip = rect{ abs.right( ) - grip_size, abs.bottom( ) - grip_size, grip_size, grip_size };
		const auto grip_hovered = resizable && allow_window_drag && input.in_rect( grip );
		const auto window_hovered = allow_window_drag && input.in_rect( abs );

		if ( grip_hovered && input.mouse_clicked && c.active_resize == null_id )
		{
			c.active_resize = id;
		}

		if ( c.active_resize == id && input.mouse_down )
		{
			w = std::max( min_w, w + input.mouse_delta_x( ) );
			h = std::max( min_h, h + input.mouse_delta_y( ) );
			abs.w = w;
			abs.h = h;
		}
		else if ( window_hovered && !grip_hovered && input.mouse_clicked&& c.active_window == null_id&& c.active_slider == null_id&& c.active_resize == null_id&& c.active_text_input == null_id&& c.active_child_scroll == null_id&& !c.overlay_blocking( ) )
		{
			c.active_window = id;
		}

		if ( allow_window_drag && c.active_window == id && input.mouse_down&& c.active_slider == null_id&& c.active_resize == null_id&& c.active_text_input == null_id&& c.active_child_scroll == null_id )
		{
			x += input.mouse_delta_x( );
			y += input.mouse_delta_y( );
			abs.x = x;
			abs.y = y;
		}

		window_state state{};
		state.title = std::string( title );
		state.bounds = abs;
		state.cursor_x = s.window_pad_x;
		state.cursor_y = s.window_pad_y;
		state.is_child = false;

		c.windows.push_back( std::move( state ) );

		auto& dl = draw::current( );

		const auto draw_w = abs.w * reveal_clamped;
		const auto draw_h = abs.h * reveal_clamped;
		const auto draw_x = abs.x + ( abs.w - draw_w ) * 0.5f;
		const auto draw_y = abs.y + ( abs.h - draw_h ) * 0.5f;

		auto window_bg = s.window_bg;
		window_bg.a = static_cast< std::uint8_t >( window_bg.a * reveal_clamped );
		auto window_border = s.window_border;
		window_border.a = static_cast< std::uint8_t >( window_border.a * reveal_clamped );

		if ( reveal_clamped < 1.0f )
		{
			dl.rect_filled_blurred( draw_x, draw_y, draw_w, draw_h, xdraw::corner_radius{ r }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 210.0f * reveal_clamped ) } );
		}
		else
		{
			dl.rect_filled_blurred( draw_x, draw_y, draw_w, draw_h, xdraw::corner_radius{ r } );
		}

		dl.rect_filled( draw_x, draw_y, draw_w, draw_h, window_bg, xdraw::corner_radius{ r } );

		if ( s.border_thickness > 0.0f )
		{
			dl.rect( draw_x, draw_y, draw_w, draw_h, window_border, xdraw::corner_radius{ r }, s.border_thickness );
		}

		dl.push_clip( draw_x, draw_y, draw_w, draw_h );
		push_id( id );

		return true;
	}

	void end_window( )
	{
		draw::current( ).pop_clip( );
		get_ctx( ).windows.pop_back( );
		pop_id( );
	}

	bool begin_child( std::string_view title, float w, float h, bool scrollable )
	{
		auto& c = get_ctx( );
		const auto parent = layout::current_window_const( );

		if ( !parent )
		{
			return false;
		}

		const auto id = make_id( title );
		const auto& s = c.style;
		const auto r = s.rounding;

		if ( w <= 0.0f )
		{
			w = layout::item_width( );
		}

		auto actual_h = h;
		if ( actual_h <= 0.0f )
		{
			if ( scrollable )
			{
				actual_h = 150.0f;
			}
			else
			{
				const auto it = c.child_height_cache.find( id );
				actual_h = ( it != c.child_height_cache.end( ) ) ? it->second : 100.0f;
			}
		}

		auto pwin = layout::current_window( );
		const auto consumed_y = pwin->cursor_y + ( pwin->line_h > 0.0f ? pwin->line_h + s.item_spacing_y : 0.0f );
		const auto max_h = parent->bounds.h - consumed_y - s.window_pad_y;

		if ( max_h > 0.0f )
		{
			actual_h = std::min( actual_h, max_h );
		}

		const auto abs = layout::item( w, actual_h );
		auto scroll_y{ 0.0f };
		auto sb_inset{ 0.0f };

		if ( scrollable )
		{
			auto& cs = c.child_scroll_cache[ id ];
			scroll_y = cs.scroll;

			constexpr auto k_scrollbar_w{ 6.0f };
			constexpr auto k_scrollbar_pad{ 8.0f };
			constexpr auto k_scrollbar_content_gap{ 0.0f };

			const auto shrink_phase = std::clamp( cs.scrollbar_anim / 0.5f, 0.0f, 1.0f );
			const auto shrink_eased = ease::out_cubic( shrink_phase );
			sb_inset = ( k_scrollbar_w + k_scrollbar_pad + k_scrollbar_content_gap ) * shrink_eased;
		}

		window_state state{};
		state.title = std::string( title );
		state.bounds = rect{ abs.x, abs.y, abs.w - sb_inset, abs.h };
		state.cursor_x = s.window_pad_x;
		state.cursor_y = s.window_pad_y - scroll_y;
		state.is_child = true;
		state.group_id = id;
		state.scrollable = scrollable;
		state.scroll_y = scroll_y;

		c.windows.push_back( std::move( state ) );

		auto& dl = draw::current( );
		dl.rect_filled( abs.x, abs.y, abs.w, abs.h, s.child_bg, xdraw::corner_radius{ r } );

		if ( s.border_thickness > 0.0f )
		{
			dl.rect( abs.x, abs.y, abs.w, abs.h, s.child_border, xdraw::corner_radius{ r }, s.border_thickness );
		}

		dl.push_clip( abs.x, abs.y, abs.w, abs.h );
		push_id( id );

		return true;
	}

	void end_child( )
	{
		auto& c = get_ctx( );
		auto win = layout::current_window( );

		if ( win && win->group_id != null_id )
		{
			const auto& s = c.style;
			const auto& input = c.input;
			const auto true_content_h = win->content_h + win->scroll_y + s.window_pad_y;
			const auto visible_h = win->bounds.h;

			if ( win->scrollable )
			{
				const auto max_scroll = std::max( 0.0f, true_content_h - visible_h );
				auto& cs = c.child_scroll_cache[ win->group_id ];

				constexpr auto k_scrollbar_w{ 6.0f };
				constexpr auto k_scrollbar_pad{ 8.0f };
				constexpr auto k_scrollbar_content_gap{ 0.0f };

				const auto shrink_phase = std::clamp( cs.scrollbar_anim / 0.5f, 0.0f, 1.0f );
				const auto shrink_eased = ease::out_cubic( shrink_phase );
				const auto inset = ( k_scrollbar_w + k_scrollbar_pad + k_scrollbar_content_gap ) * shrink_eased;
				const auto full_w = win->bounds.w + inset;
				const auto full_right = win->bounds.right( ) + inset;
				const auto full_bounds = rect{ win->bounds.x, win->bounds.y, full_w, win->bounds.h };

				const auto popup_hovered = !c.overlay_blocking( ) && input.in_rect( full_bounds );
				const auto sb_target = ( popup_hovered || cs.scrollbar_dragging ) && max_scroll > 0.0f ? 1.0f : 0.0f;
				const auto dt = xdraw::delta_time( );
				const auto sb_speed = sb_target > cs.scrollbar_anim ? 7.0f : 12.0f;
				cs.scrollbar_anim += ( sb_target - cs.scrollbar_anim ) * std::min( sb_speed * dt, 1.0f );

				const auto sb_alpha_phase = std::clamp( ( cs.scrollbar_anim - 0.3f ) / 0.7f, 0.0f, 1.0f );
				const auto sb_alpha = ease::out_cubic( sb_alpha_phase );

				const auto track = rect
				{
					std::floorf( full_right - k_scrollbar_w - k_scrollbar_pad ),
					std::floorf( win->bounds.y + k_scrollbar_pad ),
					k_scrollbar_w,
					std::floorf( win->bounds.h - k_scrollbar_pad * 2.0f )
				};

				const auto thumb_h_unclamped = ( max_scroll > 0.0f ) ? ( track.h * ( visible_h / true_content_h ) ) : track.h;
				const auto thumb_h = std::floorf( std::max( 20.0f, thumb_h_unclamped ) );
				const auto rel = max_scroll > 0.0f ? ( cs.scroll / max_scroll ) : 0.0f;
				const auto thumb_y = std::floorf( track.y + rel * ( track.h - thumb_h ) );
				const auto thumb = rect{ track.x, thumb_y, track.w, thumb_h };

				if ( max_scroll > 0.0f && !c.overlay_blocking( ) )
				{
					if ( input.mouse_clicked && thumb.contains( input.mouse_x, input.mouse_y ) )
					{
						cs.scrollbar_dragging = true;
						cs.scrollbar_drag_offset = input.mouse_y - thumb.y;
						c.active_child_scroll = win->group_id;
						c.active_window = null_id;
					}
					else if ( input.mouse_clicked && track.contains( input.mouse_x, input.mouse_y ) )
					{
						const auto target_y = input.mouse_y - thumb.h * 0.5f;
						const auto rel_click = std::clamp( ( target_y - track.y ) / ( track.h - thumb.h ), 0.0f, 1.0f );
						cs.scroll_target = rel_click * max_scroll;
						cs.scrollbar_dragging = true;
						cs.scrollbar_drag_offset = thumb.h * 0.5f;
						c.active_child_scroll = win->group_id;
						c.active_window = null_id;
					}

					if ( cs.scrollbar_dragging )
					{
						if ( !input.mouse_down )
						{
							cs.scrollbar_dragging = false;
							if ( c.active_child_scroll == win->group_id )
							{
								c.active_child_scroll = null_id;
							}
						}
						else
						{
							const auto rel_drag = std::clamp( ( input.mouse_y - cs.scrollbar_drag_offset - track.y ) / ( track.h - thumb.h ), 0.0f, 1.0f );
							cs.scroll_target = rel_drag * max_scroll;
							cs.scroll = cs.scroll_target;
						}
					}

					if ( popup_hovered && input.scroll_delta != 0.0f && !cs.scrollbar_dragging )
					{
						cs.scroll_target -= input.scroll_delta * 40.0f;
					}
				}

				cs.scroll_target = std::clamp( cs.scroll_target, 0.0f, max_scroll );
				cs.scroll += ( cs.scroll_target - cs.scroll ) * std::min( 18.0f * dt, 1.0f );
				cs.scroll = std::clamp( cs.scroll, 0.0f, max_scroll );

				draw::current( ).pop_clip( );

				if ( max_scroll > 0.0f )
				{
					constexpr auto fade_h{ 16.0f };
					const auto cr = std::min( s.rounding, fade_h );
					auto& dl = draw::current( );

					auto bg_solid = s.child_bg;
					auto bg_clear = s.child_bg;
					bg_clear.a = 0;

					const auto top_t = std::clamp( cs.scroll / fade_h, 0.0f, 1.0f );
					if ( top_t > 0.01f )
					{
						auto top = bg_solid;
						top.a = static_cast< std::uint8_t >( bg_solid.a * top_t );
						dl.rect_filled_gradient( win->bounds.x, win->bounds.y, full_w, fade_h, top, top, bg_clear, bg_clear, xdraw::corner_radius{ cr, cr, 0.0f, 0.0f } );
					}

					const auto remaining = max_scroll - cs.scroll;
					const auto bot_t = std::clamp( remaining / fade_h, 0.0f, 1.0f );
					if ( bot_t > 0.01f )
					{
						auto bot = bg_solid;
						bot.a = static_cast< std::uint8_t >( bg_solid.a * bot_t );
						dl.rect_filled_gradient( win->bounds.x, win->bounds.bottom( ) - fade_h, full_w, fade_h, bg_clear, bg_clear, bot, bot, xdraw::corner_radius{ 0.0f, 0.0f, cr, cr } );
					}
				}

				if ( max_scroll > 0.0f && sb_alpha > 0.01f )
				{
					auto& dl = draw::current( );
					const auto thumb_hovered = thumb.contains( input.mouse_x, input.mouse_y );
					const auto thumb_active = thumb_hovered || cs.scrollbar_dragging;

					auto track_col = xdraw::color{ 255, 255, 255, 30 };
					track_col.a = static_cast< std::uint8_t >( track_col.a * sb_alpha );

					auto thumb_col = thumb_active ? xdraw::color{ 255, 255, 255, 150 } : xdraw::color{ 255, 255, 255, 100 };
					thumb_col.a = static_cast< std::uint8_t >( thumb_col.a * sb_alpha );

					const auto track_r = k_scrollbar_w * 0.5f;
					dl.rect_filled( track.x, track.y, track.w, track.h, track_col, xdraw::corner_radius{ track_r } );
					dl.rect_filled( thumb.x, thumb.y, thumb.w, thumb.h, thumb_col, xdraw::corner_radius{ track_r } );
				}
			}
			else
			{
				c.child_height_cache[ win->group_id ] = true_content_h;
				draw::current( ).pop_clip( );
			}
		}
		else
		{
			draw::current( ).pop_clip( );
		}

		c.windows.pop_back( );
		pop_id( );
	}

	bool begin_popup( std::string_view label, float width )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto& s = c.style;
		const auto& input = c.input;

		constexpr auto dot_area_w{ 16.0f };
		const auto dot_area_h = win->last_item.h;
		const auto far_right = win->bounds.w - s.window_pad_x - dot_area_w;
		const auto dot_local_y = win->last_item.y;
		const auto dot_abs = rect{ std::floorf( win->bounds.x + far_right ), std::floorf( win->bounds.y + dot_local_y ), dot_area_w, dot_area_h };

		const auto is_open = overlays::is_open( id );
		const auto can_interact = !c.overlay_blocking( ) || is_open;
		const auto hovered = can_interact && input.in_rect( dot_abs );

		const auto hover_anim = anim::lerp( id + 1, hovered ? 1.0f : 0.0f, 12.0f );
		const auto open_anim = anim::lerp( id + 2, is_open ? 1.0f : 0.0f, 10.0f );
		auto dot_col = lerp( s.text_dim, s.accent, std::max( hover_anim, open_anim ) );

		constexpr auto dot_r{ 1.5f };
		constexpr auto dot_spacing{ 4.0f };
		const auto total_dots_w = dot_r * 2.0f * 3.0f + dot_spacing * 2.0f;
		const auto start_x = dot_abs.x + ( dot_area_w - total_dots_w ) * 0.5f;
		const auto cy = dot_abs.y + dot_area_h * 0.5f;

		auto& dl = draw::current( );

		for ( int i = 0; i < 3; ++i )
		{
			const auto cx = start_x + dot_r + static_cast< float >( i ) * ( dot_r * 2.0f + dot_spacing );
			dl.circle_filled( cx, cy, dot_r, dot_col, 8 );
		}

		if ( hovered && input.mouse_clicked )
		{
			if ( is_open )
			{
				overlays::close( id );
			}
			else
			{
				overlays::add( std::make_unique<popup_overlay>( id, dot_abs, width ) );
			}
		}

		auto ov = dynamic_cast< popup_overlay* >( overlays::find( id ) );
		if ( ov && !ov->is_closed( ) )
		{
			overlays::touch( id );
			ov->update_anchor( dot_abs );
			ov->tick( );

			const auto popup_rect = ov->get_popup( );
			const auto et = ease::out_cubic( ov->open_anim( ) );
			const auto animated_h = popup_rect.h * et;
			const auto alpha_mult = et;

			auto& top_dl = xdraw::get( xdraw::layer::top );

			auto bg = s.popup_bg;
			bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
			auto border = lighten( s.popup_border, 1.1f );
			border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

			if ( animated_h > 1.0f )
			{
				top_dl.rect_filled_blurred( popup_rect.x, popup_rect.y, popup_rect.w, animated_h, xdraw::corner_radius{ s.popup_rounding }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 210.0f * alpha_mult ) } );
			}
			top_dl.rect_filled( popup_rect.x, popup_rect.y, popup_rect.w, animated_h, bg, xdraw::corner_radius{ s.popup_rounding } );
			top_dl.rect( popup_rect.x, popup_rect.y, popup_rect.w, animated_h, border, xdraw::corner_radius{ s.popup_rounding } );

			draw::push_layer( xdraw::layer::top );

			window_state state{};
			state.title = std::string( label );
			state.bounds = popup_rect;
			state.cursor_x = s.window_pad_x;
			state.cursor_y = s.window_pad_y;
			state.is_child = true;

			c.windows.push_back( std::move( state ) );
			push_id( id );

			top_dl.push_clip( popup_rect.x, popup_rect.y, popup_rect.w, animated_h );

			c.inside_overlay = id;

			return true;
		}

		return false;
	}

	void end_popup( )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return;
		}

		const auto& s = get_ctx( ).style;
		const auto content_h = win->content_h + s.window_pad_y;

		for ( auto& o : get_overlays( ).list )
		{
			auto ov = dynamic_cast< popup_overlay* >( o.get( ) );
			if ( ov && !ov->closing( ) )
			{
				ov->set_content_h( content_h );
				break;
			}
		}

		get_ctx( ).inside_overlay = null_id;

		xdraw::get( xdraw::layer::top ).pop_clip( );
		pop_id( );
		get_ctx( ).windows.pop_back( );
		draw::pop_layer( );
	}

	void text( std::string_view label, xdraw::color col )
	{
		if ( !layout::current_window( ) )
		{
			return;
		}

		const auto [display, full] = parse_label( label );
		const auto [lw, lh] = xdraw::measure_text( display );
		const auto abs = layout::item( lw, lh );

		draw::current( ).text( abs.x, abs.y, display, col );
	}

	bool button( std::string_view label, float w, float h )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& s = c.style;
		const auto& input = c.input;

		const auto abs = layout::item( w, h );
		const auto can_interact = !c.overlay_blocking( );
		const auto hovered = can_interact && input.in_rect( abs );
		const auto held = hovered && input.mouse_down;
		const auto pressed = hovered && input.mouse_clicked;

		const auto hover_anim = anim::lerp( id, hovered ? 1.0f : 0.0f, 12.0f );
		const auto active_anim = anim::lerp( id + 1, held ? 1.0f : 0.0f, 15.0f );

		auto bg = lerp( s.button_bg, s.button_hovered, hover_anim );
		bg = lerp( bg, s.button_active, active_anim );

		const auto r = s.button_rounding;
		const auto border = lerp( s.button_border, lighten( s.button_border, 1.2f ), hover_anim );

		auto& dl = draw::current( );
		dl.rect_filled( abs.x, abs.y, abs.w, abs.h, bg, xdraw::corner_radius{ r } );
		dl.rect( abs.x, abs.y, abs.w, abs.h, border, xdraw::corner_radius{ r } );

		if ( !display.empty( ) )
		{
			const auto available_w = abs.w - s.frame_pad_x * 2.0f;
			const auto label_text = truncate( display, available_w );
			const auto [lw, lh] = xdraw::measure_text( label_text );
			const auto tx = abs.x + ( abs.w - lw ) * 0.5f;
			const auto ty = abs.y + ( abs.h - lh ) * 0.5f;
			const auto text_col = lerp( s.text_dim, s.text, hover_anim );
			dl.text( tx, ty, label_text, text_col );
		}

		return pressed;
	}

	namespace {

		void draw_minimal_checkbox(
			xdraw::draw_list& dl,
			float x,
			float y,
			float size,
			float t,
			const style& st,
			float alpha_mult = 1.0f )
		{
			auto bg = st.checkbox_bg;
			bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
			dl.rect_filled( x, y, size, size, bg, xdraw::corner_radius{ st.checkbox_rounding } );

			if ( t > 0.01f )
			{
				auto mark = st.checkbox_mark;
				mark.a = static_cast< std::uint8_t >( mark.a * t * alpha_mult );
				dl.rect_filled( x, y, size, size, mark, xdraw::corner_radius{ st.checkbox_rounding } );
			}
		}

		void draw_minimal_slider(
			xdraw::draw_list& dl,
			float track_x,
			float track_y,
			float track_w,
			float track_h,
			float norm_pos,
			const style& st )
		{
			constexpr auto thumb_r{ 4.0f };
			const auto track_r = track_h * 0.5f;
			const auto thumb_cx = track_x + std::clamp( norm_pos, 0.0f, 1.0f ) * track_w;
			const auto fill_w = std::max( thumb_r, thumb_cx - track_x );

			dl.rect_filled( track_x, track_y, track_w, track_h, st.slider_track, xdraw::corner_radius{ track_r } );

			if ( fill_w > 0.5f )
			{
				dl.rect_filled( track_x, track_y, fill_w, track_h, st.slider_fill, xdraw::corner_radius{ track_r } );
			}

		}

		class checkbox_bind_overlay : public overlay
		{
		public:
			checkbox_bind_overlay( std::uintptr_t id, const rect& anchor, setting* s ) : overlay{ id, anchor }, m_setting{ s }
			{
				this->m_hover_anims.fill( 0.0f );
				this->m_item_anims.fill( 0.0f );
			}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y );
			}

			bool process_input( const input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto popup = this->get_popup( );

				if ( this->m_listening )
				{
					if ( input.mouse_clicked && !popup.contains( input.mouse_x, input.mouse_y ) )
					{
						this->m_listening = false;
						binds::get_bind_registry( ).listening_setting = 0;
						return true;
					}

					for ( const auto vk : input.key_presses( ) )
					{
						if ( vk == VK_ESCAPE )
						{
							if ( this->m_setting )
							{
								this->m_setting->bind.key = 0;
								this->m_setting->bind.active = false;
							}
						}
						else
						{
							if ( this->m_setting )
							{
								this->m_setting->bind.key = vk;
							}
						}

						this->m_listening = false;
						binds::get_bind_registry( ).listening_setting = 0;
						return true;
					}

					if ( input.rmb_clicked )
					{
						if ( this->m_setting )
						{
							this->m_setting->bind.key = VK_RBUTTON;
						}

						this->m_listening = false;
						binds::get_bind_registry( ).listening_setting = 0;
						return true;
					}

					return true;
				}

				if ( ( input.mouse_clicked || input.rmb_clicked ) && !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return true;
				}

				if ( input.mouse_clicked && popup.contains( input.mouse_x, input.mouse_y ) )
				{
					for ( int i = 0; i < k_item_count; ++i )
					{
						const auto ir = this->get_item_rect( popup, i );

						if ( ir.contains( input.mouse_x, input.mouse_y ) )
						{
							if ( i == 0 )
							{
								this->m_listening = true;
								binds::get_bind_registry( ).listening_setting = reinterpret_cast< std::uintptr_t >( this->m_setting );
							}
							else if ( i == 1 )
							{
								if ( this->m_setting )
								{
									auto m = static_cast< int >( this->m_setting->bind.mode );
									m = ( m + 1 ) % 3;
									this->m_setting->bind.mode = static_cast< bind_mode >( m );
								}
							}
							else if ( i == 2 )
							{
								if ( this->m_setting )
								{
									this->m_setting->bind.key = 0;
									this->m_setting->bind.active = false;
									this->m_setting->bind.mode = bind_mode::toggle;
								}

								this->m_closing = true;
							}

							return true;
						}
					}
				}

				return popup.contains( input.mouse_x, input.mouse_y );
			}

			void render( const style& style, const input_state& input ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto popup = this->get_popup( );
				auto& dl = xdraw::get( xdraw::layer::top );

				const auto speed = this->m_closing ? 18.0f : 16.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto ease_t = ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = ease_t;
				const auto animated_h = popup.h * ease_t;
				const auto pr = style.popup_rounding;

				auto bg = style.popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = lighten( style.popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				if ( animated_h > 1.0f )
				{
					dl.rect_filled_blurred( popup.x, popup.y, popup.w, animated_h, xdraw::corner_radius{ pr }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 210.0f * alpha_mult ) } );
				}
				dl.rect_filled( popup.x, popup.y, popup.w, animated_h, bg, xdraw::corner_radius{ pr } );

				if ( border.a > 0 )
				{
					dl.rect( popup.x, popup.y, popup.w, animated_h, border, xdraw::corner_radius{ pr } );
				}

				dl.push_clip( popup.x, popup.y, popup.w, animated_h );

				const auto key_text = this->m_listening ? "..." : ( this->m_setting ? vk_name( this->m_setting->bind.key ) : "none" );
				const auto mode_text = this->m_setting ? binds::mode_name( this->m_setting->bind.mode ) : "toggle";

				for ( int i = 0; i < k_item_count; ++i )
				{
					const auto ir = this->get_item_rect( popup, i );

					const auto item_delay = i * 0.05f;
					const auto item_progress = std::clamp( this->m_open_anim - item_delay, 0.0f, 1.0f ) / ( 1.0f - std::min( item_delay, 0.25f ) );
					auto& ia = this->m_item_anims[ i ];
					ia = std::min( ia + 20.0f * dt, item_progress );

					const auto item_ease = ease::out_cubic( ia );
					const auto item_alpha = item_ease * alpha_mult;
					const auto slide = ( 1.0f - item_ease ) * 6.0f;

					const auto is_hovered = !this->m_closing && ir.contains( input.mouse_x, input.mouse_y ) && !( this->m_listening && i != 0 );
					auto& ha = this->m_hover_anims[ i ];
					ha += ( ( is_hovered ? 1.0f : 0.0f ) - ha ) * std::min( 18.0f * dt, 1.0f );

					const auto is_first = ( i == 0 );
					const auto is_last = ( i == k_item_count - 1 );

					if ( ha > 0.01f )
					{
						auto hov = style.combo_popup_item_hovered;
						hov.a = static_cast< std::uint8_t >( hov.a * item_alpha * ha );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, hov, xdraw::corner_radius{ is_first ? pr : 0.0f, is_first ? pr : 0.0f, is_last ? pr : 0.0f, is_last ? pr : 0.0f } );
					}

					if ( i == 0 && this->m_listening )
					{
						this->m_listen_pulse += dt * 3.0f;
						if ( this->m_listen_pulse > 6.28318f )
						{
							this->m_listen_pulse -= 6.28318f;
						}

						const auto pulse = std::sin( this->m_listen_pulse ) * 0.5f + 0.5f;
						auto pulse_col = style.keybind_waiting;
						pulse_col.a = static_cast< std::uint8_t >( 30.0f * item_alpha * ( 0.5f + pulse * 0.5f ) );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, pulse_col, xdraw::corner_radius{ is_first ? pr : 0.0f, is_first ? pr : 0.0f, 0.0f, 0.0f } );
					}

					if ( i == 2 )
					{
						auto text_col = style.text_dim;
						text_col = lerp( text_col, style.text, ha );
						text_col.a = static_cast< std::uint8_t >( text_col.a * item_alpha );

						const auto [cw, ch] = xdraw::measure_text( "clear bind" );
						dl.text( ir.x + ( ir.w - cw ) * 0.5f, ir.y + ( k_item_h - ch ) * 0.5f + slide, "clear bind", text_col );
					}
					else
					{
						static constexpr const char* row_labels[ ]{ "key", "mode" };
						const char* row_values[ ]{ key_text, mode_text };

						const auto [lw, lh] = xdraw::measure_text( row_labels[ i ] );
						auto label_col = style.text_dim;
						label_col.a = static_cast< std::uint8_t >( label_col.a * item_alpha );
						dl.text( ir.x + 8.0f, ir.y + ( k_item_h - lh ) * 0.5f + slide, row_labels[ i ], label_col );

						const auto [vw, vh] = xdraw::measure_text( row_values[ i ] );
						auto val_col = ( i == 0 && this->m_listening ) ? style.keybind_waiting : style.text;
						val_col = lerp( val_col, lighten( val_col, 1.3f ), ha );
						val_col.a = static_cast< std::uint8_t >( val_col.a * item_alpha );
						dl.text( ir.right( ) - vw - 8.0f, ir.y + ( k_item_h - vh ) * 0.5f + slide, row_values[ i ], val_col );
					}
				}

				dl.pop_clip( );
			}

		private:
			static constexpr auto k_item_count{ 3 };
			static constexpr auto k_item_h{ 24.0f };
			static constexpr auto k_pad{ 4.0f };
			static constexpr auto k_popup_w{ 150.0f };

			[[nodiscard]] rect get_popup( ) const
			{
				const auto h = k_pad * 2.0f + k_item_h * static_cast< float >( k_item_count );
				return { this->m_anchor.x, this->m_anchor.y, k_popup_w, h };
			}

			[[nodiscard]] rect get_item_rect( const rect& popup, int index ) const
			{
				return { popup.x + k_pad, popup.y + k_pad + static_cast< float >( index ) * k_item_h, popup.w - k_pad * 2.0f, k_item_h };
			}

			setting* m_setting{};
			float m_open_anim{};
			bool m_listening{};
			float m_listen_pulse{};
			std::array<float, k_item_count> m_hover_anims{};
			std::array<float, k_item_count> m_item_anims{};
		};

	} // the checkbox_bind namespace

	bool checkbox( std::string_view label, setting& s )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		binds::register_setting( &s );

		if ( s.name.empty( ) )
		{
			const auto [display, full] = parse_label( label );
			s.name = std::string( display );
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& st = c.style;
		const auto& input = c.input;

		const auto check_size = st.checkbox_size;
		const auto abs = layout::item( check_size, check_size );

		const auto [lw, lh] = xdraw::measure_text( display );
		const auto full_w = !display.empty( ) ? ( abs.w + st.item_spacing_x + lw ) : abs.w;
		const auto extended = rect{ abs.x, abs.y, full_w, abs.h };

		const auto can_interact = !c.overlay_blocking( );
		auto changed{ false };

		const auto badge_id = id + 300;
		auto& reg = binds::get_bind_registry( );
		const auto badge_listening = ( reg.listening_setting == badge_id );

		if ( badge_listening )
		{
			for ( const auto vk : input.key_presses( ) )
			{
				if ( vk == VK_ESCAPE )
				{
					s.bind.key = 0;
					s.bind.active = false;
				}
				else
				{
					s.bind.key = vk;
				}

				reg.listening_setting = 0;
				break;
			}

			if ( badge_listening && input.rmb_clicked )
			{
				s.bind.key = VK_RBUTTON;
				reg.listening_setting = 0;
			}
		}

		const auto hovered = can_interact && input.in_rect( extended ) && !badge_listening;
		if ( hovered && input.mouse_clicked )
		{
			s.value = !s.value;
			s.bind.active = s.value;

			if ( s.value && s.bind.excludes )
			{
				s.bind.excludes->bind.active = false;
				s.bind.excludes->value = false;
			}

			changed = true;
		}

		const auto ctx_id = id + 200;
		const auto ctx_is_open = overlays::is_open( ctx_id );

		if ( ctx_is_open )
		{
			overlays::touch( ctx_id );
		}

		if ( hovered && input.rmb_clicked && ( !c.overlay_blocking( ) || ctx_is_open ) )
		{
			if ( ctx_is_open )
			{
				overlays::close( ctx_id );
			}
			else if ( !c.overlay_blocking( ) )
			{
				const auto mouse_anchor = rect{ input.mouse_x, input.mouse_y, 0.0f, 0.0f };
				overlays::add( std::make_unique<checkbox_bind_overlay>( ctx_id, mouse_anchor, &s ) );
			}
		}

		const auto check_anim = anim::lerp( id, s.value ? 1.0f : 0.0f, 8.0f );
		const auto hover_anim = anim::lerp( id + 1, hovered ? 1.0f : 0.0f, 10.0f );
		const auto ease_t = ease::smoothstep( check_anim );

		auto& dl = draw::current( );

		const auto now = std::chrono::steady_clock::now( );
		const auto& hl = get_highlight_state( );
		const auto is_highlighted = !hl.label.empty( ) && now < hl.expires_at && display == hl.label;
		const auto highlight_anim = anim::lerp( id + 97, is_highlighted ? 1.0f : 0.0f, 18.0f );

		draw_minimal_checkbox( dl, abs.x, abs.y, abs.w, ease_t, st );

		if ( highlight_anim > 0.01f )
		{
			auto pulse = st.accent;
			pulse.a = static_cast< std::uint8_t >( std::min( 255.0f, pulse.a * ( 0.2f + highlight_anim * 0.55f ) ) );
			dl.rect_filled( extended.x - 4.0f, extended.y - 2.0f, extended.w + 8.0f, extended.h + 4.0f, pulse, xdraw::corner_radius{ 8.0f } );
		}

		if ( !display.empty( ) )
		{
			const auto tx = abs.x + abs.w + st.item_spacing_x;
			const auto ty = abs.y + ( check_size - lh ) * 0.5f;
			const auto available_w = win->bounds.right( ) - tx - st.window_pad_x;
			const auto label_text = truncate( display, available_w );
			auto label_col = lerp( st.text_dim, st.text, std::max( check_anim, hover_anim ) );
			if ( highlight_anim > 0.01f )
			{
				const auto glow_col = lerp( st.accent, st.text, 0.35f );
				label_col = lerp( label_col, glow_col, std::clamp( highlight_anim, 0.0f, 1.0f ) );

				auto shadow = st.accent;
				shadow.a = static_cast< std::uint8_t >( std::min( 255.0f, 120.0f * highlight_anim ) );
				dl.text( tx + 1.0f, ty, label_text, shadow );
			}
			dl.text( tx, ty, label_text, label_col );

			if ( s.bind.key != 0 || badge_listening )
			{
				const auto badge_text = badge_listening ? "..." : vk_name( s.bind.key );
				const auto [kw, kh] = xdraw::measure_text( badge_text );

				const auto badge_pad_x{ 4.0f };
				const auto badge_pad_y{ 1.0f };
				const auto badge_w = kw + badge_pad_x * 2.0f;
				const auto badge_h = kh + badge_pad_y * 2.0f;

				const auto [displayed_w, displayed_h] = xdraw::measure_text( label_text );
				const auto badge_x = tx + displayed_w + 4.0f;
				const auto badge_y = ty + ( lh - badge_h ) * 0.5f;
				const auto badge_rect = rect{ badge_x, badge_y, badge_w, badge_h };

				if ( badge_x + badge_w < win->bounds.right( ) - st.window_pad_x )
				{
					const auto badge_hovered = can_interact && input.in_rect( badge_rect );
					if ( badge_hovered && input.mouse_clicked && !badge_listening && !c.overlay_blocking( ) )
					{
						reg.listening_setting = badge_id;
					}
					else if ( badge_listening && input.mouse_clicked && !badge_hovered )
					{
						reg.listening_setting = 0;
					}

					const auto bind_active_anim = anim::lerp( id + 50, s.value ? 1.0f : 0.0f, 8.0f );
					const auto badge_hover_anim = anim::lerp( id + 51, ( badge_hovered || badge_listening ) ? 1.0f : 0.0f, 12.0f );

					auto badge_bg = st.keybind_bg;
					badge_bg.a = static_cast< std::uint8_t >( std::max( badge_bg.a, static_cast< std::uint8_t >( 40 ) ) );
					badge_bg = lighten( badge_bg, 1.0f + badge_hover_anim * 0.3f );

					auto badge_border = lerp( st.keybind_border, st.accent, std::max( bind_active_anim * 0.5f, badge_hover_anim * 0.8f ) );
					badge_border.a = static_cast< std::uint8_t >( std::max( badge_border.a, static_cast< std::uint8_t >( 30 ) ) );

					if ( badge_listening )
					{
						badge_border = lerp( badge_border, st.keybind_waiting, 0.6f );
					}

					const auto badge_r = st.keybind_rounding;

					dl.rect_filled( badge_x, badge_y, badge_w, badge_h, badge_bg, xdraw::corner_radius{ badge_r } );
					dl.rect( badge_x, badge_y, badge_w, badge_h, badge_border, xdraw::corner_radius{ badge_r } );

					auto key_col = lerp( st.text_dim, st.accent, bind_active_anim );
					key_col = lerp( key_col, st.text, badge_hover_anim );

					if ( badge_listening )
					{
						key_col = st.keybind_waiting;
					}

					dl.text( badge_x + badge_pad_x, badge_y + badge_pad_y, badge_text, key_col );
				}
			}
		}

		return changed;
	}

	namespace {

		template<typename T>
		inline bool slider( std::uintptr_t id, std::string_view label, T& v, T v_min, T v_max, std::string_view fmt )
		{
			auto win = layout::current_window( );
			if ( !win )
			{
				return false;
			}

			auto& c = get_ctx( );
			const auto [display, full] = parse_label( label );
			const auto& s = c.style;
			const auto& input = c.input;
			auto changed{ false };

			const auto [avail_w, avail_h] = layout::avail( );
			const auto slider_width = avail_w;

			char value_buf[ 64 ]{};
			if constexpr ( std::is_floating_point_v<T> )
			{
				std::snprintf( value_buf, sizeof( value_buf ), fmt.data( ), v );
			}
			else
			{
				std::snprintf( value_buf, sizeof( value_buf ), fmt.data( ), static_cast< int >( v ) );
			}

			const auto [label_w, label_h] = xdraw::measure_text( display );
			const auto [value_w, value_h] = xdraw::measure_text( value_buf );

			const auto text_height = std::max( label_h, value_h );
			const auto track_height = s.slider_h;
			constexpr auto thumb_r{ 6.0f };
			const auto thumb_d = thumb_r * 2.0f;
			const auto slider_area_h = std::max( track_height, thumb_d );
			const auto spacing = s.item_spacing_y * 0.25f;
			const auto total_height = text_height + spacing + slider_area_h;

			const auto abs = layout::item( slider_width, total_height, text_height );
			const auto track_y = abs.y + text_height + spacing + ( slider_area_h - track_height ) * 0.5f;
			const auto track_rect = rect{ abs.x, track_y, slider_width, track_height };
			const auto slider_hit_rect = rect{ abs.x, abs.y + text_height + spacing, slider_width, slider_area_h };

			const auto can_interact = !c.overlay_blocking( );

			const auto is_editing = c.active_slider_edit == id;
			const auto value_text_x = abs.x + slider_width - value_w;
			const auto value_text_rect = rect{ value_text_x, abs.y, value_w, text_height };

			if ( is_editing )
			{
				auto& buf = c.slider_edit_buf;
				auto& cursor = c.slider_edit_cursor;

				const auto [edit_w, edit_h] = xdraw::measure_text( buf.empty( ) ? "0" : buf );
				const auto edit_box_w = std::max( value_w + 16.0f, edit_w + 12.0f );
				const auto edit_box_x = std::floorf( abs.x + slider_width - edit_box_w );
				const auto edit_box_rect = rect{ edit_box_x, abs.y, edit_box_w, text_height };

				if ( input.mouse_clicked && !input.in_rect( edit_box_rect ) )
				{
					if constexpr ( std::is_floating_point_v<T> )
					{
						v = std::clamp( static_cast< T >( std::stod( buf ) ), v_min, v_max );
					}
					else
					{
						v = std::clamp( static_cast< T >( std::stoi( buf ) ), v_min, v_max );
					}

					changed = true;

					c.active_slider_edit = null_id;
				}
				else
				{
					for ( const auto vk : input.key_presses( ) )
					{
						if ( vk == VK_RETURN )
						{
							if constexpr ( std::is_floating_point_v<T> )
							{
								v = std::clamp( static_cast< T >( std::stod( buf ) ), v_min, v_max );
							}
							else
							{
								v = std::clamp( static_cast< T >( std::stoi( buf ) ), v_min, v_max );
							}

							changed = true;

							c.active_slider_edit = null_id;
							break;
						}
						else if ( vk == VK_ESCAPE )
						{
							c.active_slider_edit = null_id;
							break;
						}
						else if ( vk == VK_BACK && cursor > 0 )
						{
							buf.erase( cursor - 1, 1 );
							cursor--;
						}
						else if ( vk == VK_DELETE && cursor < buf.size( ) )
						{
							buf.erase( cursor, 1 );
						}
						else if ( vk == VK_LEFT && cursor > 0 )
						{
							cursor--;
						}
						else if ( vk == VK_RIGHT && cursor < buf.size( ) )
						{
							cursor++;
						}
						else if ( vk == VK_HOME )
						{
							cursor = 0;
						}
						else if ( vk == VK_END )
						{
							cursor = buf.size( );
						}
					}

					if ( c.active_slider_edit == id )
					{
						for ( const auto ch : input.chars( ) )
						{
							if ( ( ch >= '0' && ch <= '9' ) || ch == '.' || ch == '-' )
							{
								buf.insert( buf.begin( ) + static_cast< std::ptrdiff_t >( cursor ), static_cast< char >( ch ) );
								cursor++;
							}
						}
					}
				}

				if ( c.active_slider_edit == id )
				{
					const auto dt = xdraw::delta_time( );
					const auto edit_anim = anim::lerp( id + 2000000, 1.0f, 12.0f );
					const auto tr = s.checkbox_rounding;

					auto border_col = lerp( s.text_input_border, s.accent, edit_anim );
					auto bg_col = lerp( s.text_input_bg, lighten( s.text_input_bg, 1.08f ), edit_anim );

					auto& dl = draw::current( );
					dl.rect_filled( edit_box_rect.x, edit_box_rect.y, edit_box_rect.w, edit_box_rect.h, bg_col, xdraw::corner_radius{ tr } );
					dl.rect( edit_box_rect.x, edit_box_rect.y, edit_box_rect.w, edit_box_rect.h, border_col, xdraw::corner_radius{ tr } );

					const auto text_pad{ 4.0f };
					const auto [buf_w, buf_h] = xdraw::measure_text( buf.empty( ) ? "0" : buf );
					const auto buf_y = std::floorf( edit_box_rect.y + ( edit_box_rect.h - buf_h ) * 0.5f );

					if ( !buf.empty( ) )
					{
						dl.text( edit_box_rect.x + text_pad, buf_y, buf, s.text );
					}

					const auto cursor_text = buf.substr( 0, cursor );
					const auto [cw, ch] = xdraw::measure_text( cursor_text.empty( ) ? "" : cursor_text );
					const auto cursor_x = std::floorf( edit_box_rect.x + text_pad + cw );
					const auto cursor_h = std::floorf( ( buf_h > 0.0f ? buf_h : text_height ) * 0.7f );
					const auto cursor_y = std::floorf( edit_box_rect.y + ( edit_box_rect.h - cursor_h ) * 0.5f );

					c.slider_edit_blink += dt;

					if ( c.slider_edit_blink > 1.0f )
					{
						c.slider_edit_blink -= 1.0f;
					}

					const auto blink_a = std::sin( c.slider_edit_blink * 6.28318f ) * 0.5f + 0.5f;
					auto cursor_col = s.text;
					cursor_col.a = static_cast< std::uint8_t >( 255.0f * ( 0.4f + 0.6f * blink_a ) );
					dl.rect_filled( cursor_x, cursor_y, 1.0f, cursor_h, cursor_col );
				}
			}
			else
			{
				const auto hovered = can_interact && input.in_rect( slider_hit_rect );
				const auto value_hovered = can_interact && input.in_rect( value_text_rect );

				if ( value_hovered && input.mouse_double_clicked && c.active_slider == null_id )
				{
					c.active_slider_edit = id;
					c.slider_edit_buf = value_buf;
					c.slider_edit_cursor = c.slider_edit_buf.size( );
					c.slider_edit_blink = 0.0f;
				}
				else if ( hovered && input.mouse_clicked && c.active_slider == null_id )
				{
					c.active_slider = id;
				}

				if ( c.active_slider == id && input.mouse_down && can_interact )
				{
					const auto norm = std::clamp( ( input.mouse_x - track_rect.x ) / track_rect.w, 0.0f, 1.0f );

					if constexpr ( std::is_integral_v<T> )
					{
						v = static_cast< T >( v_min + norm * ( v_max - v_min ) );
					}
					else
					{
						v = v_min + norm * ( v_max - v_min );
					}

					changed = true;
				}

				if ( hovered && can_interact )
				{
					for ( const auto vk : input.key_presses( ) )
					{
						if ( vk == VK_LEFT )
						{
							if constexpr ( std::is_integral_v<T> )
							{
								v = std::max( v_min, v - 1 );
							}
							else
							{
								v = std::max( v_min, v - ( v_max - v_min ) * 0.01f );
							}

							changed = true;
						}
						else if ( vk == VK_RIGHT )
						{
							if constexpr ( std::is_integral_v<T> )
							{
								v = std::min( v_max, v + 1 );
							}
							else
							{
								v = std::min( v_max, v + ( v_max - v_min ) * 0.01f );
							}

							changed = true;
						}
					}
				}

				const auto value_col = lerp( s.text_dim, s.accent, std::max( anim::get( id + 1000000 ) * 0.65f, c.active_slider == id ? 0.85f : 0.0f ) );
				draw::current( ).text( abs.x + slider_width - value_w, abs.y, value_buf, value_col );
			}

			auto& value_anim = get_anim_states( )[ id ];
			value_anim = value_anim + ( static_cast< float >( v ) - value_anim ) * std::min( 20.0f * xdraw::delta_time( ), 1.0f );

			const auto hovered_or_active = ( can_interact && input.in_rect( slider_hit_rect ) ) || c.active_slider == id;
			auto& hover_anim_val = get_anim_states( )[ id + 1000000 ];
			hover_anim_val = hover_anim_val + ( ( hovered_or_active ? 1.0f : 0.0f ) - hover_anim_val ) * std::min( 12.0f * xdraw::delta_time( ), 1.0f );

			const auto range = static_cast< float >( v_max - v_min );
			const auto norm_pos = ( value_anim - static_cast< float >( v_min ) ) / range;

			auto& dl = draw::current( );

			if ( !display.empty( ) && !is_editing )
			{
				const auto available_label_w = slider_width - value_w - s.item_spacing_x;
				const auto label_text = truncate( display, available_label_w );
				const auto label_col = lerp( s.text_dim, s.text, hover_anim_val );
				dl.text( abs.x, abs.y, label_text, label_col );
			}

			draw_minimal_slider( dl, track_rect.x, track_rect.y, track_rect.w, track_rect.h, norm_pos, s );

			return changed;
		}

	} // the slider namespace

	bool slider_float( std::string_view label, float& v, float v_min, float v_max, std::string_view fmt )
	{
		return slider( make_id( label ), label, v, v_min, v_max, fmt );
	}

	bool slider_int( std::string_view label, int& v, int v_min, int v_max, std::string_view fmt )
	{
		return slider( make_id( label ), label, v, v_min, v_max, fmt );
	}

	bool keybind( std::string_view label, int& key )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& s = c.style;
		const auto& input = c.input;

		const auto is_waiting = ( c.active_keybind == id );
		auto changed = false;

		const auto button_text = is_waiting ? "..." : vk_name( key );
		const auto [btw, bth] = xdraw::measure_text( button_text );
		const auto [lw, lh] = xdraw::measure_text( display );

		const auto button_width = s.keybind_w;
		const auto button_height = s.keybind_h;
		const auto total_w = !display.empty( ) ? ( button_width + s.item_spacing_x + lw ) : button_width;
		const auto total_h = std::max( button_height, lh );

		const auto abs = layout::item( total_w, total_h );
		const auto button_rect = rect{ abs.x, abs.y, button_width, button_height };

		const auto can_interact = !c.overlay_blocking( );
		const auto hovered = can_interact && input.in_rect( button_rect );

		if ( hovered && input.mouse_clicked )
		{
			c.active_keybind = id;
		}

		const auto dt = xdraw::delta_time( );
		const auto hover_anim = anim::lerp( id, hovered ? 1.0f : 0.0f, 12.0f );
		const auto wait_anim = anim::lerp( id + 1, is_waiting ? 1.0f : 0.0f, 12.0f );
		auto& pulse_anim = get_anim_states( )[ id + 2 ];

		if ( is_waiting )
		{
			pulse_anim += dt * 3.0f;

			if ( pulse_anim > 6.28318f )
			{
				pulse_anim -= 6.28318f;
			}
		}

		const auto r = s.keybind_rounding;
		auto bg = lerp( s.keybind_bg, lighten( s.keybind_bg, 1.05f ), hover_anim );
		auto border = lerp( s.keybind_border, lighten( s.keybind_border, 1.3f ), hover_anim );

		auto& dl = draw::current( );
		dl.rect_filled( button_rect.x, button_rect.y, button_rect.w, button_rect.h, bg, xdraw::corner_radius{ r } );

		if ( wait_anim > 0.01f )
		{
			constexpr auto pad{ 2.0f };
			const auto fx = button_rect.x + pad;
			const auto fy = button_rect.y + pad;
			const auto fw = button_rect.w - pad * 2.0f;
			const auto fh = button_rect.h - pad * 2.0f;

			const auto pulse_i = ( std::sin( pulse_anim ) * 0.5f + 0.5f ) * 0.4f + 0.6f;
			const auto shift = std::sin( pulse_anim * 0.5f ) * 0.5f + 0.5f;

			auto col_l = lighten( s.keybind_waiting, 1.0f + pulse_i * 0.3f );
			auto col_r = darken( s.keybind_waiting, 0.7f + pulse_i * 0.2f );
			col_l.a = static_cast< std::uint8_t >( col_l.a * wait_anim * 0.4f );
			col_r.a = static_cast< std::uint8_t >( col_r.a * wait_anim * 0.4f );

			const auto ikr = std::max( 0.0f, r - 2.0f );
			if ( shift > 0.5f )
			{
				dl.rect_filled_gradient( fx, fy, fw, fh, col_r, col_l, col_l, col_r, xdraw::corner_radius{ ikr } );
			}
			else
			{
				dl.rect_filled_gradient( fx, fy, fw, fh, col_l, col_r, col_r, col_l, xdraw::corner_radius{ ikr } );
			}

			const auto wb = lighten( s.keybind_waiting, 1.0f + pulse_i * 0.2f );
			border = lerp( border, wb, wait_anim );
		}

		dl.rect( button_rect.x, button_rect.y, button_rect.w, button_rect.h, border, xdraw::corner_radius{ r } );

		const auto kb_text_col = lerp( s.text_dim, s.text, std::max( hover_anim, wait_anim ) );
		const auto tx = button_rect.x + ( button_rect.w - btw ) * 0.5f;
		const auto ty = button_rect.y + ( button_rect.h - bth ) * 0.5f;
		dl.text( tx, ty, button_text, kb_text_col );

		if ( !display.empty( ) )
		{
			const auto lx = button_rect.x + button_width + s.item_spacing_x;
			const auto ly = abs.y + ( total_h - lh ) * 0.5f;
			const auto available_w = win->bounds.right( ) - lx - s.window_pad_x;
			const auto label_text = truncate( display, available_w );
			dl.text( lx, ly, label_text, kb_text_col );
		}

		if ( is_waiting )
		{
			if ( input.mouse_clicked && !hovered )
			{
				key = VK_LBUTTON;
				c.active_keybind = null_id;
				return true;
			}

			if ( input.rmb_clicked )
			{
				key = VK_RBUTTON;
				c.active_keybind = null_id;
				return true;
			}

			for ( const auto vk : input.key_presses( ) )
			{
				if ( vk == VK_ESCAPE )
				{
					key = 0;
				}
				else
				{
					key = vk;
				}

				c.active_keybind = null_id;
				return true;
			}
		}

		return changed;
	}

	namespace {

		class combo_overlay : public overlay
		{
		public:
			combo_overlay( std::uintptr_t id, const rect& anchor, float width, const std::vector<std::string>& items, int* current_item, float item_h, float item_pad, std::function<void( )> on_change = nullptr ) : overlay{ id, anchor }, m_width{ width }, m_items{ items }, m_current_item{ current_item }, m_item_h{ item_h }, m_item_pad{ item_pad }, m_on_change{ std::move( on_change ) }
			{
				this->m_hover_anims.resize( items.size( ), 0.0f );
				this->m_selected_anims.resize( items.size( ), 0.0f );
				this->m_item_anims.resize( items.size( ), 0.0f );

				if ( current_item && *current_item >= 0 && *current_item < static_cast< int >( items.size( ) ) )
				{
					this->m_selected_anims[ *current_item ] = 1.0f;
				}
			}

			bool hit_test( float x, float y ) const override
			{
				return this->get_dropdown( ).contains( x, y ) || this->m_anchor.contains( x, y );
			}

			bool process_input( const input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto dd = this->get_dropdown( );

				if ( input.mouse_clicked && !dd.contains( input.mouse_x, input.mouse_y ) && !this->m_anchor.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return true;
				}

				const auto ms = this->max_scroll( );
				if ( ms > 0.0f )
				{
					const auto track = this->get_scrollbar_track( dd );
					const auto thumb = this->get_scrollbar_thumb( track );

					if ( input.mouse_clicked && thumb.contains( input.mouse_x, input.mouse_y ) )
					{
						this->m_scrollbar_dragging = true;
						this->m_scrollbar_drag_offset = input.mouse_y - thumb.y;
						return true;
					}

					if ( input.mouse_clicked && track.contains( input.mouse_x, input.mouse_y ) )
					{
						const auto target_y = input.mouse_y - thumb.h * 0.5f;
						const auto rel = std::clamp( ( target_y - track.y ) / ( track.h - thumb.h ), 0.0f, 1.0f );
						this->m_scroll_target = rel * ms;
						this->m_scrollbar_dragging = true;
						this->m_scrollbar_drag_offset = thumb.h * 0.5f;
						return true;
					}

					if ( this->m_scrollbar_dragging )
					{
						if ( !input.mouse_down )
						{
							this->m_scrollbar_dragging = false;
						}
						else
						{
							const auto rel = std::clamp( ( input.mouse_y - this->m_scrollbar_drag_offset - track.y ) / ( track.h - thumb.h ), 0.0f, 1.0f );
							this->m_scroll_target = rel * ms;
							this->m_scroll = this->m_scroll_target;
							return true;
						}
					}
				}

				if ( dd.contains( input.mouse_x, input.mouse_y ) && input.scroll_delta != 0.0f )
				{
					this->m_scroll_target -= input.scroll_delta * 30.0f;
					this->m_scroll_target = std::clamp( this->m_scroll_target, 0.0f, this->max_scroll( ) );
					return true;
				}

				if ( input.mouse_clicked && dd.contains( input.mouse_x, input.mouse_y ) )
				{
					for ( int i = 0; i < static_cast< int >( this->m_items.size( ) ); ++i )
					{
						const auto iy = dd.y + this->m_item_pad + i * this->m_item_h - this->m_scroll;
						const auto ir = rect{ dd.x + this->m_item_pad, iy, dd.w - this->m_item_pad * 2.0f, this->m_item_h };

						if ( ir.contains( input.mouse_x, input.mouse_y ) )
						{
							if ( this->m_current_item )
							{
								*this->m_current_item = i;
							}

							if ( this->m_on_change )
							{
								this->m_on_change( );
							}

							this->m_changed = true;
							this->m_closing = true;
							return true;
						}
					}
				}

				return dd.contains( input.mouse_x, input.mouse_y );
			}

			void render( const style& style, const input_state& input ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto dd = get_dropdown( );
				auto& dl = xdraw::get( xdraw::layer::top );

				const auto speed = this->m_closing ? 16.0f : 14.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto ease_t = ease::out_cubic( this->m_open_anim );
				const auto animated_h = dd.h * ease_t;
				const auto alpha_mult = ease_t;

				auto bg = style.combo_popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = lighten( style.combo_popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				const auto pr = style.combo_popup_rounding;

				if ( animated_h > 1.0f )
				{
					dl.rect_filled_blurred( dd.x, dd.y, dd.w, animated_h, xdraw::corner_radius{ pr }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 210.0f * alpha_mult ) } );
				}
				dl.rect_filled( dd.x, dd.y, dd.w, animated_h, bg, xdraw::corner_radius{ pr } );
				dl.rect( dd.x, dd.y, dd.w, animated_h, border, xdraw::corner_radius{ pr } );

				const auto ms = this->max_scroll( );
				const auto popup_hovered = !this->m_closing && dd.contains( input.mouse_x, input.mouse_y );
				const auto sb_target = ( popup_hovered || this->m_scrollbar_dragging ) && ms > 0.0f ? 1.0f : 0.0f;
				const auto sb_speed = sb_target > this->m_scrollbar_anim ? 7.0f : 12.0f;
				this->m_scrollbar_anim += ( sb_target - this->m_scrollbar_anim ) * std::min( sb_speed * dt, 1.0f );

				const auto shrink_phase = std::clamp( this->m_scrollbar_anim / 0.5f, 0.0f, 1.0f );
				const auto shrink_eased = ease::out_cubic( shrink_phase );
				const auto sb_alpha_phase = std::clamp( ( this->m_scrollbar_anim - 0.3f ) / 0.7f, 0.0f, 1.0f );
				const auto sb_alpha = ease::out_cubic( sb_alpha_phase );
				const auto sb_inset = ( k_scrollbar_w + k_scrollbar_pad ) * shrink_eased;

				this->m_scroll_target = std::clamp( this->m_scroll_target, 0.0f, ms );
				this->m_scroll += ( this->m_scroll_target - this->m_scroll ) * std::min( 18.0f * dt, 1.0f );
				this->m_scroll = std::clamp( this->m_scroll, 0.0f, ms );

				dl.push_clip( dd.x - 2.0f, dd.y, dd.w + 4.0f, animated_h );

				const auto item_count = static_cast< int >( this->m_items.size( ) );
				for ( int i = 0; i < item_count; ++i )
				{
					const auto iy = dd.y + this->m_item_pad + i * this->m_item_h - this->m_scroll;

					if ( iy + this->m_item_h <= dd.y || iy >= dd.y + animated_h )
					{
						continue;
					}

					const auto ir = rect
					{
						dd.x + this->m_item_pad,
						iy,
						dd.w - this->m_item_pad * 2.0f - sb_inset,
						this->m_item_h
					};

					const auto is_first = ( i == 0 );
					const auto is_last = ( i == item_count - 1 );
					const auto ir_tl = is_first ? pr : 0.0f;
					const auto ir_tr = is_first ? pr : 0.0f;
					const auto ir_bl = is_last ? pr : 0.0f;
					const auto ir_br = is_last ? pr : 0.0f;

					constexpr auto max_delay{ 0.5f };
					const auto item_count_f = static_cast< float >( item_count );
					const auto item_delay = ( item_count_f > 1.0f ) ? ( max_delay * static_cast< float >( i ) / ( item_count_f - 1.0f ) ) : 0.0f;
					const auto item_progress = std::clamp( ( this->m_open_anim - item_delay ) / ( 1.0f - max_delay ), 0.0f, 1.0f );
					auto& ia = this->m_item_anims[ i ];
					ia = std::min( ia + 18.0f * dt, item_progress );

					const auto item_ease = ease::out_cubic( ia );
					const auto item_alpha = item_ease * alpha_mult;
					const auto slide = ( 1.0f - item_ease ) * 8.0f;

					const auto is_selected = this->m_current_item && ( i == *this->m_current_item );
					const auto is_hovered = ir.contains( input.mouse_x, input.mouse_y );

					auto& ha = this->m_hover_anims[ i ];
					ha += ( ( is_hovered && !this->m_closing ? 1.0f : 0.0f ) - ha ) * std::min( 15.0f * dt, 1.0f );

					auto& sa = this->m_selected_anims[ i ];
					sa += ( ( is_selected ? 1.0f : 0.0f ) - sa ) * std::min( 12.0f * dt, 1.0f );

					const auto se = ease::out_quad( sa );
					if ( se > 0.01f )
					{
						auto sel = style.combo_popup_item_selected;
						sel.a = static_cast< std::uint8_t >( sel.a * item_alpha * se );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, sel, xdraw::corner_radius{ ir_tl, ir_tr, ir_br, ir_bl } );
					}

					if ( ha > 0.01f )
					{
						auto hov = style.combo_popup_item_hovered;
						hov.a = static_cast< std::uint8_t >( hov.a * item_alpha * ha );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, hov, xdraw::corner_radius{ ir_tl, ir_tr, ir_br, ir_bl } );
					}

					const auto [tw, th] = xdraw::measure_text( this->m_items[ i ] );
					auto text_col = style.text;
					auto sel_text = lerp( style.text, style.accent, 0.4f );
					text_col = lerp( text_col, sel_text, se );
					text_col = lerp( text_col, lighten( text_col, 1.3f ), ha );
					text_col.a = static_cast< std::uint8_t >( text_col.a * item_alpha );

					dl.text( ir.x + 10.0f, ir.y + ( this->m_item_h - th ) * 0.5f + slide, this->m_items[ i ], text_col );
				}

				if ( ms > 0.0f )
				{
					constexpr auto fade_h{ 16.0f };
					const auto fcr = std::min( pr, fade_h );

					auto fade_solid = style.combo_popup_bg;
					fade_solid.a = static_cast< std::uint8_t >( fade_solid.a * alpha_mult );
					auto fade_clear = fade_solid;
					fade_clear.a = 0;

					const auto top_t = std::clamp( this->m_scroll / fade_h, 0.0f, 1.0f );
					if ( top_t > 0.01f && animated_h >= fade_h )
					{
						auto top = fade_solid;
						top.a = static_cast< std::uint8_t >( fade_solid.a * top_t );
						dl.rect_filled_gradient( dd.x, dd.y, dd.w, fade_h, top, top, fade_clear, fade_clear, xdraw::corner_radius{ fcr, fcr, 0.0f, 0.0f } );
					}

					const auto remaining = ms - this->m_scroll;
					const auto bot_t = std::clamp( remaining / fade_h, 0.0f, 1.0f );

					if ( bot_t > 0.01f && animated_h >= fade_h )
					{
						auto bot = fade_solid;
						bot.a = static_cast< std::uint8_t >( fade_solid.a * bot_t );
						dl.rect_filled_gradient( dd.x, dd.y + animated_h - fade_h, dd.w, fade_h, fade_clear, fade_clear, bot, bot, xdraw::corner_radius{ 0.0f, 0.0f, fcr, fcr } );
					}
				}

				if ( ms > 0.0f && sb_alpha > 0.01f )
				{
					const auto track = this->get_scrollbar_track( dd );
					const auto thumb = this->get_scrollbar_thumb( track );

					const auto thumb_hovered = thumb.contains( input.mouse_x, input.mouse_y );
					const auto thumb_active = thumb_hovered || this->m_scrollbar_dragging;

					auto track_col = xdraw::color{ 255, 255, 255, 30 };
					track_col.a = static_cast< std::uint8_t >( track_col.a * sb_alpha * alpha_mult );

					auto thumb_col = thumb_active ? xdraw::color{ 255, 255, 255, 150 } : xdraw::color{ 255, 255, 255, 100 };
					thumb_col.a = static_cast< std::uint8_t >( thumb_col.a * sb_alpha * alpha_mult );

					const auto track_r = k_scrollbar_w * 0.5f;
					dl.rect_filled( track.x, track.y, track.w, track.h, track_col, xdraw::corner_radius{ track_r } );
					dl.rect_filled( thumb.x, thumb.y, thumb.w, thumb.h, thumb_col, xdraw::corner_radius{ track_r } );
				}

				dl.pop_clip( );
			}

			[[nodiscard]] bool was_changed( ) const { return this->m_changed; }

		private:
			static constexpr auto k_max_h{ 255.0f };
			static constexpr auto k_scrollbar_w{ 6.0f };
			static constexpr auto k_scrollbar_pad{ 4.0f };

			rect get_dropdown( ) const
			{
				const auto full_h = static_cast< float >( this->m_items.size( ) ) * this->m_item_h + this->m_item_pad * 2.0f;
				return { this->m_anchor.x, this->m_anchor.bottom( ) + 4.0f, this->m_width, std::min( full_h, this->k_max_h ) };
			}

			float max_scroll( ) const
			{
				const auto full_h = static_cast< float >( this->m_items.size( ) ) * this->m_item_h + this->m_item_pad * 2.0f;
				return std::max( 0.0f, full_h - this->k_max_h );
			}

			rect get_scrollbar_track( const rect& dd ) const
			{
				return rect
				{
					dd.right( ) - k_scrollbar_w - k_scrollbar_pad,
					dd.y + k_scrollbar_pad,
					k_scrollbar_w,
					dd.h - k_scrollbar_pad * 2.0f
				};
			}

			rect get_scrollbar_thumb( const rect& track ) const
			{
				const auto full_h = static_cast< float >( this->m_items.size( ) ) * this->m_item_h + this->m_item_pad * 2.0f;
				const auto thumb_h = std::max( 20.0f, track.h * ( this->k_max_h / full_h ) );
				const auto ms = this->max_scroll( );
				const auto rel = ms > 0.0f ? ( this->m_scroll / ms ) : 0.0f;
				const auto thumb_y = track.y + rel * ( track.h - thumb_h );
				return rect{ track.x, thumb_y, track.w, thumb_h };
			}

			float m_width{};
			std::vector<std::string> m_items{};
			int* m_current_item{};
			float m_item_h{};
			float m_item_pad{};
			std::function<void( )> m_on_change{};
			float m_open_anim{};
			float m_scroll{};
			float m_scroll_target{};
			std::vector<float> m_item_anims{};
			std::vector<float> m_hover_anims{};
			std::vector<float> m_selected_anims{};
			bool m_changed{};
			float m_scrollbar_anim{};
			bool m_scrollbar_dragging{};
			float m_scrollbar_drag_offset{};
		};

	} // the combo namespace

	bool combo( std::string_view label, int& current, const char* const items[ ], int count, float width )
	{
		auto win = layout::current_window( );
		if ( !win || count <= 0 )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& s = c.style;
		const auto& input = c.input;

		const auto is_open = overlays::is_open( id );
		auto changed{ false };

		if ( auto popup = dynamic_cast< combo_overlay* >( overlays::find( id ) ) )
		{
			if ( popup->was_changed( ) )
			{
				changed = true;
			}
		}

		if ( width <= 0.0f )
		{
			width = layout::item_width( );
		}

		const auto [lw, lh] = xdraw::measure_text( display );
		const auto combo_h_val = s.combo_h;
		const auto sp = s.item_spacing_y * 0.25f;
		const auto total_h = lh + sp + combo_h_val;

		const auto abs = layout::item( width, total_h, lh );
		const auto button_y = abs.y + lh + sp;
		const auto button_rect = rect{ abs.x, button_y, width, combo_h_val };

		if ( is_open )
		{
			overlays::touch( id );

			if ( auto popup = dynamic_cast< combo_overlay* >( overlays::find( id ) ) )
			{
				popup->update_anchor( button_rect );
			}
		}

		const auto hovered = input.in_rect( button_rect );

		if ( hovered && input.mouse_clicked && !c.overlay_blocking( ) )
		{
			if ( is_open )
			{
				overlays::close( id );
			}
			else
			{
				std::vector<std::string> items_vec;
				items_vec.reserve( count );

				for ( int i = 0; i < count; ++i )
				{
					items_vec.emplace_back( items[ i ] );
				}

				overlays::add( std::make_unique<combo_overlay>( id, button_rect, width, items_vec, &current, s.combo_item_h, s.window_pad_y * 0.5f ) );
			}
		}

		const auto r = s.combo_rounding;
		const auto hover_anim = anim::lerp( id, ( hovered || is_open ) ? 1.0f : 0.0f, 15.0f );
		const auto bg = lerp( s.combo_bg, s.combo_hovered, hover_anim );
		const auto border = is_open ? lighten( s.combo_border, 1.3f ) : lerp( s.combo_border, lighten( s.combo_border, 1.15f ), hover_anim );

		auto& dl = draw::current( );

		if ( !display.empty( ) )
		{
			const auto label_text = truncate( display, width );
			const auto label_col = lerp( s.text_dim, s.text, hover_anim );
			dl.text( abs.x, abs.y, label_text, label_col );
		}

		dl.rect_filled( button_rect.x, button_rect.y, button_rect.w, button_rect.h, bg, xdraw::corner_radius{ r } );
		dl.rect( button_rect.x, button_rect.y, button_rect.w, button_rect.h, border, xdraw::corner_radius{ r } );

		const auto current_text = ( current >= 0 && current < count ) ? items[ current ] : "";
		const auto [tw, th] = xdraw::measure_text( current_text );
		const auto combo_text_col = lerp( s.text_dim, s.text, hover_anim );
		dl.text( button_rect.x + s.frame_pad_x, button_rect.y + ( combo_h_val - th ) * 0.5f, current_text, combo_text_col );

		constexpr auto arrow_size{ 6.0f };
		constexpr auto arrow_h_val{ 3.5f };

		const auto ax = button_rect.right( ) - arrow_size - s.frame_pad_x - 4.0f;
		const auto ay = button_rect.y + ( combo_h_val - arrow_h_val ) * 0.5f;
		const auto arrow_col = lerp( s.combo_arrow, lighten( s.combo_arrow, 1.3f ), hover_anim );

		if ( is_open )
		{
			const float pts[ ]{ ax, ay + arrow_h_val, ax + arrow_size * 0.5f, ay, ax + arrow_size, ay + arrow_h_val };
			dl.polyline( pts, arrow_col, false, 1.5f );
		}
		else
		{
			const float pts[ ]{ ax, ay, ax + arrow_size * 0.5f, ay + arrow_h_val, ax + arrow_size, ay };
			dl.polyline( pts, arrow_col, false, 1.5f );
		}

		return changed;
	}

	namespace {

		class multicombo_overlay : public overlay
		{
		public:
			multicombo_overlay( std::uintptr_t id, const rect& anchor, float width, const char* const* items, int count, bool* selected, float item_h, float item_pad ) : overlay{ id, anchor }, m_width{ width }, m_count{ count }, m_selected{ selected }, m_item_h{ item_h }, m_item_pad{ item_pad }
			{
				this->m_items.reserve( count );

				for ( int i = 0; i < count; ++i )
				{
					this->m_items.emplace_back( items[ i ] );
				}

				this->m_hover_anims.resize( count, 0.0f );
				this->m_check_anims.resize( count, 0.0f );
				this->m_item_anims.resize( count, 0.0f );

				for ( int i = 0; i < count; ++i )
				{
					this->m_check_anims[ i ] = selected[ i ] ? 1.0f : 0.0f;
				}
			}

			bool hit_test( float x, float y ) const override
			{
				return this->get_dropdown( ).contains( x, y ) || this->m_anchor.contains( x, y );
			}

			bool process_input( const input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto dd = this->get_dropdown( );

				if ( input.mouse_clicked && !dd.contains( input.mouse_x, input.mouse_y ) && !this->m_anchor.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return true;
				}

				if ( dd.contains( input.mouse_x, input.mouse_y ) && input.scroll_delta != 0.0f )
				{
					this->m_scroll -= input.scroll_delta * 30.0f;
					this->m_scroll = std::clamp( this->m_scroll, 0.0f, max_scroll( ) );
					return true;
				}

				if ( input.mouse_clicked && dd.contains( input.mouse_x, input.mouse_y ) )
				{
					for ( int i = 0; i < this->m_count; ++i )
					{
						const auto iy = dd.y + this->m_item_pad + i * this->m_item_h - this->m_scroll;
						const auto ir = rect{ dd.x + this->m_item_pad, iy, dd.w - this->m_item_pad * 2.0f, this->m_item_h };

						if ( ir.contains( input.mouse_x, input.mouse_y ) && this->m_selected )
						{
							this->m_selected[ i ] = !this->m_selected[ i ];
							this->m_changed = true;
							this->m_display_dirty = true;
							return true;
						}
					}
				}

				return dd.contains( input.mouse_x, input.mouse_y );
			}

			void render( const style& style, const input_state& input ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto dd = get_dropdown( );
				auto& dl = xdraw::get( xdraw::layer::top );

				const auto speed = this->m_closing ? 16.0f : 14.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto ease_t = ease::out_cubic( this->m_open_anim );
				const auto animated_h = dd.h * ease_t;
				const auto alpha_mult = ease_t;

				auto bg = style.combo_popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = lighten( style.combo_popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				const auto pr = style.combo_popup_rounding;

				if ( animated_h > 1.0f )
				{
					dl.rect_filled_blurred( dd.x, dd.y, dd.w, animated_h, xdraw::corner_radius{ pr }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 210.0f * alpha_mult ) } );
				}
				dl.rect_filled( dd.x, dd.y, dd.w, animated_h, bg, xdraw::corner_radius{ pr } );
				dl.rect( dd.x, dd.y, dd.w, animated_h, border, xdraw::corner_radius{ pr } );

				dl.push_clip( dd.x - 2.0f, dd.y, dd.w + 4.0f, animated_h );

				constexpr auto max_delay{ 0.5f };
				const auto count_f = static_cast< float >( this->m_count );

				for ( int i = 0; i < this->m_count; ++i )
				{
					const auto iy = dd.y + this->m_item_pad + i * this->m_item_h - this->m_scroll;
					if ( iy + this->m_item_h <= dd.y || iy >= dd.y + animated_h )
					{
						continue;
					}

					const auto ir = rect{ dd.x + this->m_item_pad, iy, dd.w - this->m_item_pad * 2.0f, this->m_item_h };

					const auto item_delay = ( count_f > 1.0f ) ? ( max_delay * static_cast< float >( i ) / ( count_f - 1.0f ) ) : 0.0f;
					const auto item_progress = std::clamp( ( this->m_open_anim - item_delay ) / ( 1.0f - max_delay ), 0.0f, 1.0f );
					auto& ia = this->m_item_anims[ i ];

					ia = std::min( ia + 18.0f * dt, item_progress );

					const auto item_ease = ease::out_cubic( ia );
					const auto item_alpha = item_ease * alpha_mult;
					const auto slide = ( 1.0f - item_ease ) * 8.0f;

					const auto is_selected = this->m_selected && this->m_selected[ i ];
					const auto is_hovered = ir.contains( input.mouse_x, input.mouse_y ) && !m_closing;

					auto& ca = this->m_check_anims[ i ];
					ca += ( ( is_selected ? 1.0f : 0.0f ) - ca ) * std::min( 12.0f * dt, 1.0f );
					const auto ce = ease::smoothstep( ca );

					auto& ha = this->m_hover_anims[ i ];
					ha += ( ( is_hovered ? 1.0f : 0.0f ) - ha ) * std::min( 18.0f * dt, 1.0f );
					const auto he = ease::out_quad( ha );

					const auto is_first = ( i == 0 );
					const auto is_last = ( i == this->m_count - 1 );
					const auto ir_tl = is_first ? pr : 0.0f;
					const auto ir_tr = is_first ? pr : 0.0f;
					const auto ir_bl = is_last ? pr : 0.0f;
					const auto ir_br = is_last ? pr : 0.0f;

					if ( he > 0.01f )
					{
						auto hov = style.combo_popup_item_hovered;
						hov.a = static_cast< std::uint8_t >( hov.a * item_alpha * he );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, hov, xdraw::corner_radius{ ir_tl, ir_tr, ir_br, ir_bl } );
					}

					constexpr auto check_size{ 12.0f };
					const auto cx = ir.x + 4.0f;
					const auto cy = ir.y + ( this->m_item_h - check_size ) * 0.5f + slide;

					draw_minimal_checkbox( dl, cx, cy, check_size, ce, style, item_alpha );

					auto text_col = style.text;
					if ( ce > 0.5f )
					{
						text_col = lerp( text_col, style.checkbox_mark, ( ce - 0.5f ) * 0.6f );
					}

					text_col = lerp( text_col, lighten( text_col, 1.2f ), he );
					text_col.a = static_cast< std::uint8_t >( text_col.a * item_alpha );

					const auto [tw, th] = xdraw::measure_text( this->m_items[ i ] );
					dl.text( ir.x + check_size + 10.0f, ir.y + ( this->m_item_h - th ) * 0.5f + slide, this->m_items[ i ], text_col );
				}

				const auto ms = this->max_scroll( );
				if ( ms > 0.0f )
				{
					constexpr auto fade_h{ 14.0f };
					const auto fcr = std::min( pr, fade_h );

					auto fade_solid = style.combo_popup_bg;
					fade_solid.a = static_cast< std::uint8_t >( fade_solid.a * alpha_mult );
					auto fade_clear = fade_solid;
					fade_clear.a = 0;

					const auto top_t = std::clamp( this->m_scroll / fade_h, 0.0f, 1.0f );
					if ( top_t > 0.01f && animated_h >= fade_h )
					{
						auto top = fade_solid;
						top.a = static_cast< std::uint8_t >( fade_solid.a * top_t );
						dl.rect_filled_gradient( dd.x, dd.y, dd.w, fade_h, top, top, fade_clear, fade_clear, xdraw::corner_radius{ fcr, fcr, 0.0f, 0.0f } );
					}

					const auto remaining = ms - this->m_scroll;
					const auto bot_t = std::clamp( remaining / fade_h, 0.0f, 1.0f );
					if ( bot_t > 0.01f && animated_h >= fade_h )
					{
						auto bot = fade_solid;
						bot.a = static_cast< std::uint8_t >( fade_solid.a * bot_t );
						dl.rect_filled_gradient( dd.x, dd.y + animated_h - fade_h, dd.w, fade_h, fade_clear, fade_clear, bot, bot, xdraw::corner_radius{ 0.0f, 0.0f, fcr, fcr } );
					}
				}

				dl.pop_clip( );
			}

			[[nodiscard]] bool was_changed( ) const { return this->m_changed; }

			[[nodiscard]] const std::string& get_display_text( ) const
			{
				if ( this->m_display_dirty )
				{
					this->m_cached_display.clear( );

					for ( int i = 0; i < this->m_count; ++i )
					{
						if ( this->m_selected && this->m_selected[ i ] )
						{
							if ( !this->m_cached_display.empty( ) )
							{
								this->m_cached_display += ", ";
							}

							this->m_cached_display += m_items[ i ];
						}
					}

					if ( this->m_cached_display.empty( ) )
					{
						this->m_cached_display = "none";
					}

					this->m_display_dirty = false;
				}

				return this->m_cached_display;
			}

		private:
			static constexpr auto k_max_h{ 255.0f };

			rect get_dropdown( ) const
			{
				const auto full_h = static_cast< float >( this->m_count ) * this->m_item_h + this->m_item_pad * 2.0f;
				return { this->m_anchor.x, this->m_anchor.bottom( ) + 4.0f, this->m_width, std::min( full_h, this->k_max_h ) };
			}

			float max_scroll( ) const
			{
				const auto full_h = static_cast< float >( this->m_count ) * this->m_item_h + this->m_item_pad * 2.0f;
				return std::max( 0.0f, full_h - this->k_max_h );
			}

			float m_width{};
			std::vector<std::string> m_items{};
			int m_count{};
			bool* m_selected{};
			float m_item_h{};
			float m_item_pad{};
			float m_open_anim{};
			float m_scroll{};
			std::vector<float> m_item_anims{};
			std::vector<float> m_check_anims{};
			std::vector<float> m_hover_anims{};
			bool m_changed{};
			mutable std::string m_cached_display{};
			mutable bool m_display_dirty{ true };
		};

	} // the multicombo namespace

	bool multicombo( std::string_view label, bool* selected, const char* const items[ ], int count, float width )
	{
		auto win = layout::current_window( );
		if ( !win || count <= 0 || !selected )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& s = c.style;
		const auto& input = c.input;

		const auto is_open = overlays::is_open( id );
		auto changed{ false };

		if ( auto popup = dynamic_cast< multicombo_overlay* >( overlays::find( id ) ) )
		{
			if ( popup->was_changed( ) )
			{
				changed = true;
			}
		}

		if ( width <= 0.0f )
		{
			width = layout::item_width( );
		}

		const auto [lw, lh] = xdraw::measure_text( display );
		const auto combo_h_val = s.combo_h;
		const auto sp = s.item_spacing_y * 0.25f;
		const auto total_h = lh + sp + combo_h_val;

		const auto abs = layout::item( width, total_h, lh );
		const auto button_y = abs.y + lh + sp;
		const auto button_rect = rect{ abs.x, button_y, width, combo_h_val };

		if ( is_open )
		{
			overlays::touch( id );

			if ( auto popup = dynamic_cast< multicombo_overlay* >( overlays::find( id ) ) )
			{
				popup->update_anchor( button_rect );
			}
		}

		const auto hovered = input.in_rect( button_rect );

		if ( hovered && input.mouse_clicked && !c.overlay_blocking( ) )
		{
			if ( is_open )
			{
				overlays::close( id );
			}
			else
			{
				overlays::add( std::make_unique<multicombo_overlay>( id, button_rect, width, items, count, selected, s.combo_item_h, s.window_pad_y * 0.5f ) );
			}
		}

		const auto r = s.combo_rounding;
		const auto hover_anim = anim::lerp( id, ( hovered || is_open ) ? 1.0f : 0.0f, 15.0f );
		const auto bg = lerp( s.combo_bg, s.combo_hovered, hover_anim );
		const auto border = is_open ? lighten( s.combo_border, 1.3f ) : lerp( s.combo_border, lighten( s.combo_border, 1.15f ), hover_anim );
		const auto mc_text_col = lerp( s.text_dim, s.text, hover_anim );

		auto& dl = draw::current( );

		if ( !display.empty( ) )
		{
			dl.text( abs.x, abs.y, truncate( display, width ), mc_text_col );
		}

		dl.rect_filled( button_rect.x, button_rect.y, button_rect.w, button_rect.h, bg, xdraw::corner_radius{ r } );
		dl.rect( button_rect.x, button_rect.y, button_rect.w, button_rect.h, border, xdraw::corner_radius{ r } );

		std::string_view display_text{ "none" };
		std::string local_display;

		if ( auto popup = dynamic_cast< multicombo_overlay* >( overlays::find( id ) ) )
		{
			display_text = popup->get_display_text( );
		}
		else
		{
			for ( int i = 0; i < count; ++i )
			{
				if ( selected[ i ] )
				{
					if ( !local_display.empty( ) )
					{
						local_display += ", ";
					}

					local_display += items[ i ];
				}
			}

			display_text = local_display.empty( ) ? std::string_view{ "none" } : std::string_view{ local_display };
		}

		constexpr auto arrow_size{ 6.0f };
		const auto arrow_pad = s.frame_pad_x + 4.0f + arrow_size + 8.0f;
		const auto max_text_w = width - s.frame_pad_x - arrow_pad;
		const auto text_trunc = truncate( display_text, max_text_w );
		const auto [dtw, dth] = xdraw::measure_text( text_trunc );

		dl.text( button_rect.x + s.frame_pad_x, button_rect.y + ( combo_h_val - dth ) * 0.5f, text_trunc, mc_text_col );

		constexpr auto arrow_h_val{ 3.5f };
		const auto ax = button_rect.right( ) - arrow_size - s.frame_pad_x - 4.0f;
		const auto ay = button_rect.y + ( combo_h_val - arrow_h_val ) * 0.5f;
		const auto arrow_col = lerp( s.combo_arrow, lighten( s.combo_arrow, 1.3f ), hover_anim );

		if ( is_open )
		{
			const float pts[ ]{ ax, ay + arrow_h_val, ax + arrow_size * 0.5f, ay, ax + arrow_size, ay + arrow_h_val };
			dl.polyline( pts, arrow_col, false, 1.5f );
		}
		else
		{
			const float pts[ ]{ ax, ay, ax + arrow_size * 0.5f, ay + arrow_h_val, ax + arrow_size, ay };
			dl.polyline( pts, arrow_col, false, 1.5f );
		}

		return changed;
	}

	namespace {

		class color_picker_overlay : public overlay
		{
		public:
			color_picker_overlay( std::uintptr_t id, const rect& anchor, xdraw::color* col, bool show_alpha ) : overlay{ id, anchor }, m_col{ col }, m_show_alpha{ show_alpha }
			{
				if ( col )
				{
					const auto h = rgb_to_hsv( *col );
					this->m_hue = h.h / 360.0f;
					this->m_sat = h.s;
					this->m_val = h.v;
				}
			}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y ) || this->m_anchor.contains( x, y );
			}

			bool process_input( const input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto popup = this->get_popup( );

				if ( input.mouse_clicked && !popup.contains( input.mouse_x, input.mouse_y ) && !this->m_anchor.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return true;
				}

				const auto pad{ 8.0f };
				const auto bar_w{ 14.0f };
				const auto bar_spacing{ 8.0f };
				const auto hue_bar_h{ 14.0f };
				const auto sv_size = this->m_show_alpha ? ( popup.w - pad * 3.0f - bar_w ) : ( popup.w - pad * 2.0f );

				const auto sv = rect{ popup.x + pad, popup.y + pad, sv_size, sv_size };
				const auto hue = rect{ popup.x + pad, sv.bottom( ) + bar_spacing, sv_size, hue_bar_h };
				const auto alpha_r = rect{ sv.right( ) + bar_spacing, popup.y + pad, bar_w, sv_size + bar_spacing + hue_bar_h };

				if ( input.mouse_clicked )
				{
					if ( sv.contains( input.mouse_x, input.mouse_y ) )
					{
						this->m_active = 1;
					}
					else if ( hue.contains( input.mouse_x, input.mouse_y ) )
					{
						this->m_active = 2;
					}
					else if ( this->m_show_alpha && alpha_r.contains( input.mouse_x, input.mouse_y ) )
					{
						this->m_active = 3;
					}
				}

				if ( input.mouse_released )
				{
					this->m_active = 0;
				}

				if ( input.mouse_down && this->m_active != 0 )
				{
					if ( this->m_active == 1 )
					{
						this->m_sat = std::clamp( ( input.mouse_x - sv.x ) / sv.w, 0.0f, 1.0f );
						this->m_val = 1.0f - std::clamp( ( input.mouse_y - sv.y ) / sv.h, 0.0f, 1.0f );
					}
					else if ( this->m_active == 2 )
					{
						this->m_hue = std::clamp( ( input.mouse_x - hue.x ) / hue.w, 0.0f, 1.0f );
					}
					else if ( this->m_active == 3 && this->m_col )
					{
						this->m_col->a = static_cast< std::uint8_t >( ( 1.0f - std::clamp( ( input.mouse_y - alpha_r.y ) / alpha_r.h, 0.0f, 1.0f ) ) * 255.0f );
					}

					if ( this->m_col && this->m_active != 3 )
					{
						const auto nc = hsv_to_rgb( this->m_hue * 360.0f, this->m_sat, this->m_val );
						this->m_col->r = nc.r;
						this->m_col->g = nc.g;
						this->m_col->b = nc.b;
					}

					this->m_changed = true;
				}

				return popup.contains( input.mouse_x, input.mouse_y );
			}

			void render( const style& style, const input_state& ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto popup = this->get_popup( );
				auto& dl = xdraw::get( xdraw::layer::top );

				const auto speed = this->m_closing ? 18.0f : 16.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto ease_t = ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = ease_t;
				const auto scale = 0.95f + ease_t * 0.05f;

				const auto sw = popup.w * scale;
				const auto sh = popup.h * scale;
				const auto sx = popup.x + ( popup.w - sw ) * 0.5f;
				const auto sy = popup.y + ( popup.h - sh ) * 0.5f;

				const auto pr = style.picker_popup_rounding;
				auto tint = style.picker_popup_bg;
				tint.a = static_cast< std::uint8_t >( tint.a * alpha_mult );
				auto border = style.picker_popup_border;
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				dl.rect_filled( sx, sy, sw, sh, tint, xdraw::corner_radius{ pr } );
				dl.rect( sx, sy, sw, sh, border, xdraw::corner_radius{ pr } );

				if ( ease_t < 0.15f )
				{
					return;
				}

				const auto ca = std::clamp( ( ease_t - 0.15f ) / 0.85f, 0.0f, 1.0f );
				const auto a = static_cast< std::uint8_t >( 255 * ca );

				dl.push_clip( sx, sy, sw, sh );

				const auto pad{ 8.0f };
				const auto bar_w{ 14.0f };
				const auto bar_spacing{ 8.0f };
				const auto cr{ 4.0f };

				const auto has_alpha = this->m_show_alpha;
				const auto sv_size = has_alpha ? ( sw - pad * 3.0f - bar_w ) : ( sw - pad * 2.0f );
				const auto hue_bar_h{ 14.0f };

				const auto sv_rect = rect{ sx + pad, sy + pad, sv_size, sv_size };
				const auto hue_rect = rect{ sx + pad, sv_rect.bottom( ) + bar_spacing, sv_size, hue_bar_h };
				const auto alpha_rect = rect{ sv_rect.right( ) + bar_spacing, sy + pad, bar_w, sv_size + bar_spacing + hue_bar_h };

				const auto sv_corners = xdraw::corner_radius
				{
					cr,
					has_alpha ? 0.0f : cr,
					0.0f,
					0.0f
				};

				const auto hue_corners = xdraw::corner_radius
				{
					0.0f,
					0.0f,
					has_alpha ? 0.0f : cr,
					cr
				};

				const auto alpha_corners = xdraw::corner_radius
				{
					0.0f,
					cr,
					cr,
					0.0f
				};

				{
					const auto hue_col = hsv_to_rgb( this->m_hue * 360.0f, 1.0f, 1.0f );
					const auto hue_a = xdraw::color{ hue_col.r, hue_col.g, hue_col.b, a };
					const auto white_a = xdraw::color{ 255, 255, 255, a };
					const auto black_a = xdraw::color{ 0, 0, 0, a };
					const auto clear = xdraw::color{ 0, 0, 0, 0 };

					dl.rect_filled_gradient( sv_rect.x, sv_rect.y, sv_rect.w, sv_rect.h, white_a, hue_a, hue_a, white_a, sv_corners );
					dl.rect_filled_gradient( sv_rect.x, sv_rect.y, sv_rect.w, sv_rect.h, clear, clear, black_a, black_a, sv_corners );
				}

				{
					constexpr auto hue_segments{ 6 };
					const auto seg_w = hue_rect.w / static_cast< float >( hue_segments );

					for ( int i = 0; i < hue_segments; ++i )
					{
						const auto h0 = static_cast< float >( i ) / static_cast< float >( hue_segments ) * 360.0f;
						const auto h1 = static_cast< float >( i + 1 ) / static_cast< float >( hue_segments ) * 360.0f;

						auto c0 = hsv_to_rgb( h0, 1.0f, 1.0f );
						auto c1 = hsv_to_rgb( h1, 1.0f, 1.0f );
						c0.a = a;
						c1.a = a;

						const auto seg_x = hue_rect.x + seg_w * static_cast< float >( i );
						const auto is_first = ( i == 0 );
						const auto is_last = ( i == hue_segments - 1 );

						const auto seg_r = xdraw::corner_radius
						{
							0.0f,
							0.0f,
							is_last ? hue_corners.br : 0.0f,
							is_first ? hue_corners.bl : 0.0f
						};

						dl.rect_filled_gradient( seg_x, hue_rect.y, seg_w + ( is_last ? 0.0f : 1.0f ), hue_rect.h, c0, c1, c1, c0, seg_r );
					}
				}

				if ( has_alpha && this->m_col )
				{
					auto top_col = xdraw::color{ this->m_col->r, this->m_col->g, this->m_col->b, a };
					auto bot_col = xdraw::color{ this->m_col->r, this->m_col->g, this->m_col->b, 0 };

					dl.rect_filled_gradient( alpha_rect.x, alpha_rect.y, alpha_rect.w, alpha_rect.h, top_col, top_col, bot_col, bot_col, alpha_corners );
				}

				auto white = xdraw::color{ 255, 255, 255, a };
				auto dark = xdraw::color{ 0, 0, 0, static_cast< std::uint8_t >( 120 * ca ) };

				{
					const auto sv_cx = sv_rect.x + this->m_sat * sv_rect.w;
					const auto sv_cy = sv_rect.y + ( 1.0f - this->m_val ) * sv_rect.h;
					const auto selected = hsv_to_rgb( this->m_hue * 360.0f, this->m_sat, this->m_val );
					const auto fill = xdraw::color{ selected.r, selected.g, selected.b, a };

					dl.circle_filled( sv_cx, sv_cy, 6.0f, fill );
					dl.circle( sv_cx, sv_cy, 6.0f, white, 2.0f );
					dl.circle( sv_cx, sv_cy, 7.5f, dark, 1.0f );
				}

				{
					const auto hue_cx = hue_rect.x + this->m_hue * hue_rect.w;
					const auto pill_w = 4.0f;
					const auto pill_h = hue_rect.h + 4.0f;
					const auto pill_r = pill_w * 0.5f;
					const auto pill_x = hue_cx - pill_w * 0.5f;
					const auto pill_y = hue_rect.y - 2.0f;

					dl.rect_filled( pill_x, pill_y, pill_w, pill_h, white, xdraw::corner_radius{ pill_r } );
					dl.rect( pill_x, pill_y, pill_w, pill_h, dark, xdraw::corner_radius{ pill_r } );
				}

				if ( has_alpha && this->m_col )
				{
					const auto alpha_cy = alpha_rect.y + ( 1.0f - this->m_col->a / 255.0f ) * alpha_rect.h;
					const auto pill_w = alpha_rect.w + 4.0f;
					const auto pill_h = 4.0f;
					const auto pill_r = pill_h * 0.5f;
					const auto pill_x = alpha_rect.x - 2.0f;
					const auto pill_y = alpha_cy - pill_h * 0.5f;

					dl.rect_filled( pill_x, pill_y, pill_w, pill_h, white, xdraw::corner_radius{ pill_r } );
					dl.rect( pill_x, pill_y, pill_w, pill_h, dark, xdraw::corner_radius{ pill_r } );
				}

				dl.pop_clip( );
			}

			[[nodiscard]] bool was_changed( ) const { return this->m_changed; }
			void clear_changed( ) { this->m_changed = false; }

		private:
			[[nodiscard]] rect get_popup( ) const
			{
				const auto pad{ 8.0f };
				const auto bar_w{ 14.0f };
				const auto bar_spacing{ 8.0f };
				const auto hue_bar_h{ 14.0f };
				const auto sv_size{ 170.0f };
				const auto w = this->m_show_alpha ? ( pad + sv_size + bar_spacing + bar_w + pad ) : ( pad + sv_size + pad );
				const auto h = pad + sv_size + bar_spacing + hue_bar_h + pad;
				return { this->m_anchor.x, this->m_anchor.bottom( ) + 2.0f, w, h };
			}

			xdraw::color* m_col{};
			float m_hue{};
			float m_sat{};
			float m_val{};
			int m_active{};
			bool m_show_alpha{};
			float m_open_anim{};
			bool m_changed{};
		};

		class color_context_overlay : public overlay
		{
		public:
			color_context_overlay( std::uintptr_t id, const rect& anchor, xdraw::color* col, bool show_alpha ) : overlay{ id, anchor }, m_col{ col }, m_show_alpha{ show_alpha }
			{
				this->m_hover_anims.fill( 0.0f );
				this->m_item_anims.fill( 0.0f );

				const auto clip = clipboard_paste( get_hwnd( ) );
				const auto parsed = hex_to_color( clip );
				this->m_can_paste = parsed.has_value( );

				if ( this->m_can_paste )
				{
					this->m_paste_color = *parsed;
					this->m_paste_hex = clip;
				}
			}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y );
			}

			bool process_input( const input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto popup = this->get_popup( );

				if ( ( input.mouse_clicked || input.rmb_clicked ) && !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return true;
				}

				if ( input.mouse_clicked && popup.contains( input.mouse_x, input.mouse_y ) )
				{
					for ( int i = 0; i < k_item_count; ++i )
					{
						if ( i == 1 && !this->m_can_paste )
						{
							continue;
						}

						const auto ir = this->get_item_rect( popup, i );

						if ( ir.contains( input.mouse_x, input.mouse_y ) )
						{
							if ( i == 0 && this->m_col )
							{
								const auto hex = color_to_hex( *this->m_col, this->m_show_alpha );
								clipboard_copy( get_hwnd( ), hex );
							}
							else if ( i == 1 && this->m_col )
							{
								*this->m_col = this->m_paste_color;
								this->m_changed = true;
							}

							this->m_closing = true;
							return true;
						}
					}
				}

				return popup.contains( input.mouse_x, input.mouse_y );
			}

			void render( const style& style, const input_state& input ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto popup = this->get_popup( );
				auto& dl = xdraw::get( xdraw::layer::top );

				const auto speed = this->m_closing ? 18.0f : 16.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto ease_t = ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = ease_t;
				const auto animated_h = popup.h * ease_t;
				const auto pr = style.popup_rounding;

				auto bg = style.popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = lighten( style.popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				dl.rect_filled( popup.x, popup.y, popup.w, animated_h, bg, xdraw::corner_radius{ pr } );

				if ( border.a > 0 )
				{
					dl.rect( popup.x, popup.y, popup.w, animated_h, border, xdraw::corner_radius{ pr } );
				}

				dl.push_clip( popup.x, popup.y, popup.w, animated_h );

				static constexpr const char* labels[ k_item_count ]{ "copy", "paste" };

				for ( int i = 0; i < k_item_count; ++i )
				{
					const auto disabled = ( i == 1 && !this->m_can_paste );
					const auto ir = this->get_item_rect( popup, i );

					const auto item_delay = i * 0.06f;
					const auto item_progress = std::clamp( this->m_open_anim - item_delay, 0.0f, 1.0f ) / ( 1.0f - std::min( item_delay, 0.3f ) );
					auto& ia = this->m_item_anims[ i ];
					ia = std::min( ia + 20.0f * dt, item_progress );

					const auto item_ease = ease::out_cubic( ia );
					const auto item_alpha = item_ease * alpha_mult;
					const auto slide = ( 1.0f - item_ease ) * 6.0f;

					const auto is_hovered = !disabled && !this->m_closing && ir.contains( input.mouse_x, input.mouse_y );
					auto& ha = this->m_hover_anims[ i ];
					ha += ( ( is_hovered ? 1.0f : 0.0f ) - ha ) * std::min( 18.0f * dt, 1.0f );

					const auto is_first = ( i == 0 );
					const auto is_last = ( i == k_item_count - 1 );

					if ( ha > 0.01f )
					{
						auto hov = style.combo_popup_item_hovered;
						hov.a = static_cast< std::uint8_t >( hov.a * item_alpha * ha );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, hov, xdraw::corner_radius{ is_first ? pr : 0.0f, is_first ? pr : 0.0f, is_last ? pr : 0.0f, is_last ? pr : 0.0f } );
					}

					const auto [tw, th] = xdraw::measure_text( labels[ i ] );
					auto text_col = disabled ? style.text_dim : style.text;
					text_col = lerp( text_col, lighten( text_col, 1.3f ), ha );
					text_col.a = static_cast< std::uint8_t >( text_col.a * item_alpha );

					dl.text( ir.x + 8.0f, ir.y + ( k_item_h - th ) * 0.5f + slide, labels[ i ], text_col );

					if ( i == 0 && this->m_col )
					{
						const auto hex = color_to_hex( *this->m_col, this->m_show_alpha );
						const auto [hw, hh] = xdraw::measure_text( hex );
						auto hex_col = style.text_dim;
						hex_col.a = static_cast< std::uint8_t >( hex_col.a * item_alpha );
						dl.text( ir.right( ) - hw - 8.0f, ir.y + ( k_item_h - hh ) * 0.5f + slide, hex, hex_col );
					}
					else if ( i == 1 && this->m_can_paste )
					{
						const auto swatch_size = k_item_h - 8.0f;
						const auto sx = ir.right( ) - swatch_size - 8.0f;
						const auto sy = ir.y + ( k_item_h - swatch_size ) * 0.5f + slide;

						auto swatch_col = this->m_paste_color;
						swatch_col.a = static_cast< std::uint8_t >( std::min( 255.0f, swatch_col.a * item_alpha ) );
						dl.rect_filled( sx, sy, swatch_size, swatch_size, swatch_col, xdraw::corner_radius{ 3.0f } );

						const auto [hw, hh] = xdraw::measure_text( this->m_paste_hex );
						auto hex_col = style.text_dim;
						hex_col.a = static_cast< std::uint8_t >( hex_col.a * item_alpha );
						dl.text( sx - hw - 4.0f, ir.y + ( k_item_h - hh ) * 0.5f + slide, this->m_paste_hex, hex_col );
					}
				}

				dl.pop_clip( );
			}

			[[nodiscard]] bool was_changed( ) const { return this->m_changed; }

		private:
			static constexpr auto k_item_count{ 2 };
			static constexpr auto k_item_h{ 24.0f };
			static constexpr auto k_pad{ 4.0f };
			static constexpr auto k_popup_w{ 160.0f };

			[[nodiscard]] rect get_popup( ) const
			{
				const auto h = k_pad * 2.0f + k_item_h * static_cast< float >( k_item_count );
				return { this->m_anchor.x, this->m_anchor.y, k_popup_w, h };
			}

			[[nodiscard]] rect get_item_rect( const rect& popup, int index ) const
			{
				return { popup.x + k_pad, popup.y + k_pad + static_cast< float >( index ) * k_item_h, popup.w - k_pad * 2.0f, k_item_h };
			}

			xdraw::color* m_col{};
			bool m_show_alpha{};
			float m_open_anim{};
			bool m_changed{};
			bool m_can_paste{};
			xdraw::color m_paste_color{};
			std::string m_paste_hex{};
			std::array<float, k_item_count> m_hover_anims{};
			std::array<float, k_item_count> m_item_anims{};
		};

	} // the color_picker namespace

	bool color_picker( std::string_view label, xdraw::color& col, float width, bool show_alpha )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto [display, full] = parse_label( label );
		const auto& s = c.style;
		const auto& input = c.input;

		const auto is_open = overlays::is_open( id );
		auto changed{ false };

		if ( auto popup = dynamic_cast< color_picker_overlay* >( overlays::find( id ) ) )
		{
			if ( popup->was_changed( ) )
			{
				changed = true;
			}

			popup->clear_changed( );
		}

		const auto ctx_id = id + 100;

		if ( auto ctx_popup = dynamic_cast< color_context_overlay* >( overlays::find( ctx_id ) ) )
		{
			if ( ctx_popup->was_changed( ) )
			{
				changed = true;
			}
		}

		const auto sw = s.swatch_w;
		const auto sh = s.swatch_h;
		const auto has_label = !display.empty( );

		auto label_w{ 0.0f };
		auto label_h{ 0.0f };

		if ( has_label )
		{
			auto [w, h] = xdraw::measure_text( display );
			label_w = w;
			label_h = h;
		}

		const auto total_w = has_label ? ( sw + s.item_spacing_x + label_w ) : sw;
		const auto total_h = has_label ? std::max( sh, label_h ) : sh;

		const auto abs = layout::item( total_w, total_h );
		auto x_offset{ 0.0f };

		if ( width > 0.0f )
		{
			x_offset = width - total_w;
		}

		const auto swatch_y = has_label ? ( abs.y + ( total_h - sh ) * 0.5f ) : abs.y;
		const auto swatch_rect = rect{ abs.x + x_offset, swatch_y, sw, sh };

		const auto can_interact = !c.overlay_blocking( ) || is_open;
		const auto hovered = can_interact && input.in_rect( swatch_rect );

		if ( is_open )
		{
			overlays::touch( id );

			if ( auto popup = dynamic_cast< color_picker_overlay* >( overlays::find( id ) ) )
			{
				popup->update_anchor( swatch_rect );
			}
		}

		const auto ctx_is_open = overlays::is_open( ctx_id );
		if ( ctx_is_open )
		{
			overlays::touch( ctx_id );
		}

		if ( can_interact && input.in_rect( swatch_rect ) && input.rmb_clicked )
		{
			if ( ctx_is_open )
			{
				overlays::close( ctx_id );
			}
			else if ( !c.overlay_blocking( ) || ctx_is_open )
			{
				const auto mouse_anchor = rect{ input.mouse_x, input.mouse_y, 0.0f, 0.0f };
				get_overlays( ).list.push_back( std::make_unique<color_context_overlay>( ctx_id, mouse_anchor, &col, show_alpha ) );
			}
		}

		if ( hovered && input.mouse_clicked )
		{
			if ( is_open )
			{
				overlays::close( id );
			}
			else if ( !c.overlay_blocking( ) )
			{
				get_overlays( ).list.push_back( std::make_unique<color_picker_overlay>( id, swatch_rect, &col, show_alpha ) );
			}
		}

		const auto hover_anim = anim::lerp( id + 2, hovered ? 1.0f : 0.0f, 12.0f );
		const auto open_anim = anim::lerp( id + 3, is_open ? 1.0f : 0.0f, 12.0f );
		const auto r = s.color_swatch_rounding;

		auto& dl = draw::current( );

		if ( col.a < 255 )
		{
			constexpr auto checker_size{ 4.0f };
			const auto dark_checker = xdraw::color{ 180, 180, 180, 255 };
			const auto light_checker = xdraw::color{ 220, 220, 220, 255 };

			dl.rect_filled( swatch_rect.x, swatch_rect.y, swatch_rect.w, swatch_rect.h, dark_checker, xdraw::corner_radius{ r } );

			for ( float cy = swatch_rect.y; cy < swatch_rect.bottom( ); cy += checker_size )
			{
				for ( float cx = swatch_rect.x; cx < swatch_rect.right( ); cx += checker_size )
				{
					const auto ix = static_cast< int >( ( cx - swatch_rect.x ) / checker_size );
					const auto iy = static_cast< int >( ( cy - swatch_rect.y ) / checker_size );

					if ( ( ix + iy ) % 2 != 0 )
					{
						const auto cw = std::min( checker_size, swatch_rect.right( ) - cx );
						const auto ch = std::min( checker_size, swatch_rect.bottom( ) - cy );

						const auto touches_tl = ( cx < swatch_rect.x + r && cy < swatch_rect.y + r );
						const auto touches_tr = ( cx + cw > swatch_rect.right( ) - r && cy < swatch_rect.y + r );
						const auto touches_bl = ( cx < swatch_rect.x + r && cy + ch > swatch_rect.bottom( ) - r );
						const auto touches_br = ( cx + cw > swatch_rect.right( ) - r && cy + ch > swatch_rect.bottom( ) - r );

						if ( !touches_tl && !touches_tr && !touches_bl && !touches_br )
						{
							dl.rect_filled( cx, cy, cw, ch, light_checker );
						}
					}
				}
			}
		}

		dl.rect_filled( swatch_rect.x, swatch_rect.y, swatch_rect.w, swatch_rect.h, col, xdraw::corner_radius{ r } );

		if ( has_label )
		{
			const auto lx = swatch_rect.right( ) + s.item_spacing_x;
			const auto ly = abs.y + ( total_h - label_h ) * 0.5f;
			const auto available_w = win->bounds.right( ) - lx - s.window_pad_x;
			const auto label_text = truncate( display, available_w );
			const auto label_col = lerp( s.text_dim, s.text, std::max( hover_anim, open_anim ) );
			dl.text( lx, ly, label_text, label_col );
		}

		return changed;
	}

	namespace {

		struct text_input_state
		{
			std::size_t cursor{};
			std::size_t sel_start{};
			std::size_t sel_end{};
			float blink_timer{};
			float scroll_offset{};
			std::unordered_map<int, float> key_repeat_timers{};
			std::unordered_map<int, bool> key_was_down{};
		};

		std::unordered_map<std::uintptr_t, text_input_state>& get_text_input_states( )
		{
			static std::unordered_map<std::uintptr_t, text_input_state> s{};
			return s;
		}

	} // the text_input namespace

	bool text_input( std::string_view label, std::string& buf, std::size_t max_len, std::string_view hint )
	{
		auto win = layout::current_window( );
		if ( !win )
		{
			return false;
		}

		auto& c = get_ctx( );
		const auto id = make_id( label );
		const auto& s = c.style;
		const auto& input = c.input;
		auto changed = false;

		auto& states = get_text_input_states( );
		auto& st = states[ id ];

		const auto is_active = ( c.active_text_input == id );
		const auto [avail_w, avail_h] = layout::avail( );
		const auto input_h = s.text_input_h;

		const auto abs = layout::item( avail_w, input_h );
		const auto frame = rect{ abs.x, abs.y, avail_w, input_h };

		const auto can_interact = !c.overlay_blocking( );
		const auto hovered = can_interact && input.in_rect( frame );
		const auto clicked = hovered && input.mouse_clicked;

		if ( clicked )
		{
			c.active_text_input = id;
			st.key_was_down.clear( );
			st.key_repeat_timers.clear( );

			const auto text_start_x = frame.x + s.frame_pad_x - st.scroll_offset;
			std::size_t best_pos{ 0 };
			auto best_dist = std::abs( input.mouse_x - text_start_x );

			for ( std::size_t i = 1; i <= buf.size( ); ++i )
			{
				const auto [tw, th] = xdraw::measure_text( std::string_view{ buf.data( ), i } );
				const auto dist = std::abs( input.mouse_x - ( text_start_x + tw ) );

				if ( dist < best_dist )
				{
					best_dist = dist;
					best_pos = i;
				}
			}

			st.cursor = best_pos;
			st.sel_start = best_pos;
			st.sel_end = best_pos;
			st.blink_timer = 0.0f;
		}

		if ( is_active && input.mouse_clicked && !hovered )
		{
			c.active_text_input = null_id;
		}

		if ( is_active && hovered && input.mouse_down )
		{
			const auto text_start_x = frame.x + s.frame_pad_x - st.scroll_offset;
			std::size_t best_pos{ 0 };
			auto best_dist = std::abs( input.mouse_x - text_start_x );

			for ( std::size_t i = 1; i <= buf.size( ); ++i )
			{
				const auto [tw, th] = xdraw::measure_text( std::string_view{ buf.data( ), i } );
				const auto dist = std::abs( input.mouse_x - ( text_start_x + tw ) );

				if ( dist < best_dist )
				{
					best_dist = dist;
					best_pos = i;
				}
			}

			st.sel_end = best_pos;
			st.cursor = best_pos;
			st.blink_timer = 0.0f;
		}

		if ( is_active )
		{
			const auto dt = xdraw::delta_time( );
			const auto shift = input.shift_held( );
			const auto ctrl = input.ctrl_held( );

			const auto has_sel = st.sel_start != st.sel_end;
			const auto sel_min = std::min( st.sel_start, st.sel_end );
			const auto sel_max = std::max( st.sel_start, st.sel_end );

			auto process_key = [ & ]( int vk ) -> bool
				{
					for ( const auto pk : input.key_presses( ) )
					{
						if ( pk == vk )
						{
							return true;
						}
					}

					const auto is_down = input.key_down( vk );
					st.key_was_down[ vk ] = is_down;

					if ( !is_down )
					{
						st.key_repeat_timers[ vk ] = 0.0f;
						return false;
					}

					st.key_repeat_timers[ vk ] += dt;
					constexpr auto delay = 0.4f;
					constexpr auto rate = 0.03f;

					if ( st.key_repeat_timers[ vk ] >= delay )
					{
						const auto excess = st.key_repeat_timers[ vk ] - delay;
						if ( static_cast< int >( excess / rate ) > static_cast< int >( ( excess - dt ) / rate ) )
						{
							return true;
						}
					}

					return false;
				};

			if ( process_key( VK_LEFT ) )
			{
				st.blink_timer = 0.0f;

				if ( ctrl )
				{
					while ( st.cursor > 0 && buf[ st.cursor - 1 ] == ' ' ) { st.cursor--; }
					while ( st.cursor > 0 && buf[ st.cursor - 1 ] != ' ' ) { st.cursor--; }
				}
				else if ( has_sel && !shift )
				{
					st.cursor = sel_min;
				}
				else if ( st.cursor > 0 )
				{
					st.cursor--;
				}

				if ( shift )
				{
					st.sel_end = st.cursor;
				}
				else
				{
					st.sel_start = st.cursor;
					st.sel_end = st.cursor;
				}
			}

			if ( process_key( VK_RIGHT ) )
			{
				st.blink_timer = 0.0f;

				if ( ctrl )
				{
					while ( st.cursor < buf.size( ) && buf[ st.cursor ] != ' ' ) { st.cursor++; }
					while ( st.cursor < buf.size( ) && buf[ st.cursor ] == ' ' ) { st.cursor++; }
				}
				else if ( has_sel && !shift )
				{
					st.cursor = sel_max;
				}
				else if ( st.cursor < buf.size( ) )
				{
					st.cursor++;
				}

				if ( shift )
				{
					st.sel_end = st.cursor;
				}
				else
				{
					st.sel_start = st.cursor;
					st.sel_end = st.cursor;
				}
			}

			if ( process_key( VK_HOME ) )
			{
				st.blink_timer = 0.0f;
				st.cursor = 0;

				if ( shift )
				{
					st.sel_end = 0;
				}
				else
				{
					st.sel_start = 0;
					st.sel_end = 0;
				}
			}

			if ( process_key( VK_END ) )
			{
				st.blink_timer = 0.0f;
				st.cursor = buf.size( );

				if ( shift )
				{
					st.sel_end = buf.size( );
				}
				else
				{
					st.sel_start = buf.size( );
					st.sel_end = buf.size( );
				}
			}

			if ( ctrl && process_key( 'A' ) )
			{
				st.sel_start = 0;
				st.sel_end = buf.size( );
				st.cursor = buf.size( );
			}

			if ( process_key( VK_BACK ) )
			{
				st.blink_timer = 0.0f;

				if ( has_sel )
				{
					buf.erase( sel_min, sel_max - sel_min );
					st.cursor = sel_min;
					changed = true;
				}
				else if ( st.cursor > 0 )
				{
					if ( ctrl )
					{
						auto es = st.cursor;
						while ( es > 0 && buf[ es - 1 ] == ' ' ) { es--; }
						while ( es > 0 && buf[ es - 1 ] != ' ' ) { es--; }
						buf.erase( es, st.cursor - es );
						st.cursor = es;
					}
					else
					{
						buf.erase( st.cursor - 1, 1 );
						st.cursor--;
					}

					changed = true;
				}

				st.sel_start = st.cursor;
				st.sel_end = st.cursor;
			}

			if ( process_key( VK_DELETE ) )
			{
				st.blink_timer = 0.0f;

				if ( has_sel )
				{
					buf.erase( sel_min, sel_max - sel_min );
					st.cursor = sel_min;
					changed = true;
				}
				else if ( st.cursor < buf.size( ) )
				{
					if ( ctrl )
					{
						auto ee = st.cursor;
						while ( ee < buf.size( ) && buf[ ee ] != ' ' ) { ee++; }
						while ( ee < buf.size( ) && buf[ ee ] == ' ' ) { ee++; }
						buf.erase( st.cursor, ee - st.cursor );
					}
					else
					{
						buf.erase( st.cursor, 1 );
					}

					changed = true;
				}

				st.sel_start = st.cursor;
				st.sel_end = st.cursor;
			}

			if ( process_key( VK_ESCAPE ) || process_key( VK_RETURN ) )
			{
				c.active_text_input = null_id;
			}

			for ( const auto wch : input.chars( ) )
			{
				if ( buf.size( ) < max_len )
				{
					st.blink_timer = 0.0f;

					if ( has_sel )
					{
						buf.erase( sel_min, sel_max - sel_min );
						st.cursor = sel_min;
					}

					buf.insert( st.cursor, 1, static_cast< char >( wch ) );
					st.cursor++;
					st.sel_start = st.cursor;
					st.sel_end = st.cursor;
					changed = true;
				}
			}

			st.cursor = std::min( st.cursor, buf.size( ) );
			st.sel_start = std::min( st.sel_start, buf.size( ) );
			st.sel_end = std::min( st.sel_end, buf.size( ) );
		}

		const auto dt = xdraw::delta_time( );
		const auto hover_anim = anim::lerp( id, hovered ? 1.0f : 0.0f, 12.0f );
		const auto active_anim = anim::lerp( id + 1, is_active ? 1.0f : 0.0f, 12.0f );

		if ( is_active )
		{
			st.blink_timer += dt;

			if ( st.blink_timer > 1.0f )
			{
				st.blink_timer -= 1.0f;
			}
		}

		const auto r = s.text_input_rounding;
		auto border = lerp( s.text_input_border, lighten( s.text_input_border, 1.3f ), hover_anim );
		border = lerp( border, s.accent, active_anim );
		auto bg = lerp( s.text_input_bg, lighten( s.text_input_bg, 1.05f ), hover_anim );
		bg = lerp( bg, lighten( s.text_input_bg, 1.08f ), active_anim );

		auto& dl = draw::current( );
		dl.rect_filled( frame.x, frame.y, frame.w, frame.h, bg, xdraw::corner_radius{ r } );
		dl.rect( frame.x, frame.y, frame.w, frame.h, border, xdraw::corner_radius{ r } );

		const auto text_pad = s.frame_pad_x;
		const auto text_area_w = frame.w - text_pad * 2.0f;
		auto [tw, th] = buf.empty( ) ? xdraw::measure_text( "A" ) : xdraw::measure_text( buf );

		if ( buf.empty( ) )
		{
			tw = 0.0f;
		}

		if ( is_active )
		{
			const auto [co, coh] = st.cursor == 0 ? std::pair{ 0.0f, 0.0f } : xdraw::measure_text( std::string_view{ buf.data( ), st.cursor } );

			if ( co < st.scroll_offset )
			{
				st.scroll_offset = co;
			}
			else if ( co > st.scroll_offset + text_area_w - 2.0f )
			{
				st.scroll_offset = co - text_area_w + 2.0f;
			}

			st.scroll_offset = std::max( 0.0f, st.scroll_offset );
		}

		dl.push_clip( frame.x + text_pad, frame.y, text_area_w, frame.h );

		const auto text_x = frame.x + text_pad - st.scroll_offset;
		const auto text_y = frame.y + ( frame.h - th ) * 0.5f;

		if ( is_active && st.sel_start != st.sel_end )
		{
			const auto sm = std::min( st.sel_start, st.sel_end );
			const auto sx_val = std::max( st.sel_start, st.sel_end );
			const auto [sw_start, sh_start] = sm == 0 ? std::pair{ 0.0f, 0.0f } : xdraw::measure_text( std::string_view{ buf.data( ), sm } );
			const auto [sw_end, sh_end] = xdraw::measure_text( std::string_view{ buf.data( ), sx_val } );
			auto sel_col = s.accent;
			sel_col.a = 100;
			dl.rect_filled( text_x + sw_start, text_y - 1.0f, sw_end - sw_start, th + 2.0f, sel_col );
		}

		const auto ti_text = lerp( s.text_dim, s.text, std::max( hover_anim, active_anim ) );

		if ( buf.empty( ) && !is_active && !hint.empty( ) )
		{
			auto hint_col = ti_text;
			hint_col.a = 100;
			dl.text( frame.x + text_pad, text_y, hint, hint_col );
		}
		else if ( !buf.empty( ) )
		{
			dl.text( text_x, text_y, buf, ti_text );
		}

		if ( is_active )
		{
			const auto [co, coh] = st.cursor == 0 ? std::pair{ 0.0f, 0.0f } : xdraw::measure_text( std::string_view{ buf.data( ), st.cursor } );
			const auto cursor_x = text_x + co;
			const auto cursor_h = th;
			const auto ba = std::sin( st.blink_timer * 6.28318f ) * 0.5f + 0.5f;
			auto cursor_col = s.text;
			cursor_col.a = static_cast< std::uint8_t >( 255.0f * ( 0.4f + 0.6f * ba ) );
			dl.rect_filled( cursor_x, text_y, 1.0f, cursor_h, cursor_col );
		}

		dl.pop_clip( );

		return changed;
	}

} // namespace xui
