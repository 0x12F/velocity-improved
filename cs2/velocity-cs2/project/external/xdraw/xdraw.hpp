#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <span>
#include <utility>
#include <string_view>

namespace xdraw {

	enum class layer
	{
		bottom,
		middle,
		top,
		count
	};

	enum class text_style
	{
		normal,
		outlined,
		shadowed
	};

	struct color
	{
		union
		{
			std::uint32_t val;
			struct { std::uint8_t r, g, b, a; };
		};

		constexpr color( ) : val{ 0 } {}
		constexpr color( std::uint32_t v ) : val{ v } {}
		constexpr color( std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255 ) : r{ r }, g{ g }, b{ b }, a{ a } {}

		constexpr color alpha( std::uint8_t a_ ) const { return color{ r, g, b, a_ }; }
		constexpr operator std::uint32_t( ) const { return val; }
		constexpr std::array<float, 4> to_float( ) const { return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }
	};

	struct corner_radius
	{
		float tl{}, tr{}, br{}, bl{};

		constexpr corner_radius( ) = default;
		constexpr corner_radius( float all ) : tl{ all }, tr{ all }, br{ all }, bl{ all } {}
		constexpr corner_radius( float tl, float tr, float br, float bl ) : tl{ tl }, tr{ tr }, br{ br }, bl{ bl } {}

		static constexpr corner_radius top( float r ) { return { r, r, 0.0f, 0.0f }; }
		static constexpr corner_radius bottom( float r ) { return { 0.0f, 0.0f, r, r }; }
		static constexpr corner_radius left( float r ) { return { r, 0.0f, 0.0f, r }; }
		static constexpr corner_radius right( float r ) { return { 0.0f, r, r, 0.0f }; }
	};

	struct vertex
	{
		float pos[ 2 ];
		float uv[ 2 ];
		color col;
	};

	struct draw_cmd
	{
		std::uint32_t idx_offset{};
		std::uint32_t idx_count{};
		ID3D11ShaderResourceView* texture{};
		D3D11_RECT scissor{};
		bool has_scissor{};
	};

	struct glyph
	{
		float advance{};
		float bearing_x{}, bearing_y{};
		float width{}, height{};
		float atlas_x{}, atlas_y{};
	};

	struct font
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> atlas_tex{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> atlas_srv{};
		int atlas_w{};
		int atlas_h{};

		float size{};
		float ascent{};
		float descent{};
		float line_height{};

		std::unordered_map<char32_t, glyph> glyph_cache{};
		glyph missing_glyph{};

		font* fallback{};

		void* ft_library{};
		void* ft_face{};

		int pen_x{ 1 };
		int pen_y{ 1 };
		int row_h{ 0 };
		std::vector<std::uint8_t> atlas_bitmap{};
		bool atlas_dirty{};

		~font( );

		[[nodiscard]] const glyph& get( char32_t cp );
		[[nodiscard]] bool has_glyph( char32_t cp ) const;
		[[nodiscard]] std::pair<font*, const glyph*> resolve( char32_t cp );
		[[nodiscard]] std::pair<float, float> measure( std::string_view str );

		void flush_atlas( );
		bool rasterize( char32_t cp );
	};

	struct draw_list
	{
		std::vector<vertex> vertices{};
		std::vector<std::uint32_t> indices{};
		std::vector<draw_cmd> commands{};
		std::vector<D3D11_RECT> clip_stack{};

		void clear( );

		void push_clip( float x, float y, float w, float h );
		void push_clip_absolute( float x, float y, float w, float h );
		void pop_clip( );

		void rect_filled( float x, float y, float w, float h, color col );
		void rect_filled( float x, float y, float w, float h, color col, corner_radius rounding, bool aa = true );
		void rect_filled_gradient( float x, float y, float w, float h, color tl, color tr, color br, color bl );
		void rect_filled_gradient( float x, float y, float w, float h, color tl, color tr, color br, color bl, corner_radius rounding, bool aa = true );
		void rect_filled_blurred( float x, float y, float w, float h, color tint = color{ 255, 255, 255, 255 } );
		void rect_filled_blurred( float x, float y, float w, float h, corner_radius rounding, color tint = color{ 255, 255, 255, 255 }, bool aa = true );
		void circle_filled( float cx, float cy, float radius, color col, int segments = 0, bool aa = true );
		void triangle_filled( float x0, float y0, float x1, float y1, float x2, float y2, color col, bool aa = true );
		void convex_filled( std::span<const float> points, color col, bool aa = true );

