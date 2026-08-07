#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>
#include "../systems.hpp"

namespace systems {

	namespace detail {

		static constexpr char electric[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_complex.vfx"

				F_SELF_ILLUM = 1
				F_RENDER_BACKFACES = 1
				F_TRANSLUCENT = 1

				g_vColorTint = [0.0, 0.0, 0.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 0.8

				g_flSelfIllumBrightness = 3.0
				g_flSelfIllumScale = 1.5
				g_vSelfIllumTint = [0.4, 0.7, 1.0]
				g_flSelfIllumAlbedoFactor = 0.3
				g_vSelfIllumScrollSpeed = [0.15, 0.1]

				g_vTexCoordScrollSpeed = [0.03, 0.02]
				g_vTexCoordScale = [2.0, 2.0]

				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tSelfIllumMask = resource:"materials/particle/electrical/electrical_cracks.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tTintMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char liquid[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
				shader = "csgo_complex.vfx"

				F_SELF_ILLUM = 1
				F_RENDER_BACKFACES = 1
				F_TRANSLUCENT = 1

				g_vColorTint = [0.0, 0.0, 0.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 0.8

				g_flSelfIllumBrightness = 3.0
				g_flSelfIllumScale = 1.5
				g_vSelfIllumTint = [0.4, 0.7, 1.0]
				g_flSelfIllumAlbedoFactor = 0.3
				g_vSelfIllumScrollSpeed = [0.05, 0.03]

				g_vTexCoordScrollSpeed = [0.01, 0.005]

				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tSelfIllumMask = resource:"materials/dev/water_waves.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tTintMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
            })#";

		static constexpr char liquid_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_complex.vfx"

				F_SELF_ILLUM = 1
				F_RENDER_BACKFACES = 1
				F_TRANSLUCENT = 1
				F_DISABLE_Z_BUFFERING = 1

				g_vColorTint = [1.0, 1.0, 1.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 0.8

				g_flSelfIllumBrightness = 3.0
				g_flSelfIllumScale = 1.5
				g_vSelfIllumTint = [0.4, 0.7, 1.0]
				g_flSelfIllumAlbedoFactor = 0.3
				g_vSelfIllumScrollSpeed = [0.05, 0.03]

				g_vTexCoordScrollSpeed = [0.01, 0.005]

				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tSelfIllumMask = resource:"materials/dev/water_waves.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tTintMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char metallic[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_character.vfx"

                F_IRIDESCENCE = 1
                F_CLOTH_SHADING = 1
                F_RENDER_BACKFACES = 1
                F_DISABLE_Z_PREPASS = 1
                F_TRANSLUCENT = 1
                F_ADDITIVE_BLEND = 1

                g_vColorTint = [1.0, 1.0, 1.0]
                g_flModelTintAmount = 1.0
                g_flOpacityScale = 1.0

                g_vTexCoordScrollSpeed = [0.05, 0.02]
                g_vTexCoordScale = [1.2, 1.2]

                g_flIridescentStrength = 2.0
                g_flIridescentFresnelStrength = 15.0
                g_flIridescentHueShift = 0.5

                g_flSheenScale = 10.0
                g_flSheenTintColor = [1.0, 1.0, 1.0]

                g_fContrast = 0.5
                g_fBrightness = 1.5
                g_fSaturation = 1.5

                g_flAmbientOcclusionMasking = 0.0
                g_bFogEnabled = 0

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tNormal = resource:"materials/dev/water_waves.vtex"
                g_tMetalness = resource:"materials/dev/water_waves.vtex"
                g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tIridescentThickness_Mask = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
            })#";

		static constexpr char matte[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "generic.vfx"

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
            })#";

