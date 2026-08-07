#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		inline static auto& skin_map( ) { return settings::g_changer.skins.data; }

		enum class skins_page : int
		{
			grid,
			browser
		};

		struct skins_state
		{
			skins_page current{ skins_page::grid };
			skins_page target{ skins_page::grid };
			float fade{ 1.0f };

			std::int16_t browsing_def{};
			int browsing_agent_team{};
			std::string search_buf{};
		};

		skins_state skins_ui{};

		constexpr xdraw::color k_rarity_colors[ 8 ]
		{
			xdraw::color{ 235, 235, 235, 255 },
			xdraw::color{ 138, 173, 233, 255 },
			xdraw::color{  77, 116, 196, 255 },
			xdraw::color{ 138,  86, 207, 255 },
			xdraw::color{ 211,  44, 230, 255 },
			xdraw::color{ 235,  75,  75, 255 },
			xdraw::color{ 228, 174,  57, 255 },
			xdraw::color{ 255, 215,   0, 255 }
		};

		constexpr auto k_card_w_ref{ 118.0f };
		constexpr auto k_card_h_ref{ 100.0f };
		constexpr auto k_card_gap{ 8.0f };
		constexpr auto k_columns{ 5 };
		constexpr auto k_image_h_ratio{ 0.70f };
		constexpr auto k_rarity_bar_h{ 2.0f };

		static inline const char* wear_tier( float w )
		{
			if ( w <= 0.07f )
			{
				return "FN";
			}

			if ( w <= 0.15f )
			{
				return "MW";
			}

			if ( w <= 0.38f )
			{
				return "FT";
			}

			if ( w <= 0.45f )
			{
				return "WW";
			}

			return "BS";
		}

		static inline std::string format_wear( float w )
		{
			char buf[ 32 ]{};
			std::snprintf( buf, sizeof( buf ), "%.4f %s", w, wear_tier( w ) );
			return std::string{ buf };
		}

		static inline void request_page( skins_page p, std::int16_t def = 0 )
		{
			if ( skins_ui.current == p && skins_ui.target == p )
			{
				return;
			}

			skins_ui.target = p;
			skins_ui.fade = 1.0f;

			if ( p == skins_page::browser )
			{
				skins_ui.browsing_def = def;
			}
			else
			{
				skins_ui.browsing_agent_team = 0;
			}
		}

		static inline const std::vector<const features::changer::econ_item_system::item_def*>& select_weapons( int subtab )
		{
			auto& econ = features::changer::g_econ_item_system;

			switch ( subtab )
			{
			case 1: return econ.knives( );
			case 2: return econ.gloves( );
			case 3: return econ.agents( );
			default: return econ.guns( );
			}
		}

		class wear_sub_overlay : public xui::overlay
		{
		public:
			wear_sub_overlay( std::uintptr_t id, const xui::rect& anchor, std::int16_t def ) : overlay{ id, anchor }, m_def{ def } {}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y );
			}

			bool process_input( const xui::input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto popup = this->get_popup( );
				if ( input.mouse_clicked && !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return false;
				}

				return popup.contains( input.mouse_x, input.mouse_y );
			}

			void render( const xui::style& style, const xui::input_state& ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto popup = this->get_popup( );

				const auto speed = this->m_closing ? 18.0f : 16.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto et = xui::ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = et;
				const auto animated_h = popup.h * et;
				const auto pr = style.popup_rounding;

				auto& dl = xdraw::get( xdraw::layer::top );

				auto bg = style.popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = xui::lighten( style.popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				if ( !this->m_closing )
				{
					dl.rect_filled_blurred( popup.x, popup.y, popup.w, animated_h, xdraw::corner_radius{ pr } );
				}

				dl.rect_filled( popup.x, popup.y, popup.w, animated_h, bg, xdraw::corner_radius{ pr } );
				dl.rect( popup.x, popup.y, popup.w, animated_h, border, xdraw::corner_radius{ pr } );

				if ( et < 0.2f )
				{
					return;
				}

				xui::draw::push_layer( xdraw::layer::top );
				dl.push_clip( popup.x, popup.y, popup.w, animated_h );

				xui::window_state ws{};
				ws.title = "##wear_sub";
				ws.bounds = popup;
				ws.cursor_x = style.window_pad_x;
				ws.cursor_y = style.window_pad_y;
				ws.is_child = true;

				auto& c = xui::ctx( );
				c.windows.push_back( std::move( ws ) );
				xui::push_id( this->m_id );

				const auto prev_inside = c.inside_overlay;
				c.inside_overlay = this->m_id;

				const auto it = skin_map( ).find( this->m_def );
				if ( it != skin_map( ).end( ) )
				{
					xui::slider_float( "wear", it->second.wear, 0.0f, 1.0f, "%.4f" );
				}

				c.inside_overlay = prev_inside;

				xui::pop_id( );
				c.windows.pop_back( );
				dl.pop_clip( );
				xui::draw::pop_layer( );
			}

		private:
			[[nodiscard]] xui::rect get_popup( ) const
			{
				return { this->m_anchor.x, this->m_anchor.y, 220.0f, 56.0f };
			}

			std::int16_t m_def{};
			float m_open_anim{};
		};

		class seed_sub_overlay : public xui::overlay
		{
		public:
			seed_sub_overlay( std::uintptr_t id, const xui::rect& anchor, std::int16_t def ) : overlay{ id, anchor }, m_def{ def }
			{
				const auto it = skin_map( ).find( def );
				if ( it != skin_map( ).end( ) )
				{
					this->m_buf = std::to_string( it->second.seed );
				}
			}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y );
			}

			bool process_input( const xui::input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				const auto popup = this->get_popup( );
				if ( input.mouse_clicked && !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;
					return false;
				}

				return popup.contains( input.mouse_x, input.mouse_y );
			}

			void render( const xui::style& style, const xui::input_state& ) override
			{
				const auto dt = xdraw::delta_time( );
				const auto popup = this->get_popup( );

				const auto speed = this->m_closing ? 18.0f : 16.0f;
				const auto target = this->m_closing ? 0.0f : 1.0f;
				this->m_open_anim += ( target - this->m_open_anim ) * std::min( speed * dt, 1.0f );

				if ( this->m_open_anim < 0.01f && this->m_closing )
				{
					this->m_closed = true;
					return;
				}

				const auto et = xui::ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = et;
				const auto animated_h = popup.h * et;
				const auto pr = style.popup_rounding;

				auto& dl = xdraw::get( xdraw::layer::top );

				auto bg = style.popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = xui::lighten( style.popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				if ( !this->m_closing )
				{
					dl.rect_filled_blurred( popup.x, popup.y, popup.w, animated_h, xdraw::corner_radius{ pr } );
				}

				dl.rect_filled( popup.x, popup.y, popup.w, animated_h, bg, xdraw::corner_radius{ pr } );
				dl.rect( popup.x, popup.y, popup.w, animated_h, border, xdraw::corner_radius{ pr } );

				if ( et < 0.2f )
				{
					return;
				}

				xui::draw::push_layer( xdraw::layer::top );
				dl.push_clip( popup.x, popup.y, popup.w, animated_h );

				xui::window_state ws{};
				ws.title = "##seed_sub";
				ws.bounds = popup;
				ws.cursor_x = style.window_pad_x;
				ws.cursor_y = style.window_pad_y;
				ws.is_child = true;

				auto& c = xui::ctx( );
				c.windows.push_back( std::move( ws ) );
				xui::push_id( this->m_id );

				const auto prev_inside = c.inside_overlay;
				c.inside_overlay = this->m_id;

				if ( xui::text_input( "##seed_input", this->m_buf, 4, "0-1000" ) )
				{
					const auto it = skin_map( ).find( this->m_def );
					if ( it != skin_map( ).end( ) )
					{
						const auto v = std::stoi( this->m_buf );
						it->second.seed = std::clamp( v, 0, 1000 );
					}
				}

				c.inside_overlay = prev_inside;

				xui::pop_id( );
				c.windows.pop_back( );
				dl.pop_clip( );
				xui::draw::pop_layer( );
			}

		private:
			[[nodiscard]] xui::rect get_popup( ) const
			{
				return { this->m_anchor.x, this->m_anchor.y, 180.0f, 50.0f };
			}

			std::int16_t m_def{};
			std::string m_buf{};
			float m_open_anim{};
		};

		class skin_context_overlay : public xui::overlay
		{
		public:
			skin_context_overlay( std::uintptr_t id, const xui::rect& anchor, std::int16_t def, std::string weapon_name, std::string skin_name ) : overlay{ id, anchor }, m_def{ def }, m_weapon_name{ std::move( weapon_name ) }, m_skin_name{ std::move( skin_name ) }
			{
				this->m_hover_anims.fill( 0.0f );
				this->m_item_anims.fill( 0.0f );
			}

			[[nodiscard]] bool hit_test( float x, float y ) const override
			{
				return this->get_popup( ).contains( x, y );
			}

			bool process_input( const xui::input_state& input ) override
			{
				if ( this->m_closing )
				{
					return false;
				}

				if ( this->m_sub_id != xui::null_id )
				{
					if ( const auto sub = xui::overlays::find( this->m_sub_id ) )
					{
						if ( sub->hit_test( input.mouse_x, input.mouse_y ) )
						{
							return false;
						}
					}
					else
					{
						this->m_sub_id = xui::null_id;
					}
				}

				const auto popup = this->get_popup( );

				if ( ( input.mouse_clicked || input.rmb_clicked ) && !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					this->m_closing = true;

					if ( this->m_sub_id != xui::null_id )
					{
						xui::overlays::close( this->m_sub_id );
					}

					return true;
				}

				if ( !input.mouse_clicked || !popup.contains( input.mouse_x, input.mouse_y ) )
				{
					return popup.contains( input.mouse_x, input.mouse_y );
				}

				for ( auto i = 1; i < k_item_count; ++i )
				{
					const auto ir = this->get_item_rect( popup, i );
					if ( !ir.contains( input.mouse_x, input.mouse_y ) )
					{
						continue;
					}

					const auto it = skin_map( ).find( this->m_def );
					if ( it == skin_map( ).end( ) )
					{
						return true;
					}

					if ( i == 1 )
					{
						it->second.stattrak = !it->second.stattrak;
					}
					else if ( i == 2 || i == 3 )
					{
						if ( this->m_sub_id != xui::null_id )
						{
							xui::overlays::close( this->m_sub_id );
						}

						const auto sub_anchor = xui::rect{ popup.right( ) + 4.0f, ir.y, 0.0f, 0.0f };
						this->m_sub_id = ( i == 2 ) ? ( this->m_id ^ 0x77711ull ) : ( this->m_id ^ 0x77722ull );

						if ( i == 2 )
						{
							xui::overlays::add( std::make_unique<wear_sub_overlay>( this->m_sub_id, sub_anchor, this->m_def ) );
						}
						else
						{
							xui::overlays::add( std::make_unique<seed_sub_overlay>( this->m_sub_id, sub_anchor, this->m_def ) );
						}
					}
					else if ( i == 4 )
					{
						skin_map( ).erase( this->m_def );

						this->m_closing = true;

						if ( this->m_sub_id != xui::null_id )
						{
							xui::overlays::close( this->m_sub_id );
						}
					}

					return true;
				}

				return true;
			}

			void render( const xui::style& style, const xui::input_state& input ) override
			{
				if ( this->m_sub_id != xui::null_id )
				{
					xui::overlays::touch( this->m_sub_id );
				}

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

				const auto ease_t = xui::ease::out_cubic( this->m_open_anim );
				const auto alpha_mult = ease_t;
				const auto animated_h = popup.h * ease_t;
				const auto pr = style.popup_rounding;

				auto bg = style.popup_bg;
				bg.a = static_cast< std::uint8_t >( bg.a * alpha_mult );
				auto border = xui::lighten( style.popup_border, 1.1f );
				border.a = static_cast< std::uint8_t >( border.a * alpha_mult );

				if ( !this->m_closing )
				{
					dl.rect_filled_blurred( popup.x, popup.y, popup.w, animated_h, xdraw::corner_radius{ pr } );
				}

				dl.rect_filled( popup.x, popup.y, popup.w, animated_h, bg, xdraw::corner_radius{ pr } );
				dl.rect( popup.x, popup.y, popup.w, animated_h, border, xdraw::corner_radius{ pr } );

				dl.push_clip( popup.x, popup.y, popup.w, animated_h );

				const auto it = skin_map( ).find( this->m_def );

				for ( auto i = 0; i < k_item_count; ++i )
				{
					const auto ir = this->get_item_rect( popup, i );
					const auto item_delay = i * 0.04f;
					const auto item_progress = std::clamp( ( this->m_open_anim - item_delay ) / ( 1.0f - std::min( item_delay, 0.3f ) ), 0.0f, 1.0f );
					auto& ia = this->m_item_anims[ i ];
					ia = std::min( ia + 20.0f * dt, item_progress );

					const auto item_ease = xui::ease::out_cubic( ia );
					const auto item_alpha = item_ease * alpha_mult;
					const auto slide = ( 1.0f - item_ease ) * 6.0f;

					if ( i == 0 )
					{
						char header[ 128 ]{};
						if ( !this->m_skin_name.empty( ) )
						{
							std::snprintf( header, sizeof( header ), "%s | %s", this->m_weapon_name.c_str( ), this->m_skin_name.c_str( ) );
						}
						else
						{
							std::snprintf( header, sizeof( header ), "%s", this->m_weapon_name.c_str( ) );
						}

						auto col = style.text;
						col.a = static_cast< std::uint8_t >( col.a * item_alpha );

						const auto trunc = xui::truncate( header, ir.w - 16.0f );
						const auto [tw, th] = xdraw::measure_text( trunc );
						dl.text( ir.x + 8.0f, ir.y + ( k_item_h - th ) * 0.5f + slide, trunc, col );
						continue;
					}

					if ( i == k_item_count - 1 )
					{
						auto sep = style.separator;
						sep.a = static_cast< std::uint8_t >( sep.a * item_alpha );
						dl.line( ir.x + 6.0f, ir.y + slide, ir.right( ) - 6.0f, ir.y + slide, sep, 1.0f );
					}

					const auto is_hovered = !this->m_closing && ir.contains( input.mouse_x, input.mouse_y );
					auto& ha = this->m_hover_anims[ i ];
					ha += ( ( is_hovered ? 1.0f : 0.0f ) - ha ) * std::min( 18.0f * dt, 1.0f );

					if ( ha > 0.01f )
					{
						auto hov = style.combo_popup_item_hovered;
						hov.a = static_cast< std::uint8_t >( hov.a * item_alpha * ha );
						const auto first = ( i == 1 );
						const auto last = ( i == k_item_count - 1 );
						dl.rect_filled( ir.x, ir.y + slide, ir.w, ir.h, hov, xdraw::corner_radius{ first ? pr : 0.0f, first ? pr : 0.0f, last ? pr : 0.0f, last ? pr : 0.0f } );
					}

					if ( i == k_item_count - 1 )
					{
						auto col = xdraw::color{ 235, 75, 75, 230 };
						col = xui::lerp( col, xdraw::color{ 255, 100, 100, 255 }, ha );
						col.a = static_cast< std::uint8_t >( col.a * item_alpha );

						const auto [tw, th] = xdraw::measure_text( "remove skin" );
						dl.text( ir.x + 8.0f, ir.y + ( k_item_h - th ) * 0.5f + slide, "remove skin", col );
						continue;
					}

					static constexpr const char* labels[ ]{ "", "stattrak", "wear", "seed" };

					std::string value;
					if ( it != skin_map( ).end( ) )
					{
						if ( i == 1 )
						{
							value = it->second.stattrak ? "on" : "off";
						}
						else if ( i == 2 )
						{
							value = format_wear( it->second.wear );
						}
						else if ( i == 3 )
						{
							value = std::to_string( it->second.seed );
						}
					}
					else
					{
						value = "-";
					}

					auto label_col = style.text_dim;
					label_col.a = static_cast< std::uint8_t >( label_col.a * item_alpha );

					auto val_col = style.text;
					val_col = xui::lerp( val_col, xui::lighten( val_col, 1.3f ), ha );
					val_col.a = static_cast< std::uint8_t >( val_col.a * item_alpha );

					const auto [lw, lh] = xdraw::measure_text( labels[ i ] );
					dl.text( ir.x + 8.0f, ir.y + ( k_item_h - lh ) * 0.5f + slide, labels[ i ], label_col );

					const auto [vw, vh] = xdraw::measure_text( value );
					dl.text( ir.right( ) - vw - 8.0f, ir.y + ( k_item_h - vh ) * 0.5f + slide, value, val_col );
				}

				dl.pop_clip( );
			}

		private:
			static constexpr auto k_item_count{ 5 };
			static constexpr auto k_item_h{ 24.0f };
			static constexpr auto k_pad{ 4.0f };
			static constexpr auto k_popup_w{ 200.0f };

			[[nodiscard]] xui::rect get_popup( ) const
			{
				const auto h = k_pad * 2.0f + k_item_h * static_cast< float >( k_item_count );
				return { this->m_anchor.x, this->m_anchor.y, k_popup_w, h };
			}

			[[nodiscard]] xui::rect get_item_rect( const xui::rect& popup, int index ) const
			{
				return { popup.x + k_pad, popup.y + k_pad + static_cast< float >( index ) * k_item_h, popup.w - k_pad * 2.0f, k_item_h };
			}

			std::int16_t m_def{};
			std::string m_weapon_name{};
			std::string m_skin_name{};
			std::uintptr_t m_sub_id{ xui::null_id };
			float m_open_anim{};
			std::array<float, 5> m_hover_anims{};
			std::array<float, 5> m_item_anims{};
		};

		static inline void draw_agent_team_card( const xui::rect& card, int team, float fade_alpha )
		{
			auto& econ = features::changer::g_econ_item_system;
			auto& dl = xui::draw::current( );
			const auto& input = xui::ctx( ).input;

			const auto& sel = settings::g_changer.agents;
			const auto selected_def = ( team == 3 ) ? sel.ct_def : sel.t_def;
			const auto def = ( selected_def != 0 ) ? econ.find_def( selected_def ) : nullptr;

			const features::changer::econ_item_system::item_def* default_agent{ nullptr };

			for ( const auto a : econ.agents( ) )
			{
				const auto agent_team = a->team( );
				if ( agent_team == team || agent_team == 0 )
				{
					default_agent = a;
					break;
				}
			}

			const auto image_h = std::floor( card.h * k_image_h_ratio );
			const auto hovered = !xui::ctx( ).overlay_blocking( ) && input.in_rect( card );
			const auto hover_anim = xui::anim::lerp( xui::fnv1a( "ateam" ) + static_cast< std::uintptr_t >( team ), hovered ? 1.0f : 0.0f, 14.0f );

			auto card_bg = tokens::col_card;
			card_bg = xui::lerp( card_bg, xui::lighten( card_bg, 1.4f ), hover_anim * 0.5f );
			card_bg.a = static_cast< std::uint8_t >( card_bg.a * fade_alpha );
			dl.rect_filled( card.x, card.y, card.w, card.h, card_bg, xdraw::corner_radius{ tokens::btn_rounding } );

			if ( def )
			{
				auto bcol = tokens::col_accent;
				bcol.a = static_cast< std::uint8_t >( bcol.a * fade_alpha );
				dl.rect( card.x, card.y, card.w, card.h, bcol, xdraw::corner_radius{ tokens::btn_rounding }, 1.0f );
			}

			const auto img_source = def ? def : default_agent;
			if ( img_source )
			{
				const auto img = econ.get_skin_image( img_source->image_inventory );
				if ( img )
				{
					const auto target_h = image_h - 12.0f;
					const auto aspect = static_cast< float >( img->width ) / static_cast< float >( img->height );
					auto iw = target_h * aspect;
					auto ih = target_h;

					if ( iw > card.w - 12.0f )
					{
						iw = card.w - 12.0f;
						ih = iw / aspect;
					}

					const auto ix = std::floor( card.x + ( card.w - iw ) * 0.5f );
					const auto iy = std::floor( card.y + ( image_h - ih ) * 0.5f );
					const auto alpha = def ? 255.0f : 100.0f;
					const auto tint = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( alpha * fade_alpha ) };

					dl.image( ix, iy, iw, ih, img->srv.Get( ), tint );
				}
			}

			const auto name_y = card.y + image_h + k_rarity_bar_h + 4.0f;
			const auto label = def ? def->localized_name : ( team == 3 ? "CT" : "T" );

			auto ncol = xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );
			ncol.a = static_cast< std::uint8_t >( ncol.a * fade_alpha );

			const auto ntrunc = xui::truncate( label, card.w - 12.0f );
			const auto [nw, nh] = xdraw::measure_text( ntrunc );
			dl.text( std::floor( card.x + ( card.w - nw ) * 0.5f ), std::floor( name_y ), ntrunc, ncol );

			if ( def )
			{
				const auto team_label = ( team == 3 ) ? "CT" : "T";
				auto team_col = tokens::col_text_dim;
				team_col.a = static_cast< std::uint8_t >( 180.0f * fade_alpha );
				dl.text( card.x + 6.0f, card.y + 4.0f, team_label, team_col );
			}

			if ( hovered && input.mouse_clicked )
			{
				skins_ui.browsing_agent_team = team;
				request_page( skins_page::browser, 0 );
			}
		}

		static inline void draw_weapon_card( const xui::rect& card, const features::changer::econ_item_system::item_def* def, float fade_alpha )
		{
			auto& econ = features::changer::g_econ_item_system;
			auto& dl = xui::draw::current( );
			const auto& input = xui::ctx( ).input;

			const auto applied_it = skin_map( ).find( def->def_index );
			const auto is_skinned = ( applied_it != skin_map( ).end( ) );

			const auto pk = ( is_skinned && applied_it->second.paint_kit_id != 0 ) ? econ.find_paint_kit( applied_it->second.paint_kit_id ) : nullptr;
			const auto rarity = pk ? econ.combined_rarity( def->def_index, applied_it->second.paint_kit_id ) : 0;
			const auto rarity_col = k_rarity_colors[ std::clamp( rarity, 0, 7 ) ];

			const auto image_h = std::floor( card.h * k_image_h_ratio );

			const auto hovered = !xui::ctx( ).overlay_blocking( ) && input.in_rect( card );
			const auto hover_anim = xui::anim::lerp( xui::fnv1a( "wcard" ) + static_cast< std::uintptr_t >( def->def_index ), hovered ? 1.0f : 0.0f, 14.0f );

			auto card_bg = tokens::col_card;
			card_bg = xui::lerp( card_bg, xui::lighten( card_bg, 1.4f ), hover_anim * 0.5f );
			card_bg.a = static_cast< std::uint8_t >( card_bg.a * fade_alpha );
			dl.rect_filled( card.x, card.y, card.w, card.h, card_bg, xdraw::corner_radius{ tokens::btn_rounding } );

			if ( is_skinned )
			{
				if ( pk )
				{
					auto bcol = rarity_col;
					bcol.a = static_cast< std::uint8_t >( ( 100.0f + 80.0f * hover_anim ) * fade_alpha );
					dl.rect( card.x, card.y, card.w, card.h, bcol, xdraw::corner_radius{ tokens::btn_rounding }, 1.0f );
				}
				else
				{
					auto bcol = tokens::col_accent;
					bcol.a = static_cast< std::uint8_t >( bcol.a * fade_alpha );
					dl.rect( card.x, card.y, card.w, card.h, bcol, xdraw::corner_radius{ tokens::btn_rounding }, 1.0f );
				}
			}

			const auto img = ( is_skinned && pk ) ? econ.get_skin_image( def->def_index, applied_it->second.paint_kit_id ) : econ.get_skin_image( def->image_inventory );
			if ( img )
			{
				const auto target_h = image_h - 12.0f;
				const auto aspect = static_cast< float >( img->width ) / static_cast< float >( img->height );
				auto iw = target_h * aspect;
				auto ih = target_h;

				if ( iw > card.w - 12.0f )
				{
					iw = card.w - 12.0f;
					ih = iw / aspect;
				}

				const auto ix = std::floor( card.x + ( card.w - iw ) * 0.5f );
				const auto iy = std::floor( card.y + ( image_h - ih ) * 0.5f );
				const auto tint = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				dl.image( ix, iy, iw, ih, img->srv.Get( ), tint );
			}
			else if ( def->category == features::changer::econ_item_system::item_category::glove && !img )
			{
				for ( const auto& skin : econ.skins( ) )
				{
					if ( skin.def_index != def->def_index )
					{
						continue;
					}

					const auto fallback_img = econ.get_skin_image( def->def_index, skin.paint_kit_id );
					if ( fallback_img )
					{
						const auto target_h = image_h - 12.0f;
						const auto aspect = static_cast< float >( fallback_img->width ) / static_cast< float >( fallback_img->height );
						auto iw = target_h * aspect;
						auto ih = target_h;

						if ( iw > card.w - 12.0f )
						{
							iw = card.w - 12.0f;
							ih = iw / aspect;
						}

						const auto ix = std::floor( card.x + ( card.w - iw ) * 0.5f );
						const auto iy = std::floor( card.y + ( image_h - ih ) * 0.5f );
						const auto tint = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 150.0f * fade_alpha ) };

						dl.image( ix, iy, iw, ih, fallback_img->srv.Get( ), tint );
						break;
					}
				}
			}

			if ( pk )
			{
				auto bar = rarity_col;
				bar.a = static_cast< std::uint8_t >( bar.a * fade_alpha );
				dl.rect_filled( card.x + 6.0f, card.y + image_h, card.w - 12.0f, k_rarity_bar_h, bar );
			}

			const auto name_y = card.y + image_h + k_rarity_bar_h + 4.0f;

			if ( pk )
			{
				auto skin_col = rarity_col;
				skin_col.a = static_cast< std::uint8_t >( 200.0f * fade_alpha );

				const auto skin_trunc = xui::truncate( pk->localized_name, card.w - 12.0f );
				const auto [sw, sh] = xdraw::measure_text( skin_trunc );
				dl.text( std::floor( card.x + ( card.w - sw ) * 0.5f ), std::floor( name_y ), skin_trunc, skin_col );
			}
			else
			{
				auto ncol = xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );
				ncol.a = static_cast< std::uint8_t >( ncol.a * fade_alpha );

				const auto ntrunc = xui::truncate( def->localized_name, card.w - 12.0f );
				const auto [nw2, nh2] = xdraw::measure_text( ntrunc );
				dl.text( std::floor( card.x + ( card.w - nw2 ) * 0.5f ), std::floor( name_y ), ntrunc, ncol );
			}

			if ( is_skinned && !pk )
			{
				constexpr auto badge{ 14.0f };
				const auto bx = card.right( ) - badge - 4.0f;
				const auto by = card.y + 4.0f;

				auto badge_bg = tokens::col_accent;
				badge_bg.a = static_cast< std::uint8_t >( badge_bg.a * fade_alpha );
				dl.rect_filled( bx, by, badge, badge, badge_bg, xdraw::corner_radius{ badge * 0.5f } );

				const auto cx = bx + badge * 0.5f;
				const auto cy = by + badge * 0.5f;
				const auto check = xdraw::color{ tokens::col_dark.r, tokens::col_dark.g, tokens::col_dark.b, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				const std::array<float, 6> pts
				{
					cx - badge * 0.20f, cy,
					cx - badge * 0.05f, cy + badge * 0.18f,
					cx + badge * 0.25f, cy - badge * 0.18f
				};

				dl.polyline( pts, check, false, 1.5f );
			}

			if ( !hovered )
			{
				return;
			}

			if ( input.mouse_clicked )
			{
				if ( def->category == features::changer::econ_item_system::item_category::agent )
				{
					if ( is_skinned )
					{
						skin_map( ).erase( def->def_index );
					}
					else
					{
						for ( const auto* a : econ.agents( ) )
						{
							skin_map( ).erase( a->def_index );
						}

						auto& a = skin_map( )[ def->def_index ];
						a.paint_kit_id = 0;
						a.wear = 0.0f;
						a.seed = 0;
						a.stattrak = false;
					}
				}
				else
				{
					request_page( skins_page::browser, def->def_index );
				}

				return;
			}

			if ( input.rmb_clicked && is_skinned && pk )
			{
				const auto ctx_id = xui::fnv1a( "skin_ctx" ) ^ static_cast< std::uintptr_t >( def->def_index );

				if ( xui::overlays::is_open( ctx_id ) )
				{
					xui::overlays::close( ctx_id );
				}
				else
				{
					const auto anchor = xui::rect{ input.mouse_x, input.mouse_y, 0.0f, 0.0f };
					xui::overlays::add( std::make_unique<skin_context_overlay>( ctx_id, anchor, def->def_index, def->localized_name, pk->localized_name ) );
				}
			}
		}

		static inline void draw_agent_tile( const xui::rect& card, const features::changer::econ_item_system::item_def* def, bool is_equipped, float fade_alpha )
		{
			auto& econ = features::changer::g_econ_item_system;
			auto& dl = xui::draw::current( );
			const auto& input = xui::ctx( ).input;

			const auto image_h = std::floor( card.h * k_image_h_ratio );

			const auto hovered = !xui::ctx( ).overlay_blocking( ) && input.in_rect( card );
			const auto hover_anim = xui::anim::lerp( xui::fnv1a( "atile" ) + static_cast< std::uintptr_t >( def->def_index ), hovered ? 1.0f : 0.0f, 14.0f );

			auto card_bg = tokens::col_card;
			card_bg = xui::lerp( card_bg, xui::lighten( card_bg, 1.4f ), hover_anim * 0.5f );
			card_bg.a = static_cast< std::uint8_t >( card_bg.a * fade_alpha );
			dl.rect_filled( card.x, card.y, card.w, card.h, card_bg, xdraw::corner_radius{ tokens::btn_rounding } );

			if ( is_equipped )
			{
				auto bcol = tokens::col_accent;
				bcol.a = static_cast< std::uint8_t >( bcol.a * fade_alpha );
				dl.rect( card.x, card.y, card.w, card.h, bcol, xdraw::corner_radius{ tokens::btn_rounding }, 1.5f );
			}

			const auto img = econ.get_skin_image( def->image_inventory );
			if ( img )
			{
				const auto target_h = image_h - 12.0f;
				const auto aspect = static_cast< float >( img->width ) / static_cast< float >( img->height );
				auto iw = target_h * aspect;
				auto ih = target_h;

				if ( iw > card.w - 12.0f )
				{
					iw = card.w - 12.0f;
					ih = iw / aspect;
				}

				const auto ix = std::floor( card.x + ( card.w - iw ) * 0.5f );
				const auto iy = std::floor( card.y + ( image_h - ih ) * 0.5f );
				const auto tint = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				dl.image( ix, iy, iw, ih, img->srv.Get( ), tint );
			}

			const auto name_y = card.y + image_h + k_rarity_bar_h + 4.0f;

			auto ncol = xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );
			ncol.a = static_cast< std::uint8_t >( ncol.a * fade_alpha );

			const auto ntrunc = xui::truncate( def->localized_name, card.w - 12.0f );
			const auto [nw, nh] = xdraw::measure_text( ntrunc );
			dl.text( std::floor( card.x + ( card.w - nw ) * 0.5f ), std::floor( name_y ), ntrunc, ncol );

			if ( is_equipped )
			{
				constexpr auto badge{ 14.0f };
				const auto bx = card.right( ) - badge - 4.0f;
				const auto by = card.y + 4.0f;

				auto badge_bg = tokens::col_accent;
				badge_bg.a = static_cast< std::uint8_t >( badge_bg.a * fade_alpha );
				dl.rect_filled( bx, by, badge, badge, badge_bg, xdraw::corner_radius{ badge * 0.5f } );

				const auto cx = bx + badge * 0.5f;
				const auto cy = by + badge * 0.5f;
				const auto check = xdraw::color{ tokens::col_dark.r, tokens::col_dark.g, tokens::col_dark.b, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				const std::array<float, 6> pts
				{
					cx - badge * 0.20f, cy,
					cx - badge * 0.05f, cy + badge * 0.18f,
					cx + badge * 0.25f, cy - badge * 0.18f
				};

				dl.polyline( pts, check, false, 1.5f );
			}

			if ( hovered && input.mouse_clicked )
			{
				auto& target = ( skins_ui.browsing_agent_team == 3 ) ? settings::g_changer.agents.ct_def : settings::g_changer.agents.t_def;

				if ( is_equipped )
				{
					target = 0;
				}
				else
				{
					target = def->def_index;
				}

				request_page( skins_page::grid );
			}
		}

		static inline void draw_skin_tile( const xui::rect& card, const features::changer::econ_item_system::paint_kit* pk, const features::changer::econ_item_system::item_def* weapon, int current_kit_id, float fade_alpha )
		{
			auto& econ = features::changer::g_econ_item_system;
			auto& dl = xui::draw::current( );
			const auto& input = xui::ctx( ).input;

			const auto rarity = weapon ? econ.combined_rarity( weapon->def_index, pk->id ) : pk->rarity;
			const auto rarity_col = k_rarity_colors[ std::clamp( rarity, 0, 7 ) ];

			const auto image_h = std::floor( card.h * k_image_h_ratio );

			const auto is_equipped = ( pk->id == current_kit_id );
			const auto hovered = !xui::ctx( ).overlay_blocking( ) && input.in_rect( card );
			const auto hover_anim = xui::anim::lerp( xui::fnv1a( "scard" ) + static_cast< std::uintptr_t >( pk->id ), hovered ? 1.0f : 0.0f, 14.0f );

			auto card_bg = tokens::col_card;
			card_bg = xui::lerp( card_bg, xui::lighten( card_bg, 1.4f ), hover_anim * 0.5f );
			card_bg.a = static_cast< std::uint8_t >( card_bg.a * fade_alpha );
			dl.rect_filled( card.x, card.y, card.w, card.h, card_bg, xdraw::corner_radius{ tokens::btn_rounding } );

			if ( is_equipped )
			{
				auto bcol = tokens::col_accent;
				bcol.a = static_cast< std::uint8_t >( bcol.a * fade_alpha );
				dl.rect( card.x, card.y, card.w, card.h, bcol, xdraw::corner_radius{ tokens::btn_rounding }, 1.5f );
			}

			const auto img = weapon ? econ.get_skin_image( weapon->def_index, pk->id ) : nullptr;
			if ( img )
			{
				const auto target_h = image_h - 12.0f;
				const auto aspect = static_cast< float >( img->width ) / static_cast< float >( img->height );
				auto iw = target_h * aspect;
				auto ih = target_h;

				if ( iw > card.w - 12.0f )
				{
					iw = card.w - 12.0f;
					ih = iw / aspect;
				}

				const auto ix = std::floor( card.x + ( card.w - iw ) * 0.5f );
				const auto iy = std::floor( card.y + ( image_h - ih ) * 0.5f );
				const auto tint = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				dl.image( ix, iy, iw, ih, img->srv.Get( ), tint );
			}
			else
			{
				auto ph_col = xui::darken( tokens::col_card, 0.7f );
				ph_col.a = static_cast< std::uint8_t >( 100.0f * fade_alpha );
				dl.rect_filled( card.x + 6.0f, card.y + 6.0f, card.w - 12.0f, image_h - 12.0f, ph_col, xdraw::corner_radius{ 4.0f } );
			}

			auto bar = rarity_col;
			bar.a = static_cast< std::uint8_t >( bar.a * fade_alpha );
			dl.rect_filled( card.x + 6.0f, card.y + image_h, card.w - 12.0f, k_rarity_bar_h, bar );

			auto name_col = xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );
			name_col.a = static_cast< std::uint8_t >( name_col.a * fade_alpha );

			const auto name_trunc = xui::truncate( pk->localized_name, card.w - 12.0f );
			const auto [nw, nh] = xdraw::measure_text( name_trunc );
			const auto name_y = card.y + image_h + k_rarity_bar_h + 4.0f;
			dl.text( std::floor( card.x + ( card.w - nw ) * 0.5f ), std::floor( name_y ), name_trunc, name_col );

			if ( is_equipped )
			{
				constexpr auto badge{ 14.0f };
				const auto bx = card.right( ) - badge - 4.0f;
				const auto by = card.y + 4.0f;

				auto badge_bg = tokens::col_accent;
				badge_bg.a = static_cast< std::uint8_t >( badge_bg.a * fade_alpha );
				dl.rect_filled( bx, by, badge, badge, badge_bg, xdraw::corner_radius{ badge * 0.5f } );

				const auto cx = bx + badge * 0.5f;
				const auto cy = by + badge * 0.5f;
				const auto check = xdraw::color{ tokens::col_dark.r, tokens::col_dark.g, tokens::col_dark.b, static_cast< std::uint8_t >( 255.0f * fade_alpha ) };

				const std::array<float, 6> pts
				{
					cx - badge * 0.20f, cy,
					cx - badge * 0.05f, cy + badge * 0.18f,
					cx + badge * 0.25f, cy - badge * 0.18f
				};

				dl.polyline( pts, check, false, 1.5f );
			}

			if ( hovered && input.mouse_clicked )
			{
				if ( is_equipped )
				{
					const auto browsing_def = econ.find_def( skins_ui.browsing_def );
					if ( !browsing_def || browsing_def->category != features::changer::econ_item_system::item_category::agent )
					{
						return;
					}

					skin_map( ).erase( skins_ui.browsing_def );
					request_page( skins_page::grid );
					return;
				}
				else
				{
					const auto browsing_def = econ.find_def( skins_ui.browsing_def );
					if ( browsing_def )
					{
						if ( browsing_def->category == features::changer::econ_item_system::item_category::knife )
						{
							for ( const auto* k : econ.knives( ) )
							{
								skin_map( ).erase( k->def_index );
							}
						}
						else if ( browsing_def->category == features::changer::econ_item_system::item_category::glove )
						{
							for ( const auto* g : econ.gloves( ) )
							{
								skin_map( ).erase( g->def_index );
							}
						}
					}

					auto& a = skin_map( )[ skins_ui.browsing_def ];
					a.paint_kit_id = pk->id;
					a.wear = 0.01f;
					a.seed = 0;
					a.stattrak = false;
				}

				request_page( skins_page::grid );
			}
		}

	} // namespace detail

	void menu::draw_skins( float group_w ) const
	{
		static auto last_subtab{ -1 };
		if ( this->m_subtab != last_subtab )
		{
			last_subtab = this->m_subtab;
			detail::skins_ui.current = detail::skins_page::grid;
			detail::skins_ui.target = detail::skins_page::grid;
			detail::skins_ui.fade = 1.0f;
			detail::skins_ui.browsing_agent_team = 0;
			detail::skins_ui.search_buf.clear( );
		}

		auto& econ = features::changer::g_econ_item_system;
		auto& dl = xui::draw::current( );
		const auto& s = xui::ctx( ).style;
		const auto& input = xui::ctx( ).input;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto body_h = this->m_h - tokens::gap * 2.0f - tokens::subtab_bar_h - tokens::gap;

		const auto dt = xdraw::delta_time( );
		const auto fade_target = ( detail::skins_ui.current == detail::skins_ui.target ) ? 1.0f : 0.0f;
		detail::skins_ui.fade += ( fade_target - detail::skins_ui.fade ) * std::min( 14.0f * dt, 1.0f );

		if ( detail::skins_ui.fade < 0.05f && detail::skins_ui.current != detail::skins_ui.target )
		{
			detail::skins_ui.current = detail::skins_ui.target;
			detail::skins_ui.fade = 0.0f;
		}

		const auto fade_alpha = xui::ease::out_cubic( std::clamp( detail::skins_ui.fade, 0.0f, 1.0f ) );

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( detail::skins_ui.current == detail::skins_page::grid )
		{
			if ( !xui::begin_child( "##skins_grid", content_w, body_h, true ) )
			{
				return;
			}

			const auto win = xui::layout::current_window( );
			const auto inner_w = win->bounds.w - s.window_pad_x * 2.0f;

			const auto total_gap = ( detail::k_columns - 1 ) * detail::k_card_gap;
			const auto card_w = std::floor( ( inner_w - total_gap ) / static_cast< float >( detail::k_columns ) );
			const auto card_h = std::floor( card_w * ( detail::k_card_h_ref / detail::k_card_w_ref ) );

			const auto grid_w = card_w * detail::k_columns + total_gap;
			const auto offset_x = std::max( 0.0f, ( inner_w - grid_w ) * 0.5f );

			if ( this->m_subtab == 3 )
			{
				const auto base_x = win->bounds.x + s.window_pad_x + offset_x;
				const auto base_y = win->bounds.y + s.window_pad_y - win->scroll_y;

				const auto ct_card = xui::rect{ base_x, base_y, card_w, card_h };
				const auto t_card = xui::rect{ base_x + card_w + detail::k_card_gap, base_y, card_w, card_h };

				detail::draw_agent_team_card( ct_card, 3, fade_alpha );
				detail::draw_agent_team_card( t_card, 2, fade_alpha );

				xui::layout::item( inner_w, card_h );
				xui::end_child( );
				return;
			}

			const auto& weapons = detail::select_weapons( this->m_subtab );

			std::unordered_set<std::int16_t> defs_with_skins;
			for ( const auto& skin : econ.skins( ) )
			{
				defs_with_skins.insert( skin.def_index );
			}

			std::vector<const features::changer::econ_item_system::item_def*> items;
			items.reserve( weapons.size( ) );

			for ( const auto* w : weapons )
			{
				if ( ( w->category == features::changer::econ_item_system::item_category::knife || w->category == features::changer::econ_item_system::item_category::glove ) && !defs_with_skins.contains( w->def_index ) )
				{
					continue;
				}

				if ( !w->image_inventory.empty( ) || defs_with_skins.contains( w->def_index ) )
				{
					items.push_back( w );
				}
			}

			const auto rows = ( static_cast< int >( items.size( ) ) + detail::k_columns - 1 ) / detail::k_columns;
			const auto total_h = rows * card_h + ( rows > 0 ? ( rows - 1 ) * detail::k_card_gap : 0.0f );

			const auto base_x = win->bounds.x + s.window_pad_x + offset_x;
			const auto base_y = win->bounds.y + s.window_pad_y - win->scroll_y;

			xui::layout::item( inner_w, total_h );

			for ( auto i = 0; i < static_cast< int >( items.size( ) ); ++i )
			{
				const auto col = i % detail::k_columns;
				const auto row = i / detail::k_columns;

				const auto cx = std::floor( base_x + col * ( card_w + detail::k_card_gap ) );
				const auto cy = std::floor( base_y + row * ( card_h + detail::k_card_gap ) );

				if ( cy + card_h < win->bounds.y || cy > win->bounds.bottom( ) )
				{
					continue;
				}

				const auto card = xui::rect{ cx, cy, card_w, card_h };
				detail::draw_weapon_card( card, items[ i ], fade_alpha );
			}

			xui::end_child( );
		}
		else
		{
			if ( !xui::begin_child( "##skins_browser", content_w, body_h, true ) )
			{
				return;
			}

			const auto win = xui::layout::current_window( );
			const auto inner_w = win->bounds.w - s.window_pad_x * 2.0f;

			const auto total_gap = ( detail::k_columns - 1 ) * detail::k_card_gap;
			const auto card_w = std::floor( ( inner_w - total_gap ) / static_cast< float >( detail::k_columns ) );
			const auto card_h = std::floor( card_w * ( detail::k_card_h_ref / detail::k_card_w_ref ) );

			const auto grid_w = card_w * detail::k_columns + total_gap;
			const auto offset_x = std::max( 0.0f, ( inner_w - grid_w ) * 0.5f );

			constexpr auto bar_h{ 26.0f };
			const auto bar_x = win->bounds.x + s.window_pad_x;
			const auto bar_y = win->bounds.y + s.window_pad_y - win->scroll_y;

			const auto back_w{ 60.0f };
			const auto back_rect = xui::rect{ bar_x, bar_y, back_w, bar_h };
			const auto back_hovered = !xui::ctx( ).overlay_blocking( ) && input.in_rect( back_rect );
			const auto back_hover = xui::anim::lerp( xui::fnv1a( "skin_back" ), back_hovered ? 1.0f : 0.0f, 14.0f );

			if ( back_hovered && input.mouse_clicked )
			{
				detail::request_page( detail::skins_page::grid );
				detail::skins_ui.search_buf.clear( );
			}

			auto back_bg = xui::lerp( s.button_bg, s.button_hovered, back_hover );
			back_bg.a = static_cast< std::uint8_t >( back_bg.a * fade_alpha );
			dl.rect_filled( back_rect.x, back_rect.y, back_rect.w, back_rect.h, back_bg, xdraw::corner_radius{ s.button_rounding } );

			const auto [bw, bh] = xdraw::measure_text( "back" );
			auto back_text = xui::lerp( s.text_dim, s.text, back_hover );
			back_text.a = static_cast< std::uint8_t >( back_text.a * fade_alpha );
			dl.text( back_rect.x + ( back_rect.w - bw ) * 0.5f, back_rect.y + ( back_rect.h - bh ) * 0.5f, "back", back_text );

			xui::layout::set_cursor( back_rect.right( ) + 6.0f - win->bounds.x, bar_y - win->bounds.y );
			xui::text_input( "##skin_search", detail::skins_ui.search_buf, 64, "search..." );

			const auto grid_top_y = bar_y + bar_h + 12.0f;
			const auto base_x = win->bounds.x + s.window_pad_x + offset_x;

			if ( detail::skins_ui.browsing_agent_team != 0 )
			{
				std::string search_lower = detail::skins_ui.search_buf;
				for ( auto& c : search_lower )
				{
					c = static_cast< char >( std::tolower( c ) );
				}

				std::vector<const features::changer::econ_item_system::item_def*> agent_items;
				for ( const auto* a : econ.agents( ) )
				{
					const auto agent_team = a->team( );
					if ( agent_team != 0 && agent_team != detail::skins_ui.browsing_agent_team )
					{
						continue;
					}

					if ( !search_lower.empty( ) )
					{
						std::string n = a->localized_name;
						for ( auto& c : n )
						{
							c = static_cast< char >( std::tolower( c ) );
						}

						if ( n.find( search_lower ) == std::string::npos )
						{
							continue;
						}
					}

					agent_items.push_back( a );
				}

				const auto rows = ( static_cast< int >( agent_items.size( ) ) + detail::k_columns - 1 ) / detail::k_columns;
				const auto grid_h = rows * card_h + ( rows > 0 ? ( rows - 1 ) * detail::k_card_gap : 0.0f );

				xui::layout::set_cursor( s.window_pad_x, s.window_pad_y );
				xui::layout::item( inner_w, ( bar_h + 12.0f ) + grid_h );

				const auto& sel = settings::g_changer.agents;
				const auto current_agent = ( detail::skins_ui.browsing_agent_team == 3 ) ? sel.ct_def : sel.t_def;

				for ( auto i = 0; i < static_cast< int >( agent_items.size( ) ); ++i )
				{
					const auto col = i % detail::k_columns;
					const auto row = i / detail::k_columns;

					const auto cx = std::floor( base_x + col * ( card_w + detail::k_card_gap ) );
					const auto cy = std::floor( grid_top_y + row * ( card_h + detail::k_card_gap ) );

					if ( cy + card_h < win->bounds.y || cy > win->bounds.bottom( ) )
					{
						continue;
					}

					const auto card = xui::rect{ cx, cy, card_w, card_h };
					detail::draw_agent_tile( card, agent_items[ i ], agent_items[ i ]->def_index == current_agent, fade_alpha );
				}

				win->content_h = ( s.window_pad_y + bar_h + 12.0f + grid_h ) - win->scroll_y;
				xui::end_child( );
				return;
			}

			const auto weapon = econ.find_def( detail::skins_ui.browsing_def );

			std::unordered_set<int> valid_kits;
			for ( const auto& skin : econ.skins( ) )
			{
				if ( skin.def_index == detail::skins_ui.browsing_def )
				{
					valid_kits.insert( skin.paint_kit_id );
				}
			}

			std::string search_lower = detail::skins_ui.search_buf;
			for ( auto& c : search_lower )
			{
				c = static_cast< char >( std::tolower( c ) );
			}

			std::vector<const features::changer::econ_item_system::paint_kit*> kits;
			kits.reserve( valid_kits.size( ) );

			for ( const auto& pk : econ.paint_kits( ) )
			{
				if ( valid_kits.find( pk.id ) == valid_kits.end( ) )
				{
					continue;
				}

				if ( !search_lower.empty( ) )
				{
					std::string n = pk.localized_name;
					for ( auto& c : n )
					{
						c = static_cast< char >( std::tolower( c ) );
					}

					if ( n.find( search_lower ) == std::string::npos )
					{
						continue;
					}
				}

				kits.push_back( &pk );
			}

			const auto rows = ( static_cast< int >( kits.size( ) ) + detail::k_columns - 1 ) / detail::k_columns;
			const auto grid_h = rows * card_h + ( rows > 0 ? ( rows - 1 ) * detail::k_card_gap : 0.0f );

			const auto reserved_h = ( bar_h + 12.0f ) + grid_h;
			xui::layout::set_cursor( s.window_pad_x, s.window_pad_y );
			xui::layout::item( inner_w, reserved_h );

			const auto applied_it = detail::skin_map( ).find( detail::skins_ui.browsing_def );
			const auto current_kit = ( applied_it != detail::skin_map( ).end( ) ) ? applied_it->second.paint_kit_id : -1;

			for ( auto i = 0; i < static_cast< int >( kits.size( ) ); ++i )
			{
				const auto col = i % detail::k_columns;
				const auto row = i / detail::k_columns;

				const auto cx = std::floor( base_x + col * ( card_w + detail::k_card_gap ) );
				const auto cy = std::floor( grid_top_y + row * ( card_h + detail::k_card_gap ) );

				if ( cy + card_h < win->bounds.y || cy > win->bounds.bottom( ) )
				{
					continue;
				}

				const auto card = xui::rect{ cx, cy, card_w, card_h };
				detail::draw_skin_tile( card, kits[ i ], weapon, current_kit, fade_alpha );
			}

			const auto total_content_h = s.window_pad_y + bar_h + 12.0f + grid_h;
			win->content_h = total_content_h - win->scroll_y;

			xui::end_child( );
		}
	}

} // namespace rendering