		void rect( float x, float y, float w, float h, color col, float thickness = 1.0f, bool aa = true );
		void rect( float x, float y, float w, float h, color col, corner_radius rounding, float thickness = 1.0f, bool aa = true );
		void circle( float cx, float cy, float radius, color col, float thickness = 1.0f, int segments = 0, bool aa = true );
		void line( float x0, float y0, float x1, float y1, color col, float thickness = 1.0f, bool aa = true );
		void polyline( std::span<const float> points, color col, bool closed, float thickness = 1.0f, bool aa = true );
		void polyline_gradient( std::span<const float> points, std::span<const color> colors, bool closed, float thickness = 1.0f, bool aa = true );

		void text( float x, float y, std::string_view str, color col, font* f = nullptr );
		void text( float x, float y, std::string_view str, color col, text_style style, font* f = nullptr );
		void text( float x, float y, std::string_view str, color col, text_style style, color shadow_col, font* f = nullptr );

		void image( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, color tint = color{ 255, 255, 255, 255 } );
		void image( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, corner_radius rounding, color tint = color{ 255, 255, 255, 255 }, bool aa = true );
		void image_uv( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, float u0, float v0, float u1, float v1, color tint = color{ 255, 255, 255, 255 } );

		void ensure_cmd( ID3D11ShaderResourceView* texture );
		std::uint32_t emit_vtx( float x, float y, float u, float v, color c );
		void emit_idx( std::uint32_t a, std::uint32_t b, std::uint32_t c );
		void emit_quad( std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d );

		void build_rounded_rect_path( float x, float y, float w, float h, corner_radius r, std::vector<float>& path ) const;
		[[nodiscard]] int auto_segments( float radius ) const;
	};

	struct gif_image
	{
		struct frame
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
			float delay{};
			int x{}, y{}, w{}, h{};
			int disposal{};
		};

		std::vector<frame> frames{};
		std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> composited{};
		int canvas_w{};
		int canvas_h{};
		int current_idx{};
		float elapsed{};

		void update( float dt );
		void reset( );

		[[nodiscard]] ID3D11ShaderResourceView* current_srv( ) const;
		[[nodiscard]] int frame_count( ) const { return static_cast< int >( frames.size( ) ); }
		[[nodiscard]] int width( ) const { return canvas_w; }
		[[nodiscard]] int height( ) const { return canvas_h; }
		[[nodiscard]] bool valid( ) const { return !frames.empty( ) && !composited.empty( ); }
	};

	bool initialize( ID3D11Device* device, ID3D11DeviceContext* context );

	void begin_frame( bool update_timing = true );
	void end_frame( );

	[[nodiscard]] draw_list& get( layer l = layer::middle );
	[[nodiscard]] draw_list& get_glow( layer l = layer::middle );

	[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> create_srv_from_rgba( const std::uint8_t* pixels, int w, int h );
	[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_texture( std::span<const std::byte> data, int* w = nullptr, int* h = nullptr );
	[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_svg( std::span<const std::byte> data, float scale = 1.0f, int* out_width = nullptr, int* out_height = nullptr );
	[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_svg( const char* svg_text, float scale = 1.0f, int* out_width = nullptr, int* out_height = nullptr );

	[[nodiscard]] gif_image load_gif( std::span<const std::byte> data );
	[[nodiscard]] font* load_font( std::span<const std::byte> data, float size_px, int atlas_w = 1024, int atlas_h = 1024 );

	void push_font( font* f );
	void pop_font( );

	[[nodiscard]] font* current_font( );
	[[nodiscard]] font* primary_font( );

	[[nodiscard]] std::pair<int, int> viewport_size( );
	[[nodiscard]] std::pair<float, float> measure_text( std::string_view str, font* f = nullptr );

	[[nodiscard]] float delta_time( );
	[[nodiscard]] float framerate( );

	[[nodiscard]] ID3D11Device* device( );

} // namespace xdraw

namespace tokens {

	inline xdraw::color col_accent{ 173, 192, 255, 255 };
	inline xdraw::color col_dark{ 17, 17, 17, 255 };
	inline xdraw::color col_text{ 221, 229, 255, 235 };
	inline xdraw::color col_text_dim{ 173, 192, 255, 82 };
	inline xdraw::color col_card{ 17, 17, 17, 82 };
	inline xdraw::color col_elevated{ 31, 31, 35, 118 };

	constexpr auto sidebar_w{ 42.0f };
	constexpr auto tab_icon_size{ 35.0f };
	constexpr auto subtab_bar_h{ 35.0f };
	constexpr auto gap{ 8.0f };
	constexpr auto card_rounding{ 10.0f };
	constexpr auto btn_rounding{ 8.0f };

} // namespace tokens
