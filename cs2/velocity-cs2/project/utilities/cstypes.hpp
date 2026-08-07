#pragma once

namespace cstypes {

	constexpr auto tick_interval{ 0.015625f };

	[[nodiscard]] static int time_to_ticks( float time ) noexcept { return static_cast< int >( 0.5f + time / cstypes::tick_interval ); }
	[[nodiscard]] static float ticks_to_time( int ticks ) noexcept { return cstypes::tick_interval * static_cast< float >( ticks ); }

	namespace bone_ids {

		constexpr auto head{ 7u };
		constexpr auto neck{ 6u };
		constexpr auto spine_1{ 2u };
		constexpr auto spine_2{ 3u };
		constexpr auto spine_3{ 4u };
		constexpr auto spine_4{ 23u };
		constexpr auto pelvis{ 1u };

		constexpr auto left_clavicle{ 8u };
		constexpr auto left_shoulder{ 9u };
		constexpr auto left_elbow{ 10u };
		constexpr auto left_hand{ 11u };

		constexpr auto right_clavicle{ 12u };
		constexpr auto right_shoulder{ 13u };
		constexpr auto right_elbow{ 14u };
		constexpr auto right_hand{ 15u };

		constexpr auto left_hip{ 17u };
		constexpr auto left_knee{ 18u };
		constexpr auto left_foot{ 19u };

		constexpr auto right_hip{ 20u };
		constexpr auto right_knee{ 21u };
		constexpr auto right_foot{ 22u };

	} // namespace bone_ids

	namespace hit_groups {

		constexpr auto generic{ 0u };
		constexpr auto head{ 1u };
		constexpr auto chest{ 2u };
		constexpr auto stomach{ 3u };
		constexpr auto leftarm{ 4u };
		constexpr auto rightarm{ 5u };
		constexpr auto leftleg{ 6u };
		constexpr auto rightleg{ 7u };
		constexpr auto neck{ 8u };
		constexpr auto gear{ 10u };

	} // namespace hit_groups

	namespace weapon_type {

		constexpr auto knife{ 0u };
		constexpr auto pistol{ 1u };
		constexpr auto smg{ 2u };
		constexpr auto rifle{ 3u };
		constexpr auto shotgun{ 4u };
		constexpr auto sniper{ 5u };
		constexpr auto lmg{ 6u };
		constexpr auto c4{ 7u };
		constexpr auto taser{ 8u };
		constexpr auto grenade{ 9u };
		constexpr auto equipment{ 10u };
		constexpr auto healthshot{ 11u };

	} // namespace weapon_type

	namespace command_buttons {

		constexpr auto in_attack{ 1ull << 0 };
		constexpr auto in_jump{ 1ull << 1 };
		constexpr auto in_duck{ 1ull << 2 };
		constexpr auto in_forward{ 1ull << 3 };
		constexpr auto in_back{ 1ull << 4 };
		constexpr auto in_use{ 1ull << 5 };
		constexpr auto in_left{ 1ull << 7 };
		constexpr auto in_right{ 1ull << 8 };
		constexpr auto in_moveleft{ 1ull << 9 };
		constexpr auto in_moveright{ 1ull << 10 };
		constexpr auto in_second_attack{ 1ull << 11 };
		constexpr auto in_reload{ 1ull << 13 };
		constexpr auto in_sprint{ 1ull << 16 };
		constexpr auto in_joyautosprint{ 1ull << 17 };
		constexpr auto in_showscores{ 1ull << 33 };
		constexpr auto in_zoom{ 1ull << 34 };
		constexpr auto in_lookatweapon{ 1ull << 35 };

	} // namespace command_buttons

	namespace move_type {

		constexpr std::uint8_t none{ 0 };
		constexpr std::uint8_t obsolete{ 1 };
		constexpr std::uint8_t walk{ 2 };
		constexpr std::uint8_t fly{ 3 };
		constexpr std::uint8_t fly_gravity{ 4 };
		constexpr std::uint8_t vphysics{ 5 };
		constexpr std::uint8_t push{ 6 };
		constexpr std::uint8_t noclip{ 7 };
		constexpr std::uint8_t observer{ 8 };
		constexpr std::uint8_t ladder{ 9 };
		constexpr std::uint8_t custom{ 10 };
		constexpr std::uint8_t last{ 11 };

	} // namespace move_type

	namespace entity_flags {

		constexpr auto on_ground{ 1u << 0 };
		constexpr auto ducking{ 1u << 1 };
		constexpr auto water_jump{ 1u << 3 };
		constexpr auto on_train{ 1u << 4 };
		constexpr auto in_rain{ 1u << 5 };
		constexpr auto frozen{ 1u << 6 };
		constexpr auto at_controls{ 1u << 7 };
		constexpr auto client{ 1u << 8 };
		constexpr auto fake_client{ 1u << 9 };
		constexpr auto in_water{ 1u << 10 };
		constexpr auto hide_hud_scope{ 1u << 11 };

	} // namespace entity_flags