		// TODO
		static constexpr char matte_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "generic.vfx"

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
            })#";

		static constexpr char flat[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_unlitgeneric.vfx"

                F_RENDER_BACKFACES = 0
                F_DISABLE_Z_BUFFERING = 0
                F_PAINT_VERTEX_COLORS = 1
                F_TRANSLUCENT = 1
                F_BLEND_MODE = 1

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
            })#";

		static constexpr char flat_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_unlitgeneric.vfx"

                F_RENDER_BACKFACES = 0
                F_DISABLE_Z_BUFFERING = 1
                F_DISABLE_Z_WRITE = 1
                F_PAINT_VERTEX_COLORS = 1
                F_TRANSLUCENT = 1
                F_BLEND_MODE = 1

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
            })#";

		static constexpr char bloom[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "solidcolor.vfx"

                F_DISABLE_Z_WRITE = 0

                g_vColorTint = [8.0, 8.0, 8.0]
            })#";

		static constexpr char bloom_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "solidcolor.vfx"

                F_IGNOREZ = 1
                F_DISABLE_Z_WRITE = 1

                g_vColorTint = [5.0, 5.0, 5.0]
            })#";

		static constexpr char outlines[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_effects.vfx"

                F_ADDITIVE_BLEND = 1
                F_BLEND_MODE = 1
                F_TRANSLUCENT = 1

                g_vColorTint = [1.0, 1.0, 1.0, 0.0]
                g_flOpacityScale = 0.45
                g_flFresnelExponent = 0.75
                g_flFresnelFalloff = 1.0
                g_flFresnelMax = 0.0
                g_flFresnelMin = 1.0
				g_flColorBoost = 2.25
                g_flToolsVisCubemapReflectionRoughness = 1.0
                g_flBeginMixingRoughness = 1.0

                g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
            })#";

		static constexpr char outlines_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_effects.vfx"

                F_ADDITIVE_BLEND = 1
                F_BLEND_MODE = 1
                F_TRANSLUCENT = 1
                F_DISABLE_Z_BUFFERING = 1
                F_DISABLE_Z_WRITE = 1

                g_vColorTint = [1.0, 1.0, 1.0, 0.0]
                g_flOpacityScale = 0.45
                g_flFresnelExponent = 0.75
                g_flFresnelFalloff = 1.0
                g_flFresnelMax = 0.0
                g_flFresnelMin = 1.0
				g_flColorBoost = 2.25
                g_flToolsVisCubemapReflectionRoughness = 1.0
                g_flBeginMixingRoughness = 1.0

                g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
            })#";

		static constexpr char glow[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_effects.vfx"

                F_ADDITIVE_BLEND = 1
                F_BLEND_MODE = 1
                F_TRANSLUCENT = 1
                F_IGNOREZ = 0
                F_DISABLE_Z_BUFFERING = 0
                F_RENDER_BACKFACES = 0

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]
				g_flFresnelExponent = 1.5
				g_flFresnelFalloff = 5.0
				g_flFresnelMax = 0.0
				g_flFresnelMin = 1.0
				g_flColorBoost = 20.0
				g_flOpacityScale = 0.6

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_344101f8.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex"
            })#";

		static constexpr char glow_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
            format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
            {
                shader = "csgo_effects.vfx"

                F_ADDITIVE_BLEND = 1
                F_BLEND_MODE = 1
                F_TRANSLUCENT = 1
                F_IGNOREZ = 1
                F_DISABLE_Z_BUFFERING = 1
                F_DISABLE_Z_WRITE = 1
                F_RENDER_BACKFACES = 0

                g_vColorTint = [1.0, 1.0, 1.0, 1.0]
				g_flFresnelExponent = 1.5
				g_flFresnelFalloff = 5.0
				g_flFresnelMax = 0.0
				g_flFresnelMin = 1.0
				g_flColorBoost = 20.0
				g_flOpacityScale = 0.6

                g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_344101f8.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex"
            })#";

		static constexpr char hologram[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_complex.vfx"

				F_SELF_ILLUM = 1
				F_RENDER_BACKFACES = 1
				F_TRANSLUCENT = 1

				g_vColorTint = [0.0, 0.0, 0.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 0.6

				g_flSelfIllumBrightness = 4.5
				g_flSelfIllumScale = 2.0
				g_vSelfIllumTint = [0.45, 0.85, 1.0]
				g_flSelfIllumAlbedoFactor = 0.55
				g_vSelfIllumScrollSpeed = [0.0, 0.35]

				g_vTexCoordScale = [0.75, 9.0]

				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tSelfIllumMask = resource:"materials/dev/water_waves.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tTintMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char hologram_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_complex.vfx"

				F_SELF_ILLUM = 1
				F_RENDER_BACKFACES = 1
				F_TRANSLUCENT = 1
				F_DISABLE_Z_BUFFERING = 1

				g_vColorTint = [0.0, 0.0, 0.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 0.6

				g_flSelfIllumBrightness = 4.5
				g_flSelfIllumScale = 2.0
				g_vSelfIllumTint = [0.45, 0.85, 1.0]
				g_flSelfIllumAlbedoFactor = 0.55
				g_vSelfIllumScrollSpeed = [0.0, 0.35]

				g_vTexCoordScale = [0.75, 9.0]

				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tSelfIllumMask = resource:"materials/dev/water_waves.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tTintMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char distortion[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_effects.vfx"

				F_ADDITIVE_BLEND = 1
				F_BLEND_MODE = 1
				F_TRANSLUCENT = 1
				F_RENDER_BACKFACES = 1

				g_vColorTint = [1.0, 1.0, 1.0, 1.0]
				g_flOpacityScale = 0.85
				g_flFresnelExponent = 1.25
				g_flFresnelFalloff = 2.25
				g_flFresnelMax = 0.32
				g_flFresnelMin = 1.0
				g_flColorBoost = 14.0
				g_vTexCoordScrollSpeed = [0.24, 0.17]
				g_vTexCoordScale = [2.75, 2.75]
				g_flToolsVisCubemapReflectionRoughness = 1.0
				g_flBeginMixingRoughness = 1.0

				g_tColor = resource:"materials/dev/water_waves.vtex"
				g_tMask1 = resource:"materials/dev/water_waves.vtex"
				g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex"
				g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex"
				g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char distortion_ignorez[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_effects.vfx"

				F_ADDITIVE_BLEND = 1
				F_BLEND_MODE = 1
				F_TRANSLUCENT = 1
				F_RENDER_BACKFACES = 1
				F_DISABLE_Z_BUFFERING = 1
				F_DISABLE_Z_WRITE = 1

				g_vColorTint = [1.0, 1.0, 1.0, 1.0]
				g_flOpacityScale = 0.85
				g_flFresnelExponent = 1.25
				g_flFresnelFalloff = 2.25
				g_flFresnelMax = 0.32
				g_flFresnelMin = 1.0
				g_flColorBoost = 14.0
				g_vTexCoordScrollSpeed = [0.24, 0.17]
				g_vTexCoordScale = [2.75, 2.75]
				g_flToolsVisCubemapReflectionRoughness = 1.0
				g_flBeginMixingRoughness = 1.0

				g_tColor = resource:"materials/dev/water_waves.vtex"
				g_tMask1 = resource:"materials/dev/water_waves.vtex"
				g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex"
				g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex"
				g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
			})#";

		static constexpr char pearl[ ] = R"#(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}
			format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
			{
				shader = "csgo_character.vfx"

				F_IRIDESCENCE = 1
				F_CLOTH_SHADING = 1
				F_RENDER_BACKFACES = 1
				F_DISABLE_Z_PREPASS = 1

				g_vColorTint = [1.0, 1.0, 1.0]
				g_flModelTintAmount = 1.0
				g_flOpacityScale = 1.0

				g_vTexCoordScale = [1.0, 1.0]

				g_flIridescentStrength = 3.5
				g_flIridescentFresnelStrength = 4.0
				g_flIridescentHueShift = 1.0

				g_flSheenScale = 6.0
				g_flSheenTintColor = [1.0, 1.0, 1.0]

				g_fContrast = 0.35
				g_fBrightness = 1.25
				g_fSaturation = 1.6

				g_flAmbientOcclusionMasking = 0.0
				g_bFogEnabled = 0

				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
				g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
				g_tMetalness = resource:"materials/default/default_mask_tga_344101f8.vtex"
				g_tAmbientOcclusion = resource:"materials/default/default_mask_tga_fde710a5.vtex"
				g_tIridescentThickness_Mask = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
			})#";

	} // namespace detail

	bool materials::initialize( )
	{
		const auto liquid_ignorez_ptr = load( detail::liquid_ignorez, xs( "materials/dev/liquid_ignorez.vmat" ) );
		const auto matte_ignorez_ptr = load( detail::matte_ignorez, xs( "materials/dev/matte_ignorez.vmat" ) );
		const auto flat_ignorez_ptr = load( detail::flat_ignorez, xs( "materials/dev/flat_ignorez.vmat" ) );
		const auto bloom_ignorez_ptr = load( detail::bloom_ignorez, xs( "materials/dev/bloom_ignorez.vmat" ) );
		const auto outlines_ignorez_ptr = load( detail::outlines_ignorez, xs( "materials/dev/outlines_ignorez.vmat" ) );
		const auto glow_ignorez_ptr = load( detail::glow_ignorez, xs( "materials/dev/glow_ignorez.vmat" ) );
		const auto distortion_ignorez_ptr = load( detail::distortion_ignorez, xs( "materials/dev/distortion_ignorez.vmat" ) );
		const auto hologram_ignorez_ptr = load( detail::hologram_ignorez, xs( "materials/dev/hologram_ignorez.vmat" ) );

		const auto liquid_ptr = load( detail::liquid, xs( "materials/dev/liquid.vmat" ) );
		const auto metallic_ptr = load( detail::metallic, xs( "materials/dev/metallic.vmat" ) );
		const auto matte_ptr = load( detail::matte, xs( "materials/dev/matte.vmat" ) );
		const auto flat_ptr = load( detail::flat, xs( "materials/dev/flat.vmat" ) );
		const auto bloom_ptr = load( detail::bloom, xs( "materials/dev/bloom.vmat" ) );
		const auto outlines_ptr = load( detail::outlines, xs( "materials/dev/outlines.vmat" ) );
		const auto glow_ptr = load( detail::glow, xs( "materials/dev/glow.vmat" ) );
		const auto electric_ptr = load( detail::electric, xs( "materials/dev/electric.vmat" ) );
		const auto distortion_ptr = load( detail::distortion, xs( "materials/dev/distortion.vmat" ) );
		const auto hologram_ptr = load( detail::hologram, xs( "materials/dev/hologram.vmat" ) );
		const auto pearl_ptr = load( detail::pearl, xs( "materials/dev/pearl.vmat" ) );

		if ( !liquid_ptr || !metallic_ptr || !matte_ptr || !flat_ptr || !bloom_ptr || !outlines_ptr || !glow_ptr || !electric_ptr || !distortion_ptr || !hologram_ptr || !pearl_ptr || !liquid_ignorez_ptr || !matte_ignorez_ptr || !flat_ignorez_ptr || !bloom_ignorez_ptr || !outlines_ignorez_ptr || !glow_ignorez_ptr || !distortion_ignorez_ptr || !hologram_ignorez_ptr )
		{
			return false;
		}

		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::liquid ) ] = liquid_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::metallic ) ] = metallic_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::matte ) ] = matte_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::flat ) ] = flat_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::bloom ) ] = bloom_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::outlines ) ] = outlines_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::glow ) ] = glow_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::electric ) ] = electric_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::distortion ) ] = distortion_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::hologram ) ] = hologram_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::pearl ) ] = pearl_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::liquid_ignorez ) ] = liquid_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::matte_ignorez ) ] = matte_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::flat_ignorez ) ] = flat_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::bloom_ignorez ) ] = bloom_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::outlines_ignorez ) ] = outlines_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::glow_ignorez ) ] = glow_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::distortion_ignorez ) ] = distortion_ignorez_ptr;
		m_loaded[ static_cast< std::size_t >( settings::esp::cham_ids::hologram_ignorez ) ] = hologram_ignorez_ptr;

		return true;
	}

	std::uintptr_t materials::find( settings::esp::cham_ids id )
	{
		const auto index = static_cast< std::size_t >( id );
		if ( index >= m_loaded.size( ) )
		{
			return 0;
		}

		return m_loaded[ index ];
	}

	const char* materials::get_texture_path( std::uintptr_t entry )
	{
		const auto handle = *reinterpret_cast< const std::uintptr_t* >( entry + 0x10 );
		if ( !handle )
		{
			return nullptr;
		}

		const auto resource = *reinterpret_cast< const std::uintptr_t* >( handle + 0x08 );
		if ( !resource )
		{
			return nullptr;
		}

		return *reinterpret_cast< const char** >( resource );
	}

	std::string materials::emit_translucent_kv( std::uintptr_t src_mat )
	{
		const auto kv_count = *reinterpret_cast< const int* >( src_mat + 0x18 );
		const auto kv_array = *reinterpret_cast< const std::uintptr_t* >( src_mat + 0x20 );

		std::string kv =
			"<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} "
			"format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n"
			"{\n"
			"shader = \"csgo_complex.vfx\"\n"
			"F_TRANSLUCENT = 1\n"
			"F_ALPHA_TEST = 0\n"
			"F_ADDITIVE_BLEND = 0\n";

		auto should_skip = [ ]( const char* name ) -> bool
			{
				if ( std::strcmp( name, "shader" ) == 0 )
				{
					return true;
				}

				if ( std::strncmp( name, "F_", 2 ) == 0 )
				{
					return true;
				}

				return false;
			};

		for ( auto i = 0; i < kv_count; ++i )
		{
			const auto entry = kv_array + static_cast< std::uintptr_t >( i ) * 0x40;
			const auto name = *reinterpret_cast< const char** >( entry + 0x28 );

			if ( !name || should_skip( name ) )
			{
				continue;
			}

			if ( *reinterpret_cast< const std::uintptr_t* >( entry + 0x10 ) )
			{
				const auto path = get_texture_path( entry );
				if ( path && path[ 0 ] )
				{
					kv += std::format( "{} = resource:\"{}\"\n", name, path );
				}

				continue;
			}

			if ( std::strncmp( name, "g_b", 3 ) == 0 )
			{
				const auto v = static_cast< int >( *reinterpret_cast< const float* >( entry ) );
				kv += std::format( "{} = {}\n", name, v ? 1 : 0 );
				continue;
			}

			if ( std::strncmp( name, "g_n", 3 ) == 0 )
			{
				const auto v = static_cast< int >( *reinterpret_cast< const float* >( entry ) );
				kv += std::format( "{} = {}\n", name, v );
				continue;
			}

			if ( std::strncmp( name, "g_fl", 4 ) == 0 || std::strncmp( name, "g_f", 3 ) == 0 )
			{
				const auto v = *reinterpret_cast< const float* >( entry );
				kv += std::format( "{} = {}\n", name, v );
				continue;
			}

			if ( std::strncmp( name, "g_v", 3 ) == 0 )
			{
				const auto x = *reinterpret_cast< const float* >( entry );
				const auto y = *reinterpret_cast< const float* >( entry + 0x04 );
				const auto z = *reinterpret_cast< const float* >( entry + 0x08 );
				kv += std::format( "{} = [{}, {}, {}]\n", name, x, y, z );
				continue;
			}
		}

		kv += "}\n";
		return kv;
	}

	std::string materials::emit_ignorez_kv( std::uintptr_t src_mat )
	{
		const auto kv_count = *reinterpret_cast< const int* >( src_mat + 0x18 );
		const auto kv_array = *reinterpret_cast< const std::uintptr_t* >( src_mat + 0x20 );

		std::string kv =
			"<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} "
			"format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n"
			"{\n"
			"shader = \"csgo_complex.vfx\"\n"
			"F_TRANSLUCENT = 1\n"
			"F_DISABLE_Z_BUFFERING = 1\n"
			"F_ALPHA_TEST = 0\n"
			"F_ADDITIVE_BLEND = 0\n";

		auto should_skip = [ ]( const char* name ) -> bool
			{
				if ( std::strcmp( name, "shader" ) == 0 )
				{
					return true;
				}

				if ( std::strncmp( name, "F_", 2 ) == 0 )
				{
					return true;
				}

				return false;
			};

		for ( auto i = 0; i < kv_count; ++i )
		{
			const auto entry = kv_array + static_cast< std::uintptr_t >( i ) * 0x40;
			const auto name = *reinterpret_cast< const char** >( entry + 0x28 );

			if ( !name || should_skip( name ) )
			{
				continue;
			}

			if ( *reinterpret_cast< const std::uintptr_t* >( entry + 0x10 ) )
			{
				const auto path = get_texture_path( entry );
				if ( path && path[ 0 ] )
				{
					kv += std::format( "{} = resource:\"{}\"\n", name, path );
				}

				continue;
			}

			if ( std::strncmp( name, "g_b", 3 ) == 0 )
			{
				const auto v = static_cast< int >( *reinterpret_cast< const float* >( entry ) );
				kv += std::format( "{} = {}\n", name, v ? 1 : 0 );
				continue;
			}

			if ( std::strncmp( name, "g_n", 3 ) == 0 )
			{
				const auto v = static_cast< int >( *reinterpret_cast< const float* >( entry ) );
				kv += std::format( "{} = {}\n", name, v );
				continue;
			}

			if ( std::strncmp( name, "g_fl", 4 ) == 0 || std::strncmp( name, "g_f", 3 ) == 0 )
			{
				const auto v = *reinterpret_cast< const float* >( entry );
				kv += std::format( "{} = {}\n", name, v );
				continue;
			}

			if ( std::strncmp( name, "g_v", 3 ) == 0 )
			{
				const auto x = *reinterpret_cast< const float* >( entry );
				const auto y = *reinterpret_cast< const float* >( entry + 0x04 );
				const auto z = *reinterpret_cast< const float* >( entry + 0x08 );
				kv += std::format( "{} = [{}, {}, {}]\n", name, x, y, z );
				continue;
			}
		}

		kv += "}\n";
		return kv;
	}

	std::uintptr_t materials::load( const char* vmat_data, const char* name )
	{
		constexpr auto kv3_id = cstypes::kv3_id{ "generic", 0x41B818518343427E, 0xB5F447C23C0CDF8C };

		if ( !vmat_data || !name )
		{
			return 0;
		}

		const auto kv3_set_type = PATTERN( patterns::kv3_alloc );
		const auto kv3_destroy = PATTERN( patterns::kv3_destroy );
		const auto kv3_load = MODULE_EXPORT( "tier0.dll:?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z" );
		const auto material_create = PATTERN( patterns::material_create );
		if ( !kv3_set_type || !kv3_destroy || !kv3_load || !material_create )
		{
			return 0;
		}

		cstypes::key_values3 kv3{};
		if ( memory::call<cstypes::key_values3*>( kv3_set_type, &kv3, 1u, 6u ) != &kv3 )
		{
			return 0;
		}

		cstypes::strong_handle handle{};
		const auto loaded = memory::call<bool>( kv3_load, &kv3, nullptr, vmat_data, &kv3_id, nullptr, 0u );
		if ( loaded )
		{
			memory::call<void*>( material_create, nullptr, &handle, name, &kv3, 0, true );
		}

		// CreateMaterial copies the parsed tree; release the parser-owned value.
		memory::call<void>( kv3_destroy, &kv3, 0u );

		if ( !loaded || !handle.binding )
		{
			return 0;
		}

		const auto material = *reinterpret_cast< const std::uintptr_t* >( handle.binding );
		if ( material )
		{
			std::scoped_lock lock( m_mtx );
			m_handles.push_back( handle );
		}

		return material;
	}

	std::uintptr_t materials::get_or_create_clone( std::uintptr_t src_mat, clone_type type )
	{
		const auto key = src_mat ^ ( static_cast< std::uint64_t >( type ) << 48 );

		{
			std::scoped_lock lock( m_mtx );
			const auto it = m_map.find( key );
			if ( it != m_map.end( ) )
			{
				return it->second;
			}
		}

		const auto kv = type == clone_type::ignorez ? emit_ignorez_kv( src_mat ) : emit_translucent_kv( src_mat );
		const auto prefix = type == clone_type::ignorez ? "_ignorez" : "_clone";
		const auto name = std::format( "materials/{}_{:x}.vmat", prefix, src_mat );
		const auto mat = load( kv.c_str( ), name.c_str( ) );

		{
			std::scoped_lock lock( m_mtx );
			m_map.emplace( key, mat );
		}

		return mat;
	}

	void materials::clear_clones( )
	{
		std::scoped_lock lock( m_mtx );
		m_map.clear( );
	}

	void materials::set_material_vec3( std::uintptr_t mat, const char* param_name, float x, float y, float z )
	{
		const auto kv_count = *reinterpret_cast< const int* >( mat + 0x18 );
		const auto kv_array = *reinterpret_cast< const std::uintptr_t* >( mat + 0x20 );

		for ( auto i = 0; i < kv_count; ++i )
		{
			const auto entry = kv_array + static_cast< std::uintptr_t >( i ) * 0x40;
			const auto name = *reinterpret_cast< const char** >( entry + 0x28 );

			if ( !name || std::strcmp( name, param_name ) != 0 )
			{
				continue;
			}

			*reinterpret_cast< float* >( entry + 0x00 ) = x;
			*reinterpret_cast< float* >( entry + 0x04 ) = y;
			*reinterpret_cast< float* >( entry + 0x08 ) = z;
			return;
		}
	}

} // namespace systems
