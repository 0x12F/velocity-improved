#pragma once

#include "../xdraw.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xui {

	inline constexpr std::uintptr_t null_id{ 0 };

	struct rect
	{
		float x{}, y{}, w{}, h{};

		inline constexpr bool rect::contains( float px, float py ) const noexcept
		{
			return px >= x && px <= x + w && py >= y && py <= y + h;
		} 
		
		[[nodiscard]] constexpr float right( ) const noexcept { return x + w; }
		[[nodiscard]] constexpr float bottom( ) const noexcept { return y + h; }
		[[nodiscard]] constexpr float center_x( ) const noexcept { return x + w * 0.5f; }
		[[nodiscard]] constexpr float center_y( ) const noexcept { return y + h * 0.5f; }
		[[nodiscard]] constexpr rect offset( float dx, float dy ) const noexcept;
		[[nodiscard]] constexpr rect expand( float amount ) const noexcept;
		[[nodiscard]] constexpr rect shrink( float amount ) const noexcept;
		[[nodiscard]] constexpr rect intersect( const rect& o ) const noexcept;
	};

	struct hsv
	{
		float h{}, s{}, v{}, a{ 1.0f };
	};

	inline constexpr std::uintptr_t fnv1a( const char* data, std::size_t len ) noexcept
	{
		auto h = std::uintptr_t{ 14695981039346656037ull };

		for ( std::size_t i = 0; i < len; ++i )
		{
			h ^= static_cast< std::uintptr_t >( static_cast< unsigned char >( data[ i ] ) );
			h *= 1099511628211ull;
		}

		return h;
	}


	inline constexpr std::uintptr_t fnv1a( std::string_view sv ) noexcept
	{
		return fnv1a( sv.data( ), sv.size( ) );
	}
	
	[[nodiscard]] std::pair<std::string_view, std::string_view> parse_label( std::string_view label ) noexcept;

	[[nodiscard]] xdraw::color lighten( xdraw::color c, float factor ) noexcept;
	[[nodiscard]] xdraw::color darken( xdraw::color c, float factor ) noexcept;
	[[nodiscard]] xdraw::color alpha_mod( xdraw::color c, float a ) noexcept;
	[[nodiscard]] xdraw::color lerp( xdraw::color a, xdraw::color b, float t ) noexcept;

	[[nodiscard]] hsv rgb_to_hsv( xdraw::color c ) noexcept;
	[[nodiscard]] xdraw::color hsv_to_rgb( const hsv& c ) noexcept;
	[[nodiscard]] xdraw::color hsv_to_rgb( float h, float s, float v, float a = 1.0f ) noexcept;
	[[nodiscard]] std::string color_to_hex( xdraw::color c, bool include_alpha = true ) noexcept;
	[[nodiscard]] std::optional<xdraw::color> hex_to_color( std::string_view hex ) noexcept;

	namespace ease {

		[[nodiscard]] float linear( float t ) noexcept;
		[[nodiscard]] float in_quad( float t ) noexcept;
		[[nodiscard]] float out_quad( float t ) noexcept;
		[[nodiscard]] float in_out_quad( float t ) noexcept;
		[[nodiscard]] float in_cubic( float t ) noexcept;
		[[nodiscard]] float out_cubic( float t ) noexcept;
		[[nodiscard]] float in_out_cubic( float t ) noexcept;
		[[nodiscard]] float smoothstep( float t ) noexcept;

	} // namespace ease

	struct input_state
	{
		float mouse_x{};
		float mouse_y{};
		float prev_mouse_x{};
		float prev_mouse_y{};

		bool mouse_down{};
		bool mouse_clicked{};
		bool mouse_released{};
		bool mouse_double_clicked{};

		bool rmb_down{};
		bool rmb_clicked{};
		bool rmb_released{};

		float scroll_delta{};

		std::array<wchar_t, 32> char_queue{};
		std::size_t char_count{};

		std::array<int, 32> key_press_queue{};
		std::size_t key_press_count{};

		std::array<int, 32> key_release_queue{};
		std::size_t key_release_count{};

		std::unordered_map<int, bool> keys{};

		[[nodiscard]] std::span<const wchar_t> chars( ) const noexcept;
		[[nodiscard]] std::span<const int> key_presses( ) const noexcept;
		[[nodiscard]] std::span<const int> key_releases( ) const noexcept;

		[[nodiscard]] bool key_down( int vk ) const noexcept;
		[[nodiscard]] bool shift_held( ) const noexcept;
		[[nodiscard]] bool ctrl_held( ) const noexcept;

		[[nodiscard]] float mouse_delta_x( ) const noexcept { return this->mouse_x - this->prev_mouse_x; }
		[[nodiscard]] float mouse_delta_y( ) const noexcept { return this->mouse_y - this->prev_mouse_y; }
		[[nodiscard]] bool in_rect( const rect& r ) const noexcept { return r.contains( this->mouse_x, this->mouse_y ); }

		void push_char( wchar_t c );
		void push_key_press( int vk );
		void push_key_release( int vk );
		void clear_frame( );
	};

	bool feed_wndproc( input_state& s, UINT msg, WPARAM wp, LPARAM lp );

	struct style
	{
		float rounding{ 12.0f };
		float checkbox_rounding{ 4.0f };
		float slider_rounding{ 0.1f };
		float button_rounding{ 8.0f };
		float keybind_rounding{ 4.0f };
		float combo_rounding{ 4.0f };
		float popup_rounding{ 8.0f };
		float combo_popup_rounding{ 4.0f };
		float picker_popup_rounding{ 8.0f };
		float color_swatch_rounding{ 4.0f };
		float text_input_rounding{ 6.0f };

		float window_pad_x{ 10.0f };
		float window_pad_y{ 10.0f };
		float item_spacing_x{ 8.0f };
		float item_spacing_y{ 8.0f };
		float frame_pad_x{ 8.0f };
		float frame_pad_y{ 4.0f };
		float border_thickness{ 0.0f };

		float checkbox_size{ 16.0f };
		float slider_h{ 7.0f };
		float keybind_w{ 80.0f };
		float keybind_h{ 22.0f };
		float combo_h{ 20.0f };
		float combo_item_h{ 22.0f };
		float swatch_w{ 26.0f };
		float swatch_h{ 16.0f };
		float text_input_h{ 24.0f };

		xdraw::color window_bg{ 35, 35, 40, 175 };
		xdraw::color window_border{ 0, 0, 0, 0 };

		xdraw::color child_bg{ 17, 17, 17, 82 };
		xdraw::color child_border{ 0, 0, 0, 0 };

		xdraw::color checkbox_bg{ 17, 17, 17, 82 };
		xdraw::color checkbox_border{ 0, 0, 0, 0 };
		xdraw::color checkbox_mark{ 173, 192, 255, 255 };
		xdraw::color checkbox_mark_icon{ 17, 17, 17, 255 };

		xdraw::color slider_track{ 17, 17, 17, 82 };
		xdraw::color slider_fill{ 173, 192, 255, 255 };

		xdraw::color button_bg{ 17, 17, 17, 82 };
		xdraw::color button_border{ 0, 0, 0, 0 };
		xdraw::color button_hovered{ 17, 17, 17, 120 };
		xdraw::color button_active{ 173, 192, 255, 255 };

		xdraw::color keybind_bg{ 17, 17, 17, 82 };
		xdraw::color keybind_border{ 0, 0, 0, 0 };
		xdraw::color keybind_waiting{ 173, 192, 255, 255 };

		xdraw::color combo_bg{ 17, 17, 17, 82 };
		xdraw::color combo_border{ 0, 0, 0, 0 };
		xdraw::color combo_arrow{ 221, 229, 255, 82 };
		xdraw::color combo_hovered{ 17, 17, 17, 120 };
		xdraw::color combo_popup_bg{ 17, 17, 17, 120 };
		xdraw::color combo_popup_border{ 0, 0, 0, 0 };
		xdraw::color combo_popup_item_hovered{ 17, 17, 17, 120 };
		xdraw::color combo_popup_item_selected{ 173, 192, 255, 36 };

		xdraw::color popup_bg{ 17, 17, 17, 120 };
		xdraw::color popup_border{ 0, 0, 0, 0 };

		xdraw::color picker_bg{ 17, 17, 17, 82 };
		xdraw::color picker_border{ 0, 0, 0, 0 };
		xdraw::color picker_popup_bg{ 17, 17, 17, 120 };
		xdraw::color picker_popup_border{ 0, 0, 0, 0 };

		xdraw::color text_input_bg{ 17, 17, 17, 82 };
		xdraw::color text_input_border{ 0, 0, 0, 0 };

		xdraw::color separator{ 173, 192, 255, 30 };

		xdraw::color text{ 221, 229, 255, 235 };
		xdraw::color text_dim{ 173, 192, 255, 82 };
		xdraw::color accent{ 173, 192, 255, 255 };
	};

	enum class style_var
	{
		rounding,
		checkbox_rounding,
		slider_rounding,
		button_rounding,
		keybind_rounding,
		combo_rounding,
		popup_rounding,
		combo_popup_rounding,
		picker_popup_rounding,
		color_swatch_rounding,
		text_input_rounding,
		window_pad_x,
		window_pad_y,
		item_spacing_x,
		item_spacing_y,
		frame_pad_x,
		frame_pad_y,
		border_thickness,
		checkbox_size,
		slider_h,
		keybind_w,
		keybind_h,
		combo_h,
		combo_item_h,
		swatch_w,
		swatch_h,
		text_input_h
	};

	enum class style_col
	{
		window_bg,
		window_border,
		child_bg,
		child_border,
		checkbox_bg,
		checkbox_border,
		checkbox_mark,
		checkbox_mark_icon,
		slider_track,
		slider_fill,
		button_bg,
		button_border,
		button_hovered,
		button_active,
		keybind_bg,
		keybind_border,
		keybind_waiting,
		combo_bg,
		combo_border,
		combo_arrow,
		combo_hovered,
		combo_popup_bg,
		combo_popup_border,
		combo_popup_item_hovered,
		combo_popup_item_selected,
		popup_bg,
		popup_border,
		picker_bg,
		picker_border,
		picker_popup_bg,
		picker_popup_border,
		text_input_bg,
		text_input_border,
		separator,
		text,
		text_dim,
		accent
	};

	void push_style_var( style_var var, float value );
	void pop_style_var( int count = 1 );
	void push_style_color( style_col idx, xdraw::color col );
	void pop_style_color( int count = 1 );

	namespace anim {

		[[nodiscard]] float lerp( std::uintptr_t id, float target, float speed, float initial = 0.0f );
		[[nodiscard]] float smooth( std::uintptr_t id, float target, float speed, float initial = 0.0f );
		[[nodiscard]] float get( std::uintptr_t id, float fallback = 0.0f );
		void set( std::uintptr_t id, float value );
		void remove( std::uintptr_t id );
		void clear_all( );

	} // namespace anim

	enum class bind_mode : int
	{
		toggle,
		hold_on,
		hold_off
	};

	struct setting;

	struct bind_info
	{
		int key{};
		bind_mode mode{ bind_mode::toggle };
		bool active{};
		setting* excludes{};
	};

	struct setting
	{
		bool value{};
		bind_info bind{};
		std::string name{};
		std::string category{};

		setting( bool v = {}, bind_info b = {}, std::string n = {}, std::string c = {} );

		explicit operator bool( ) const noexcept { return this->value; }
	};

	namespace binds {

		void register_setting( setting* s );
		void unregister_setting( setting* s );

		void process( const input_state& input );

		[[nodiscard]] std::span<setting* const> all( );
		[[nodiscard]] std::size_t count( );

		void clear_all( );

		[[nodiscard]] std::uintptr_t listening_id( );

		[[nodiscard]] const char* mode_name( bind_mode m ) noexcept;

	} // namespace binds

	inline setting::setting( bool v, bind_info b, std::string n, std::string c ) : value{ v }, bind{ std::move( b ) }, name{ std::move( n ) }, category{ std::move( c ) } { binds::register_setting( this ); }

	class overlay
	{
	public:
		virtual ~overlay( ) = default;

		[[nodiscard]] std::uintptr_t id( ) const noexcept { return this->m_id; }
		[[nodiscard]] const rect& anchor( ) const noexcept { return this->m_anchor; }
		[[nodiscard]] bool is_closing( ) const noexcept { return this->m_closing; }
		[[nodiscard]] bool is_closed( ) const noexcept { return this->m_closed; }

		void update_anchor( const rect& r ) noexcept { this->m_anchor = r; }
		void request_close( ) noexcept { this->m_closing = true; }
		void force_close( ) noexcept { this->m_closing = true; this->m_closed = true; }

		[[nodiscard]] virtual bool hit_test( float x, float y ) const = 0;
		virtual bool process_input( const input_state& input ) = 0;
		virtual void render( const style& style, const input_state& input ) = 0;

	protected:
		explicit overlay( std::uintptr_t id, const rect& anchor ) : m_id{ id }, m_anchor{ anchor } {}

		std::uintptr_t m_id{ null_id };
		rect m_anchor{};
		bool m_closing{};
		bool m_closed{};
	};

	class popup_overlay : public overlay
	{
	public:
		popup_overlay( std::uintptr_t id, const rect& anchor, float width );

		[[nodiscard]] bool hit_test( float x, float y ) const override;
		bool process_input( const input_state& input ) override;
		void render( const style&, const input_state& ) override;

		void tick( );
		void set_content_h( float h );

		[[nodiscard]] float open_anim( ) const;
		[[nodiscard]] bool closing( ) const;
		[[nodiscard]] rect get_popup( ) const;

	private:
		float m_width{};
		float m_content_h{ 100.0f };
		float m_open_anim{};
	};

	namespace overlays {

		[[nodiscard]] bool is_open( std::uintptr_t id );
		[[nodiscard]] overlay* find( std::uintptr_t id );

		void close( std::uintptr_t id );
		void close_all( );

		void touch( std::uintptr_t owner_id );

		[[nodiscard]] bool wants_input( float x, float y );
		[[nodiscard]] bool has_any( );
		[[nodiscard]] bool has_any_except( std::uintptr_t exclude );

		void process( const input_state& input );
		void render( const style& style, const input_state& input );
		void sweep( );
		void add( std::unique_ptr<overlay> ov );

	} // namespace overlays

	void push_id( std::uintptr_t id );
	void push_id( std::string_view sv );
	void push_id( const void* ptr );
	void pop_id( );
	[[nodiscard]] std::uintptr_t make_id( std::string_view label );

	struct window_state
	{
		std::string title{};
		rect bounds{};
		float cursor_x{};
		float cursor_y{};
		float line_h{};
		rect last_item{};
		bool is_child{};
		float content_h{};
		std::uintptr_t group_id{ null_id };
		bool scrollable{};
		float scroll_y{};
	}; 
	
	struct child_scroll_state
	{
		float scroll{};
		float scroll_target{};
		float scrollbar_anim{};
		bool scrollbar_dragging{};
		float scrollbar_drag_offset{};
	};

	namespace layout {

		rect item( float w, float h, float label_h = 0.0f );

		[[nodiscard]] window_state* current_window( );
		[[nodiscard]] const window_state* current_window_const( );

		[[nodiscard]] std::pair<float, float> avail( );
		[[nodiscard]] float item_width( int count = 0 );

		void same_line( float offset = 0.0f );
		void new_line( );
		void spacing( float amount = 0.0f );
		void indent( float amount = 0.0f );
		void unindent( float amount = 0.0f );
		void separator( );

		void set_cursor( float x, float y );
		[[nodiscard]] std::pair<float, float> get_cursor( );

	} // namespace layout

	namespace draw {

		[[nodiscard]] xdraw::draw_list& current( );

		void push_layer( xdraw::layer l );
		void pop_layer( );

	} // namespace draw

	struct context
	{
		input_state input{};
		xui::style style{};

		std::uintptr_t active_window{};
		std::uintptr_t active_resize{};
		std::uintptr_t active_slider{};
		std::uintptr_t active_keybind{};
		std::uintptr_t active_text_input{};
		std::uintptr_t active_slider_edit{};
		std::uintptr_t active_child_scroll{};

		std::uintptr_t inside_overlay{};

		std::string slider_edit_buf{};
		std::size_t slider_edit_cursor{};
		float slider_edit_blink{};

		std::vector<window_state> windows{};
		std::vector<std::uintptr_t> id_stack{};
		std::vector<xdraw::layer> layer_stack{};

		std::vector<std::pair<style_var, float>> style_var_stack{};
		std::vector<std::pair<style_col, xdraw::color>> style_color_stack{};

		std::string truncation_scratch{};

		std::unordered_map<std::uintptr_t, float> child_height_cache{};
		std::unordered_map<std::uintptr_t, child_scroll_state> child_scroll_cache{};

		[[nodiscard]] bool overlay_blocking( ) const;
	};

	[[nodiscard]] context& ctx( );

	bool initialize( HWND hwnd );
	void begin( );
	void end( );

	bool wndproc( UINT msg, WPARAM wp, LPARAM lp );

	[[nodiscard]] std::string_view truncate( std::string_view text, float max_width );
	[[nodiscard]] const char* vk_name( int vk );
	void set_highlight_target( std::string_view label, float duration_seconds = 1.0f );

	bool begin_window( std::string_view title, float& x, float& y, float& w, float& h, bool resizable = false, float min_w = 200.0f, float min_h = 200.0f, float reveal = 1.0f );
	void end_window( );

	bool begin_child( std::string_view title, float w, float h = 0.0f, bool scrollable = false );
	void end_child( );

	bool begin_popup( std::string_view label, float width = 200.0f );
	void end_popup( );

	void text( std::string_view label, xdraw::color col );

	bool button( std::string_view label, float w, float h );
	bool checkbox( std::string_view label, setting& s );

	bool slider_float( std::string_view label, float& v, float v_min, float v_max, std::string_view fmt = "%.2f" );
	bool slider_int( std::string_view label, int& v, int v_min, int v_max, std::string_view fmt = "%d" );

	bool keybind( std::string_view label, int& key );

	bool combo( std::string_view label, int& current, const char* const items[ ], int count, float width = 0.0f );

	template<typename E> requires std::is_enum_v<E>
	bool combo( std::string_view label, E& value, const char* const items[ ], int count, float width = 0.0f )
	{
		static std::unordered_map<std::uintptr_t, int> enum_storage{};

		const auto id = make_id( label );
		auto& stored = enum_storage[ id ];

		if ( !overlays::find( id ) )
		{
			stored = static_cast< int >( value );
		}

		if ( combo( label, stored, items, count, width ) )
		{
			value = static_cast< E >( stored );
			return true;
		}

		value = static_cast< E >( stored );
		return false;
	}

	bool multicombo( std::string_view label, bool* selected, const char* const items[ ], int count, float width = 0.0f );

	bool color_picker( std::string_view label, xdraw::color& col, float width = 0.0f, bool show_alpha = true );

	bool text_input( std::string_view label, std::string& buf, std::size_t max_len = 256, std::string_view hint = "" );

} // namespace xui