	namespace item_definition_index {

		constexpr std::uint16_t weapon_none{ 0 };
		constexpr std::uint16_t weapon_desert_eagle{ 1 };
		constexpr std::uint16_t weapon_dual_berettas{ 2 };
		constexpr std::uint16_t weapon_five_seven{ 3 };
		constexpr std::uint16_t weapon_glock_18{ 4 };
		constexpr std::uint16_t weapon_ak_47{ 7 };
		constexpr std::uint16_t weapon_aug{ 8 };
		constexpr std::uint16_t weapon_awp{ 9 };
		constexpr std::uint16_t weapon_famas{ 10 };
		constexpr std::uint16_t weapon_g3sg1{ 11 };
		constexpr std::uint16_t weapon_galil_ar{ 13 };
		constexpr std::uint16_t weapon_m249{ 14 };
		constexpr std::uint16_t weapon_m4a4{ 16 };
		constexpr std::uint16_t weapon_mac_10{ 17 };
		constexpr std::uint16_t weapon_p90{ 19 };
		constexpr std::uint16_t weapon_repulsor_device{ 20 };
		constexpr std::uint16_t weapon_mp5_sd{ 23 };
		constexpr std::uint16_t weapon_ump_45{ 24 };
		constexpr std::uint16_t weapon_xm1014{ 25 };
		constexpr std::uint16_t weapon_pp_bizon{ 26 };
		constexpr std::uint16_t weapon_mag_7{ 27 };
		constexpr std::uint16_t weapon_negev{ 28 };
		constexpr std::uint16_t weapon_sawed_off{ 29 };
		constexpr std::uint16_t weapon_tec_9{ 30 };
		constexpr std::uint16_t weapon_zeus_x27{ 31 };
		constexpr std::uint16_t weapon_p2000{ 32 };
		constexpr std::uint16_t weapon_mp7{ 33 };
		constexpr std::uint16_t weapon_mp9{ 34 };
		constexpr std::uint16_t weapon_nova{ 35 };
		constexpr std::uint16_t weapon_p250{ 36 };
		constexpr std::uint16_t weapon_riot_shield{ 37 };
		constexpr std::uint16_t weapon_scar_20{ 38 };
		constexpr std::uint16_t weapon_sg_553{ 39 };
		constexpr std::uint16_t weapon_ssg_08{ 40 };
		constexpr std::uint16_t weapon_knife0{ 41 };
		constexpr std::uint16_t weapon_knife1{ 42 };
		constexpr std::uint16_t weapon_flashbang{ 43 };
		constexpr std::uint16_t weapon_high_explosive_grenade{ 44 };
		constexpr std::uint16_t weapon_smoke_grenade{ 45 };
		constexpr std::uint16_t weapon_molotov{ 46 };
		constexpr std::uint16_t weapon_decoy_grenade{ 47 };
		constexpr std::uint16_t weapon_incendiary_grenade{ 48 };
		constexpr std::uint16_t weapon_c4_explosive{ 49 };
		constexpr std::uint16_t weapon_kevlar_vest{ 50 };
		constexpr std::uint16_t weapon_kevlar_and_helmet{ 51 };
		constexpr std::uint16_t weapon_heavy_assault_suit{ 52 };
		constexpr std::uint16_t weapon_no_localized_name0{ 54 };
		constexpr std::uint16_t weapon_defuse_kit{ 55 };
		constexpr std::uint16_t weapon_rescue_kit{ 56 };
		constexpr std::uint16_t weapon_medi_shot{ 57 };
		constexpr std::uint16_t weapon_music_kit{ 58 };
		constexpr std::uint16_t weapon_knife2{ 59 };
		constexpr std::uint16_t weapon_m4a1_s{ 60 };
		constexpr std::uint16_t weapon_usp_s{ 61 };
		constexpr std::uint16_t weapon_trade_up_contract{ 62 };
		constexpr std::uint16_t weapon_cz75_auto{ 63 };
		constexpr std::uint16_t weapon_r8_revolver{ 64 };
		constexpr std::uint16_t weapon_tactical_awareness_grenade{ 68 };
		constexpr std::uint16_t weapon_bare_hands{ 69 };
		constexpr std::uint16_t weapon_breach_charge{ 70 };
		constexpr std::uint16_t weapon_tablet{ 72 };
		constexpr std::uint16_t weapon_knife3{ 74 };
		constexpr std::uint16_t weapon_axe{ 75 };
		constexpr std::uint16_t weapon_hammer{ 76 };
		constexpr std::uint16_t weapon_wrench{ 78 };
		constexpr std::uint16_t weapon_spectral_shiv{ 80 };
		constexpr std::uint16_t weapon_fire_bomb{ 81 };
		constexpr std::uint16_t weapon_diversion_device{ 82 };
		constexpr std::uint16_t weapon_frag_grenade{ 83 };
		constexpr std::uint16_t weapon_snowball{ 84 };
		constexpr std::uint16_t weapon_bump_mine{ 85 };

		constexpr std::uint16_t weapon_bayonet{ 500 };
		constexpr std::uint16_t weapon_classic_knife{ 503 };
		constexpr std::uint16_t weapon_flip_knife{ 505 };
		constexpr std::uint16_t weapon_gut_knife{ 506 };
		constexpr std::uint16_t weapon_karambit{ 507 };
		constexpr std::uint16_t weapon_m9_bayonet{ 508 };
		constexpr std::uint16_t weapon_huntsman_knife{ 509 };
		constexpr std::uint16_t weapon_falchion_knife{ 512 };
		constexpr std::uint16_t weapon_bowie_knife{ 514 };
		constexpr std::uint16_t weapon_butterfly_knife{ 515 };
		constexpr std::uint16_t weapon_shadow_daggers{ 516 };
		constexpr std::uint16_t weapon_paracord_knife{ 517 };
		constexpr std::uint16_t weapon_survival_knife{ 518 };
		constexpr std::uint16_t weapon_ursus_knife{ 519 };
		constexpr std::uint16_t weapon_navaja_knife{ 520 };
		constexpr std::uint16_t weapon_nomad_knife{ 521 };
		constexpr std::uint16_t weapon_stiletto_knife{ 522 };
		constexpr std::uint16_t weapon_talon_knife{ 523 };
		constexpr std::uint16_t weapon_skeleton_knife{ 525 };

	} // namespace item_definition_index

	struct tick_fraction
	{
		int tick{};
		float frac{};

		static tick_fraction from_value( float value )
		{
			auto integer_part{ 0.0f };
			auto fractional = std::modff( value, &integer_part );

			if ( fractional < 0.0f )
			{
				fractional += 1.0f;

				if ( fractional < 1.0f )
				{
					integer_part -= 1.0f;
				}
				else
				{
					fractional = 0.0f;
				}
			}

			tick_fraction result{};
			result.tick = ( integer_part >= -2147483600.0f && integer_part < 2147483600.0f ) ? static_cast< int >( integer_part ) : 0;
			result.frac = fractional;

			if ( result.frac < 0.0f || result.frac >= 1.0f )
			{
				result.normalize( );
			}

			return result;
		}

		tick_fraction subtract_value( float value ) const
		{
			auto offset = from_value( value );
			return this->subtract( offset );
		}

		void normalize( )
		{
			auto integer_part{ 0.0f };
			auto fractional = std::modff( frac, &integer_part );

			if ( fractional < 0.0f )
			{
				fractional += 1.0f;

				if ( fractional < 1.0f )
				{
					integer_part -= 1.0f;
				}
				else
				{
					fractional = 0.0f;
				}
			}

			auto extra = ( integer_part >= -2147483600.0f && integer_part < 2147483600.0f ) ? static_cast< int >( integer_part ) : 0;

			if ( fractional < 0.0f || fractional >= 1.0f )
			{
				tick_fraction inner{};
				inner.normalize_raw( fractional, extra );
				this->tick += inner.tick;
				this->frac = inner.frac;
			}
			else
			{
				this->tick += extra;
				this->frac = fractional;
			}
		}

		tick_fraction subtract( const tick_fraction& other ) const
		{
			tick_fraction result{};
			result.frac = this->frac - other.frac;
			result.tick = this->tick - other.tick;

			if ( result.frac < 0.0f )
			{
				result.frac += 1.0f;

				if ( result.frac < 1.0f )
				{
					result.tick--;
				}
				else
				{
					result.frac = 0.0f;
				}
			}

			if ( result.frac < 0.0f || result.frac >= 1.0f )
			{
				result.normalize( );
			}

			return result;
		}

	private:
		void normalize_raw( float f, int& extra_tick )
		{
			auto integer_part{ 0.0f };
			auto fractional = std::modff( f, &integer_part );

			if ( fractional < 0.0f )
			{
				fractional += 1.0f;

				if ( fractional < 1.0f )
				{
					integer_part -= 1.0f;
				}
				else
				{
					fractional = 0.0f;
				}
			}

			const auto extra = ( integer_part >= -2147483600.0f && integer_part < 2147483600.0f ) ? static_cast< int >( integer_part ) : 0;
			this->tick += extra;
			this->frac = fractional;
		}
	};

	struct strong_handle
	{
		const void* binding;
	};

	struct key_values3
	{
		std::uint64_t metadata;
		std::uint64_t payload;
	};
	static_assert( sizeof( key_values3 ) == 0x10 );

	struct kv3_id
	{
		const char* format;
		std::uintptr_t guid_low;
		std::uintptr_t guid_high;
	};

	struct event_hash
	{
		std::uintptr_t hash;
		const char* str;
	};

} // namespace cstypes
