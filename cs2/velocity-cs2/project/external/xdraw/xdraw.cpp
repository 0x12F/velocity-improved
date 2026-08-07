#define NOMINMAX
#include <pch/pch.hpp>

#include "xdraw.hpp"

#include <wincodec.h>
#include <algorithm>
#include <string>
#include <numbers>
#include <memory>

#include "dependencies/freetype/include.hpp"
#include "dependencies/nanosvg/include.hpp"

#include "dependencies/fonts.hpp"
#include "dependencies/shaders.hpp"

namespace xdraw {

	using Microsoft::WRL::ComPtr;

	namespace detail {

		struct state
		{
			ComPtr<ID3D11Device> device{};
			ComPtr<ID3D11DeviceContext> context{};

			ComPtr<ID3D11Buffer> vb{};
			ComPtr<ID3D11Buffer> ib{};
			ComPtr<ID3D11Buffer> cb{};
			std::uint32_t vb_capacity{};
			std::uint32_t ib_capacity{};

			ComPtr<ID3D11VertexShader> vs{};
			ComPtr<ID3D11PixelShader> ps{};
			ComPtr<ID3D11InputLayout> layout{};
			ComPtr<ID3D11RasterizerState> rasterizer{};
			ComPtr<ID3D11BlendState> blend{};
			ComPtr<ID3D11DepthStencilState> depth{};
			ComPtr<ID3D11SamplerState> sampler{};

			ComPtr<ID3D11Texture2D> white_tex{};
			ComPtr<ID3D11ShaderResourceView> white_srv{};

			draw_list lists[ 3 ]{};

			std::vector<std::unique_ptr<font>> fonts{};
			std::vector<font*> font_stack{};

			font* primary_font{};
			font* math_font{};

			LARGE_INTEGER perf_freq{};
			LARGE_INTEGER last_time{};
			float dt{};
			float fps{};

			static constexpr auto k_blur_iterations{ 4 };

			struct blur_level
			{
				ComPtr<ID3D11Texture2D> tex{};
				ComPtr<ID3D11RenderTargetView> rtv{};
				ComPtr<ID3D11ShaderResourceView> srv{};
				int w{};
				int h{};
			};

			blur_level blur_chain[ k_blur_iterations ]{};
			ComPtr<ID3D11Texture2D> blur_scene_tex{};
			ComPtr<ID3D11ShaderResourceView> blur_scene_srv{};
			ComPtr<ID3D11VertexShader> blur_vs{};
			ComPtr<ID3D11PixelShader> blur_downsample_ps{};
			ComPtr<ID3D11PixelShader> blur_upsample_ps{};
			ComPtr<ID3D11Buffer> blur_cb{};
			ComPtr<ID3D11BlendState> blur_blend{};
			ComPtr<ID3D11RasterizerState> blur_rasterizer{};
			int blur_cached_w{};
			int blur_cached_h{};

			ComPtr<ID3D11Texture2D> glow_tex{};
			ComPtr<ID3D11RenderTargetView> glow_rtv{};
			ComPtr<ID3D11ShaderResourceView> glow_srv{};
			ComPtr<ID3D11BlendState> glow_additive_blend{};

			draw_list glow_lists[ 3 ]{};
		};

		static state g{};

		static constexpr std::uint32_t k_initial_vb_size{ 64u * 1024u * sizeof( vertex ) };
		static constexpr std::uint32_t k_initial_ib_size{ 128u * 1024u * sizeof( std::uint32_t ) };

		static bool create_buffer( ID3D11Device* dev, ComPtr<ID3D11Buffer>& buf, std::uint32_t size, D3D11_BIND_FLAG bind )
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = size;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = bind;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			return SUCCEEDED( dev->CreateBuffer( &desc, nullptr, &buf ) );
		}

		static bool grow_buffer( ComPtr<ID3D11Buffer>& buf, std::uint32_t& capacity, std::uint32_t required, D3D11_BIND_FLAG bind )
		{
			if ( required <= capacity )
			{
				return true;
			}

			capacity = std::max( capacity * 2u, required );
			buf.Reset( );
			return create_buffer( g.device.Get( ), buf, capacity, bind );
		}

		static bool create_shaders( )
		{
			HRESULT hr{};

			hr = g.device->CreateVertexShader( shaders::vs_bytecode, sizeof( shaders::vs_bytecode ), nullptr, &g.vs );
			if ( FAILED( hr ) )
			{
				return false;
			}

			hr = g.device->CreatePixelShader( shaders::ps_bytecode, sizeof( shaders::ps_bytecode ), nullptr, &g.ps );
			if ( FAILED( hr ) )
			{
				return false;
			}

			D3D11_INPUT_ELEMENT_DESC input_desc[ ]
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof( vertex, pos ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof( vertex, uv ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof( vertex, col ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			hr = g.device->CreateInputLayout( input_desc, 3, shaders::vs_bytecode, sizeof( shaders::vs_bytecode ), &g.layout );

			return SUCCEEDED( hr );
		}

		static bool create_states( )
		{
			D3D11_RASTERIZER_DESC rd{};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_NONE;
			rd.ScissorEnable = TRUE;
			rd.DepthClipEnable = TRUE;

			if ( FAILED( g.device->CreateRasterizerState( &rd, &g.rasterizer ) ) )
			{
				return false;
			}

			D3D11_BLEND_DESC bd{};
			bd.RenderTarget[ 0 ].BlendEnable = TRUE;
			bd.RenderTarget[ 0 ].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bd.RenderTarget[ 0 ].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[ 0 ].SrcBlendAlpha = D3D11_BLEND_ONE;
			bd.RenderTarget[ 0 ].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			if ( FAILED( g.device->CreateBlendState( &bd, &g.blend ) ) )
			{
				return false;
			}

			D3D11_DEPTH_STENCIL_DESC dd{};
			dd.DepthEnable = FALSE;

			if ( FAILED( g.device->CreateDepthStencilState( &dd, &g.depth ) ) )
			{
				return false;
			}

			D3D11_SAMPLER_DESC sd{};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
			sd.MaxLOD = D3D11_FLOAT32_MAX;

			return SUCCEEDED( g.device->CreateSamplerState( &sd, &g.sampler ) );
		}

		static bool create_white_texture( )
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = 1;
			td.Height = 1;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			const auto white{ 0xffffffffu };
			D3D11_SUBRESOURCE_DATA init{ &white, 4, 0 };

			if ( FAILED( g.device->CreateTexture2D( &td, &init, &g.white_tex ) ) )
			{
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = td.Format;
			sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sv.Texture2D.MipLevels = 1;

			return SUCCEEDED( g.device->CreateShaderResourceView( g.white_tex.Get( ), &sv, &g.white_srv ) );
		}

		static bool create_blur_resources( )
		{
			D3D11_BLEND_DESC bbd{};
			bbd.RenderTarget[ 0 ].BlendEnable = FALSE;
			bbd.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			if ( FAILED( g.device->CreateBlendState( &bbd, &g.blur_blend ) ) )
			{
				return false;
			}

			D3D11_RASTERIZER_DESC brd{};
			brd.FillMode = D3D11_FILL_SOLID;
			brd.CullMode = D3D11_CULL_NONE;
			brd.ScissorEnable = FALSE;
			brd.DepthClipEnable = TRUE;

			if ( FAILED( g.device->CreateRasterizerState( &brd, &g.blur_rasterizer ) ) )
			{
				return false;
			}

			D3D11_BUFFER_DESC cbd{};
			cbd.ByteWidth = 16;
			cbd.Usage = D3D11_USAGE_DYNAMIC;
			cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			if ( FAILED( g.device->CreateBuffer( &cbd, nullptr, &g.blur_cb ) ) )
			{
				return false;
			}

			auto hr = g.device->CreateVertexShader( shaders::blur_fullscreen_vs_bytecode, sizeof( shaders::blur_fullscreen_vs_bytecode ), nullptr, &g.blur_vs );
			if ( FAILED( hr ) )
			{
				return false;
			}

			hr = g.device->CreatePixelShader( shaders::blur_downsample_ps_bytecode, sizeof( shaders::blur_downsample_ps_bytecode ), nullptr, &g.blur_downsample_ps );
			if ( FAILED( hr ) )
			{
				return false;
			}

			hr = g.device->CreatePixelShader( shaders::blur_upsample_ps_bytecode, sizeof( shaders::blur_upsample_ps_bytecode ), nullptr, &g.blur_upsample_ps );
			if ( FAILED( hr ) )
			{
				return false;
			}

			D3D11_BLEND_DESC abd{};
			abd.RenderTarget[ 0 ].BlendEnable = TRUE;
			abd.RenderTarget[ 0 ].SrcBlend = D3D11_BLEND_ONE;
			abd.RenderTarget[ 0 ].DestBlend = D3D11_BLEND_ONE;
			abd.RenderTarget[ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
			abd.RenderTarget[ 0 ].SrcBlendAlpha = D3D11_BLEND_ONE;
			abd.RenderTarget[ 0 ].DestBlendAlpha = D3D11_BLEND_ONE;
			abd.RenderTarget[ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			abd.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			if ( FAILED( g.device->CreateBlendState( &abd, &g.glow_additive_blend ) ) )
			{
				return false;
			}

			return true;
		}

		static void create_blur_textures( int w, int h )
		{
			g.blur_scene_tex.Reset( );
			g.blur_scene_srv.Reset( );

			D3D11_TEXTURE2D_DESC td{};
			td.Width = static_cast< UINT >( w );
			td.Height = static_cast< UINT >( h );
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			g.device->CreateTexture2D( &td, nullptr, &g.blur_scene_tex );

			D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = td.Format;
			sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sv.Texture2D.MipLevels = 1;

			g.device->CreateShaderResourceView( g.blur_scene_tex.Get( ), &sv, &g.blur_scene_srv );

			auto mw = w;
			auto mh = h;

			for ( auto i = 0; i < state::k_blur_iterations; ++i )
			{
				mw = std::max( 1, mw / 2 );
				mh = std::max( 1, mh / 2 );

				auto& lvl = g.blur_chain[ i ];
				lvl.tex.Reset( );
				lvl.rtv.Reset( );
				lvl.srv.Reset( );
				lvl.w = mw;
				lvl.h = mh;

				D3D11_TEXTURE2D_DESC ltd{};
				ltd.Width = static_cast< UINT >( mw );
				ltd.Height = static_cast< UINT >( mh );
				ltd.MipLevels = 1;
				ltd.ArraySize = 1;
				ltd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				ltd.SampleDesc.Count = 1;
				ltd.Usage = D3D11_USAGE_DEFAULT;
				ltd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

				g.device->CreateTexture2D( &ltd, nullptr, &lvl.tex );
				g.device->CreateRenderTargetView( lvl.tex.Get( ), nullptr, &lvl.rtv );
				g.device->CreateShaderResourceView( lvl.tex.Get( ), nullptr, &lvl.srv );
			}

			g.blur_cached_w = w;
			g.blur_cached_h = h;

			g.glow_tex.Reset( );
			g.glow_rtv.Reset( );
			g.glow_srv.Reset( );

			D3D11_TEXTURE2D_DESC gtd{};
			gtd.Width = static_cast< UINT >( w );
			gtd.Height = static_cast< UINT >( h );
			gtd.MipLevels = 1;
			gtd.ArraySize = 1;
			gtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			gtd.SampleDesc.Count = 1;
			gtd.Usage = D3D11_USAGE_DEFAULT;
			gtd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

			g.device->CreateTexture2D( &gtd, nullptr, &g.glow_tex );
			g.device->CreateRenderTargetView( g.glow_tex.Get( ), nullptr, &g.glow_rtv );
			g.device->CreateShaderResourceView( g.glow_tex.Get( ), nullptr, &g.glow_srv );
		}

		static void update_blur_cb( float tex_w, float tex_h )
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if ( SUCCEEDED( g.context->Map( g.blur_cb.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			{
				auto data = static_cast< float* >( mapped.pData );
				data[ 0 ] = 1.0f / tex_w;
				data[ 1 ] = 1.0f / tex_h;
				data[ 2 ] = 0.0f;
				data[ 3 ] = 0.0f;
				g.context->Unmap( g.blur_cb.Get( ), 0 );
			}
		}

		static void run_blur_pass( )
		{
			auto* ctx = g.context.Get( );

			ComPtr<ID3D11RenderTargetView> orig_rtv{};
			ComPtr<ID3D11DepthStencilView> orig_dsv{};
			ctx->OMGetRenderTargets( 1, &orig_rtv, &orig_dsv );

			D3D11_VIEWPORT orig_vp{};
			UINT num_vp{ 1 };
			ctx->RSGetViewports( &num_vp, &orig_vp );

			ComPtr<ID3D11Resource> bb_resource{};
			if ( orig_rtv )
			{
				orig_rtv->GetResource( &bb_resource );
			}

			if ( !bb_resource )
			{
				return;
			}

			ctx->CopyResource( g.blur_scene_tex.Get( ), bb_resource.Get( ) );

			ctx->IASetInputLayout( nullptr );
			ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			ctx->VSSetShader( g.blur_vs.Get( ), nullptr, 0 );
			ctx->PSSetSamplers( 0, 1, g.sampler.GetAddressOf( ) );

			constexpr float bf[ 4 ]{ 0, 0, 0, 0 };
			ctx->OMSetBlendState( g.blur_blend.Get( ), bf, 0xFFFFFFFF );
			ctx->RSSetState( g.blur_rasterizer.Get( ) );

			ID3D11ShaderResourceView* null_srv{ nullptr };
			D3D11_VIEWPORT vp{};
			vp.MaxDepth = 1.0f;

			ctx->PSSetShader( g.blur_downsample_ps.Get( ), nullptr, 0 );

			for ( auto i = 0; i < state::k_blur_iterations; ++i )
			{
				auto& dst = g.blur_chain[ i ];
				auto* src_srv = i == 0 ? g.blur_scene_srv.Get( ) : g.blur_chain[ i - 1 ].srv.Get( );
				const auto src_w = static_cast< float >( i == 0 ? g.blur_cached_w : g.blur_chain[ i - 1 ].w );
				const auto src_h = static_cast< float >( i == 0 ? g.blur_cached_h : g.blur_chain[ i - 1 ].h );

				ctx->PSSetShaderResources( 0, 1, &null_srv );
				ctx->OMSetRenderTargets( 1, dst.rtv.GetAddressOf( ), nullptr );

				vp.Width = static_cast< float >( dst.w );
				vp.Height = static_cast< float >( dst.h );
				ctx->RSSetViewports( 1, &vp );

				update_blur_cb( src_w, src_h );
				ctx->PSSetConstantBuffers( 0, 1, g.blur_cb.GetAddressOf( ) );
				ctx->PSSetShaderResources( 0, 1, &src_srv );
				ctx->Draw( 3, 0 );
			}

			ctx->PSSetShader( g.blur_upsample_ps.Get( ), nullptr, 0 );

			for ( int i = state::k_blur_iterations - 1; i > 0; --i )
			{
				auto& src = g.blur_chain[ i ];
				auto& dst = g.blur_chain[ i - 1 ];

				ctx->PSSetShaderResources( 0, 1, &null_srv );
				ctx->OMSetRenderTargets( 1, dst.rtv.GetAddressOf( ), nullptr );

				vp.Width = static_cast< float >( dst.w );
				vp.Height = static_cast< float >( dst.h );
				ctx->RSSetViewports( 1, &vp );

				update_blur_cb( static_cast< float >( src.w ), static_cast< float >( src.h ) );
				ctx->PSSetConstantBuffers( 0, 1, g.blur_cb.GetAddressOf( ) );
				ctx->PSSetShaderResources( 0, 1, src.srv.GetAddressOf( ) );
				ctx->Draw( 3, 0 );
			}

			ctx->PSSetShaderResources( 0, 1, &null_srv );
			ctx->OMSetRenderTargets( 1, orig_rtv.GetAddressOf( ), orig_dsv.Get( ) );
			ctx->RSSetViewports( 1, &orig_vp );
		}

		static void run_glow_blur( int passes )
		{
			auto* ctx = g.context.Get( );
			passes = std::clamp( passes, 1, state::k_blur_iterations );

			ComPtr<ID3D11RenderTargetView> orig_rtv{};
			ComPtr<ID3D11DepthStencilView> orig_dsv{};
			ctx->OMGetRenderTargets( 1, &orig_rtv, &orig_dsv );

			D3D11_VIEWPORT orig_vp{};
			UINT num_vp{ 1 };
			ctx->RSGetViewports( &num_vp, &orig_vp );

			ctx->IASetInputLayout( nullptr );
			ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			ctx->VSSetShader( g.blur_vs.Get( ), nullptr, 0 );
			ctx->PSSetSamplers( 0, 1, g.sampler.GetAddressOf( ) );

			constexpr float bf[ 4 ]{};
			ctx->OMSetBlendState( g.blur_blend.Get( ), bf, 0xFFFFFFFF );
			ctx->RSSetState( g.blur_rasterizer.Get( ) );

			ID3D11ShaderResourceView* null_srv{};
			D3D11_VIEWPORT vp{};
			vp.MaxDepth = 1.0f;

			ctx->PSSetShader( g.blur_downsample_ps.Get( ), nullptr, 0 );

			for ( auto i = 0; i < passes; ++i )
			{
				auto& dst = g.blur_chain[ i ];
				auto* src_srv = i == 0 ? g.glow_srv.Get( ) : g.blur_chain[ i - 1 ].srv.Get( );
				const auto src_w = static_cast< float >( i == 0 ? g.blur_cached_w : g.blur_chain[ i - 1 ].w );
				const auto src_h = static_cast< float >( i == 0 ? g.blur_cached_h : g.blur_chain[ i - 1 ].h );

				ctx->PSSetShaderResources( 0, 1, &null_srv );
				ctx->OMSetRenderTargets( 1, dst.rtv.GetAddressOf( ), nullptr );

				vp.Width = static_cast< float >( dst.w );
				vp.Height = static_cast< float >( dst.h );
				ctx->RSSetViewports( 1, &vp );

				update_blur_cb( src_w, src_h );
				ctx->PSSetConstantBuffers( 0, 1, g.blur_cb.GetAddressOf( ) );
				ctx->PSSetShaderResources( 0, 1, &src_srv );
				ctx->Draw( 3, 0 );
			}

			ctx->PSSetShader( g.blur_upsample_ps.Get( ), nullptr, 0 );

			for ( int i = passes - 1; i > 0; --i )
			{
				auto& src = g.blur_chain[ i ];
				auto& dst = g.blur_chain[ i - 1 ];

				ctx->PSSetShaderResources( 0, 1, &null_srv );
				ctx->OMSetRenderTargets( 1, dst.rtv.GetAddressOf( ), nullptr );

				vp.Width = static_cast< float >( dst.w );
				vp.Height = static_cast< float >( dst.h );
				ctx->RSSetViewports( 1, &vp );

				update_blur_cb( static_cast< float >( src.w ), static_cast< float >( src.h ) );
				ctx->PSSetConstantBuffers( 0, 1, g.blur_cb.GetAddressOf( ) );
				ctx->PSSetShaderResources( 0, 1, src.srv.GetAddressOf( ) );
				ctx->Draw( 3, 0 );
			}

			ctx->PSSetShaderResources( 0, 1, &null_srv );
			ctx->OMSetRenderTargets( 1, orig_rtv.GetAddressOf( ), orig_dsv.Get( ) );
			ctx->RSSetViewports( 1, &orig_vp );
		}

		static void render_draw_list_to_rt( draw_list& dl, int rt_w, int rt_h )
		{
			if ( dl.vertices.empty( ) || dl.commands.empty( ) )
			{
				return;
			}

			auto* ctx = g.context.Get( );

			const auto vtx_bytes = static_cast< std::uint32_t >( dl.vertices.size( ) ) * static_cast< std::uint32_t >( sizeof( vertex ) );
			const auto idx_bytes = static_cast< std::uint32_t >( dl.indices.size( ) ) * static_cast< std::uint32_t >( sizeof( std::uint32_t ) );

			grow_buffer( g.vb, g.vb_capacity, vtx_bytes, D3D11_BIND_VERTEX_BUFFER );
			grow_buffer( g.ib, g.ib_capacity, idx_bytes, D3D11_BIND_INDEX_BUFFER );

			D3D11_MAPPED_SUBRESOURCE vtx_map{}, idx_map{};

			if ( FAILED( ctx->Map( g.vb.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_map ) ) )
			{
				return;
			}

			if ( FAILED( ctx->Map( g.ib.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_map ) ) )
			{
				ctx->Unmap( g.vb.Get( ), 0 );
				return;
			}

			std::memcpy( vtx_map.pData, dl.vertices.data( ), vtx_bytes );
			std::memcpy( idx_map.pData, dl.indices.data( ), idx_bytes );

			ctx->Unmap( g.vb.Get( ), 0 );
			ctx->Unmap( g.ib.Get( ), 0 );

			D3D11_RECT full_scissor{};
			full_scissor.right = static_cast< LONG >( rt_w );
			full_scissor.bottom = static_cast< LONG >( rt_h );

			ctx->IASetInputLayout( g.layout.Get( ) );
			ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

			const std::uint32_t stride = sizeof( vertex );
			const std::uint32_t zero{};
			ctx->IASetVertexBuffers( 0, 1, g.vb.GetAddressOf( ), &stride, &zero );
			ctx->IASetIndexBuffer( g.ib.Get( ), DXGI_FORMAT_R32_UINT, 0 );

			ctx->VSSetShader( g.vs.Get( ), nullptr, 0 );
			ctx->VSSetConstantBuffers( 0, 1, g.cb.GetAddressOf( ) );
			ctx->PSSetShader( g.ps.Get( ), nullptr, 0 );
			ctx->PSSetSamplers( 0, 1, g.sampler.GetAddressOf( ) );

			ctx->RSSetState( g.rasterizer.Get( ) );
			ctx->RSSetScissorRects( 1, &full_scissor );

			constexpr float bf[ 4 ]{};
			ctx->OMSetBlendState( g.blend.Get( ), bf, 0xFFFFFFFF );
			ctx->OMSetDepthStencilState( g.depth.Get( ), 0 );

			ID3D11ShaderResourceView* bound_tex{};

			for ( auto& cmd : dl.commands )
			{
				if ( cmd.idx_count == 0 )
				{
					continue;
				}

				if ( cmd.has_scissor )
				{
					auto s = cmd.scissor;
					s.left = std::max( full_scissor.left, s.left );
					s.top = std::max( full_scissor.top, s.top );
					s.right = std::min( full_scissor.right, s.right );
					s.bottom = std::min( full_scissor.bottom, s.bottom );

					if ( s.right <= s.left || s.bottom <= s.top )
					{
						continue;
					}

					ctx->RSSetScissorRects( 1, &s );
				}
				else
				{
					ctx->RSSetScissorRects( 1, &full_scissor );
				}

				if ( cmd.texture != bound_tex )
				{
					ctx->PSSetShaderResources( 0, 1, &cmd.texture );
					bound_tex = cmd.texture;
				}

				ctx->DrawIndexed( cmd.idx_count, cmd.idx_offset, 0 );
			}
		}

		static char32_t decode_utf8( const char*& p, const char* end )
		{
			if ( p >= end )
			{
				return 0;
			}

			auto b = static_cast< std::uint8_t >( *p );
			char32_t cp;
			int extra;

			if ( b < 0x80 ) { cp = b; extra = 0; }
			else if ( b < 0xc0 ) { ++p; return 0xfffd; }
			else if ( b < 0xe0 ) { cp = b & 0x1f; extra = 1; }
			else if ( b < 0xf0 ) { cp = b & 0x0f; extra = 2; }
			else if ( b < 0xf8 ) { cp = b & 0x07; extra = 3; }
			else { ++p; return 0xfffd; }

			++p;

			for ( auto i = 0; i < extra; ++i )
			{
				if ( p >= end )
				{
					return 0xfffd;
				}

				auto cont = static_cast< std::uint8_t >( *p );

				if ( ( cont & 0xc0 ) != 0x80 )
				{
					return 0xfffd;
				}

				cp = ( cp << 6 ) | ( cont & 0x3f );
				++p;
			}

			return cp;
		}

	} // namespace detail

	font::~font( )
	{
		if ( this->ft_face )
		{
			FT_Done_Face( static_cast< FT_Face >( this->ft_face ) );
		}

		if ( this->ft_library )
		{
			FT_Done_FreeType( static_cast< FT_Library >( this->ft_library ) );
		}
	}

	bool font::has_glyph( char32_t cp ) const
	{
		if ( !this->ft_face )
		{
			return false;
		}

		return FT_Get_Char_Index( static_cast< FT_Face >( this->ft_face ), cp ) != 0;
	}

	const glyph& font::get( char32_t cp )
	{
		const auto it = this->glyph_cache.find( cp );
		if ( it != this->glyph_cache.end( ) )
		{
			return it->second;
		}

		if ( this->rasterize( cp ) )
		{
			return this->glyph_cache[ cp ];
		}

		return this->missing_glyph;
	}

	std::pair<font*, const glyph*> font::resolve( char32_t cp )
	{
		const auto it = this->glyph_cache.find( cp );
		if ( it != this->glyph_cache.end( ) )
		{
			return { this, &it->second };
		}

		if ( this->has_glyph( cp ) )
		{
			if ( this->rasterize( cp ) )
			{
				return { this, &this->glyph_cache[ cp ] };
			}
		}

		if ( this->fallback )
		{
			return this->fallback->resolve( cp );
		}

		return { this, &this->missing_glyph };
	}

	std::pair<float, float> font::measure( std::string_view str )
	{
		auto w{ 0.0f };
		auto h{ 0.0f };
		auto line_w{ 0.0f };

		const char* p = str.data( );
		const char* end = p + str.size( );

		while ( p < end )
		{
			const auto cp = detail::decode_utf8( p, end );
			if ( cp == U'\n' )
			{
				w = std::max( w, line_w );
				h += this->line_height;
				line_w = 0.0f;
				continue;
			}

			if ( cp < 32 )
			{
				continue;
			}

			auto [owner, gl] = this->resolve( cp );
			line_w += gl->advance;
		}

		w = std::max( w, line_w );

		if ( line_w > 0.0f || h == 0.0f )
		{
			h += this->line_height;
		}

		return { std::ceil( w ), h };
	}

	void font::flush_atlas( )
	{
		if ( !this->atlas_dirty || !this->atlas_tex )
		{
			return;
		}

		ComPtr<ID3D11Device> dev{};
		this->atlas_tex->GetDevice( &dev );

		ComPtr<ID3D11DeviceContext> ctx{};
		dev->GetImmediateContext( &ctx );

		D3D11_BOX box{};
		box.left = 0;
		box.top = 0;
		box.right = static_cast< UINT >( this->atlas_w );
		box.bottom = static_cast< UINT >( this->atlas_h );
		box.front = 0;
		box.back = 1;

		ctx->UpdateSubresource( this->atlas_tex.Get( ), 0, &box, this->atlas_bitmap.data( ), static_cast< UINT >( this->atlas_w * 4 ), 0 );

		this->atlas_dirty = false;
	}

	bool font::rasterize( char32_t cp )
	{
		auto face = static_cast< FT_Face >( this->ft_face );
		if ( !face )
		{
			return false;
		}

		if ( FT_Load_Char( face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_FORCE_AUTOHINT ) != 0 )
		{
			return false;
		}

		auto& slot = face->glyph;
		auto& bmp = slot->bitmap;
		const auto gw = static_cast< int >( bmp.width );
		const auto gh = static_cast< int >( bmp.rows );

		if ( this->pen_x + gw + 1 > this->atlas_w )
		{
			this->pen_x = 1;
			this->pen_y += row_h + 1;
			this->row_h = 0;
		}

		if ( this->pen_y + gh + 1 > this->atlas_h )
		{
			glyph gl{};
			gl.advance = static_cast< float >( slot->advance.x ) / 64.0f;
			this->glyph_cache[ cp ] = gl;
			return true;
		}

		for ( int y = 0; y < gh; ++y )
		{
			for ( int x = 0; x < gw; ++x )
			{
				const auto dst = static_cast< std::size_t >( ( this->pen_y + y ) * this->atlas_w + ( this->pen_x + x ) ) * 4;
				this->atlas_bitmap[ dst + 0 ] = 255;
				this->atlas_bitmap[ dst + 1 ] = 255;
				this->atlas_bitmap[ dst + 2 ] = 255;
				this->atlas_bitmap[ dst + 3 ] = bmp.buffer[ y * bmp.pitch + x ];
			}
		}

		glyph gl{};
		gl.advance = static_cast< float >( slot->advance.x ) / 64.0f;
		gl.bearing_x = static_cast< float >( slot->bitmap_left );
		gl.bearing_y = static_cast< float >( slot->bitmap_top );
		gl.width = static_cast< float >( gw );
		gl.height = static_cast< float >( gh );
		gl.atlas_x = static_cast< float >( this->pen_x );
		gl.atlas_y = static_cast< float >( this->pen_y );

		this->glyph_cache[ cp ] = gl;

		this->pen_x += gw + 1;
		this->row_h = std::max( row_h, gh );
		this->atlas_dirty = true;

		return true;
	}

	void draw_list::clear( )
	{
		this->vertices.clear( );
		this->indices.clear( );
		this->commands.clear( );
		this->clip_stack.clear( );
	}

	void draw_list::push_clip( float x, float y, float w, float h )
	{
		D3D11_RECT r{};
		r.left = static_cast< long >( std::floor( x ) );
		r.top = static_cast< long >( std::floor( y ) );
		r.right = static_cast< long >( std::ceil( x + w ) );
		r.bottom = static_cast< long >( std::ceil( y + h ) );

		if ( !this->clip_stack.empty( ) )
		{
			const auto& p = this->clip_stack.back( );
			r.left = std::max( r.left, p.left );
			r.top = std::max( r.top, p.top );
			r.right = std::min( r.right, p.right );
			r.bottom = std::min( r.bottom, p.bottom );
		}

		this->clip_stack.push_back( r );
	}

	void draw_list::push_clip_absolute( float x, float y, float w, float h )
	{
		D3D11_RECT r{};
		r.left = static_cast< long >( std::floor( x ) );
		r.top = static_cast< long >( std::floor( y ) );
		r.right = static_cast< long >( std::ceil( x + w ) );
		r.bottom = static_cast< long >( std::ceil( y + h ) );

		this->clip_stack.push_back( r );
	}

	void draw_list::pop_clip( )
	{
		if ( !this->clip_stack.empty( ) )
		{
			this->clip_stack.pop_back( );
		}
	}

	void draw_list::rect_filled( float x, float y, float w, float h, color col )
	{
		if ( w <= 0.0f || h <= 0.0f || col.a == 0 )
		{
			return;
		}

		this->ensure_cmd( nullptr );

		auto a = this->emit_vtx( x, y, 0, 0, col );
		auto b = this->emit_vtx( x + w, y, 1, 0, col );
		auto c = this->emit_vtx( x + w, y + h, 1, 1, col );
		auto d = this->emit_vtx( x, y + h, 0, 1, col );

		this->emit_quad( a, b, c, d );
	}

	void draw_list::rect_filled( float x, float y, float w, float h, color col, corner_radius rounding, bool aa )
	{
		if ( w <= 0.0f || h <= 0.0f || col.a == 0 )
		{
			return;
		}

		if ( rounding.tl <= 0.5f && rounding.tr <= 0.5f && rounding.br <= 0.5f && rounding.bl <= 0.5f )
		{
			this->rect_filled( x, y, w, h, col );
			return;
		}

		std::vector<float> path{};
		this->build_rounded_rect_path( x, y, w, h, rounding, path );
		this->convex_filled( path, col, aa );
	}

	void draw_list::rect_filled_gradient( float x, float y, float w, float h, color tl, color tr, color br, color bl )
	{
		if ( w <= 0.0f || h <= 0.0f )
		{
			return;
		}

		this->ensure_cmd( nullptr );

		const auto a = this->emit_vtx( x, y, 0, 0, tl );
		const auto b = this->emit_vtx( x + w, y, 1, 0, tr );
		const auto c = this->emit_vtx( x + w, y + h, 1, 1, br );
		const auto d = this->emit_vtx( x, y + h, 0, 1, bl );

		this->emit_quad( a, b, c, d );
	}

	void draw_list::rect_filled_gradient( float x, float y, float w, float h, color tl, color tr, color br, color bl, corner_radius rounding, bool aa )
	{
		if ( w <= 0.0f || h <= 0.0f )
		{
			return;
		}

		if ( rounding.tl <= 0.5f && rounding.tr <= 0.5f && rounding.br <= 0.5f && rounding.bl <= 0.5f )
		{
			this->rect_filled_gradient( x, y, w, h, tl, tr, br, bl );
			return;
		}

		std::vector<float> path{};
		this->build_rounded_rect_path( x, y, w, h, rounding, path );

		const auto count = static_cast< int >( path.size( ) ) / 2;
		if ( count < 3 )
		{
			return;
		}

		const auto inv_w = 1.0f / w;
		const auto inv_h = 1.0f / h;

		auto lerp_color = [ & ]( float px, float py ) -> color
			{
				const auto sx = std::clamp( ( px - x ) * inv_w, 0.0f, 1.0f );
				const auto sy = std::clamp( ( py - y ) * inv_h, 0.0f, 1.0f );

				const auto r = static_cast< std::uint8_t >( tl.r * ( 1 - sx ) * ( 1 - sy ) + tr.r * sx * ( 1 - sy ) + bl.r * ( 1 - sx ) * sy + br.r * sx * sy );
				const auto g = static_cast< std::uint8_t >( tl.g * ( 1 - sx ) * ( 1 - sy ) + tr.g * sx * ( 1 - sy ) + bl.g * ( 1 - sx ) * sy + br.g * sx * sy );
				const auto b_ = static_cast< std::uint8_t >( tl.b * ( 1 - sx ) * ( 1 - sy ) + tr.b * sx * ( 1 - sy ) + bl.b * ( 1 - sx ) * sy + br.b * sx * sy );
				const auto a_ = static_cast< std::uint8_t >( tl.a * ( 1 - sx ) * ( 1 - sy ) + tr.a * sx * ( 1 - sy ) + bl.a * ( 1 - sx ) * sy + br.a * sx * sy );

				return color{ r, g, b_, a_ };
			};

		this->ensure_cmd( nullptr );

		if ( !aa )
		{
			std::uint32_t first{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
				const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
				const auto v = this->emit_vtx( px, py, 0.5f, 0.5f, lerp_color( px, py ) );

				if ( i == 0 )
				{
					first = v;
				}
			}

			for ( auto i = 0; i < count - 2; ++i )
			{
				this->emit_idx( first, first + static_cast< std::uint32_t >( i + 1 ), first + static_cast< std::uint32_t >( i + 2 ) );
			}

			return;
		}

		constexpr auto aa_fringe{ 1.0f };
		constexpr auto aa_half = aa_fringe * 0.5f;

		auto centroid_x{ 0.0f };
		auto centroid_y{ 0.0f };

		for ( auto i = 0; i < count; ++i )
		{
			centroid_x += path[ static_cast< std::size_t >( i ) * 2 ];
			centroid_y += path[ static_cast< std::size_t >( i ) * 2 + 1 ];
		}

		centroid_x /= static_cast< float >( count );
		centroid_y /= static_cast< float >( count );

		std::vector<float> normals( count * 2 );

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto dx = path[ static_cast< std::size_t >( j ) * 2 ] - path[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = path[ static_cast< std::size_t >( j ) * 2 + 1 ] - path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = -dx / len;
			}
		}

		{
			const auto mid_x = ( path[ 0 ] + path[ 2 ] ) * 0.5f;
			const auto mid_y = ( path[ 1 ] + path[ 3 ] ) * 0.5f;
			const auto dot = normals[ 0 ] * ( mid_x - centroid_x ) + normals[ 1 ] * ( mid_y - centroid_y );

			if ( dot < 0.0f )
			{
				for ( auto i = 0; i < count * 2; ++i )
				{
					normals[ i ] = -normals[ i ];
				}
			}
		}

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );

		for ( auto i = 0; i < count; ++i )
		{
			const auto prev = ( i + count - 1 ) % count;
			auto nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
			auto ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;
			auto miter{ 1.0f };

			const auto dm = std::sqrt( nx * nx + ny * ny );
			if ( dm > 0.0001f )
			{
				miter = std::min( 1.0f / dm, 4.0f );
				nx /= dm;
				ny /= dm;
			}

			const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto inset = aa_half * miter;
			const auto outset = aa_half * miter;

			const auto col = lerp_color( px, py );

			this->emit_vtx( px - nx * inset, py - ny * inset, 0.5f, 0.5f, col );
			this->emit_vtx( px + nx * outset, py + ny * outset, 0.5f, 0.5f, col.alpha( 0 ) );
		}

		const auto inner0 = base;

		for ( auto i = 0; i < count - 2; ++i )
		{
			this->emit_idx( inner0, base + static_cast< std::uint32_t >( ( i + 1 ) * 2 ), base + static_cast< std::uint32_t >( ( i + 2 ) * 2 ) );
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto ci = base + static_cast< std::uint32_t >( i * 2 );
			const auto ni = base + static_cast< std::uint32_t >( j * 2 );

			this->emit_quad( ci, ni, ni + 1, ci + 1 );
		}
	}

	void draw_list::rect_filled_blurred( float x, float y, float w, float h, color tint )
	{
		if ( w <= 0.0f || h <= 0.0f || tint.a == 0 )
		{
			return;
		}

		auto* blur_srv = detail::g.blur_chain[ 0 ].srv.Get( );
		if ( !blur_srv )
		{
			return;
		}

		const auto vp_w = static_cast< float >( detail::g.blur_cached_w );
		const auto vp_h = static_cast< float >( detail::g.blur_cached_h );

		if ( vp_w <= 0.0f || vp_h <= 0.0f )
		{
			return;
		}

		const auto u0 = x / vp_w;
		const auto v0 = y / vp_h;
		const auto u1 = ( x + w ) / vp_w;
		const auto v1 = ( y + h ) / vp_h;

		this->ensure_cmd( blur_srv );

		auto a = this->emit_vtx( x, y, u0, v0, tint );
		auto b = this->emit_vtx( x + w, y, u1, v0, tint );
		auto c = this->emit_vtx( x + w, y + h, u1, v1, tint );
		auto d = this->emit_vtx( x, y + h, u0, v1, tint );
		this->emit_quad( a, b, c, d );
	}

	void draw_list::rect_filled_blurred( float x, float y, float w, float h, corner_radius rounding, color tint, bool aa )
	{
		if ( w <= 0.0f || h <= 0.0f || tint.a == 0 )
		{
			return;
		}

		if ( rounding.tl <= 0.5f && rounding.tr <= 0.5f && rounding.br <= 0.5f && rounding.bl <= 0.5f )
		{
			this->rect_filled_blurred( x, y, w, h, tint );
			return;
		}

		auto blur_srv = detail::g.blur_chain[ 0 ].srv.Get( );
		if ( !blur_srv )
		{
			return;
		}

		const auto vp_w = static_cast< float >( detail::g.blur_cached_w );
		const auto vp_h = static_cast< float >( detail::g.blur_cached_h );

		if ( vp_w <= 0.0f || vp_h <= 0.0f )
		{
			return;
		}

		std::vector<float> path{};
		this->build_rounded_rect_path( x, y, w, h, rounding, path );

		const auto count = static_cast< int >( path.size( ) ) / 2;
		if ( count < 3 )
		{
			return;
		}

		this->ensure_cmd( blur_srv );

		if ( !aa )
		{
			std::uint32_t first{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
				const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
				const auto vtx = this->emit_vtx( px, py, px / vp_w, py / vp_h, tint );

				if ( i == 0 )
				{
					first = vtx;
				}
			}

			for ( auto i = 0; i < count - 2; ++i )
			{
				this->emit_idx( first, first + static_cast< std::uint32_t >( i + 1 ), first + static_cast< std::uint32_t >( i + 2 ) );
			}

			return;
		}

		constexpr auto aa_fringe{ 1.0f };
		constexpr auto aa_half = aa_fringe * 0.5f;

		const auto transparent = tint.alpha( 0 );

		auto centroid_x{ 0.0f };
		auto centroid_y{ 0.0f };

		for ( auto i = 0; i < count; ++i )
		{
			centroid_x += path[ static_cast< std::size_t >( i ) * 2 ];
			centroid_y += path[ static_cast< std::size_t >( i ) * 2 + 1 ];
		}

		centroid_x /= static_cast< float >( count );
		centroid_y /= static_cast< float >( count );

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );
		this->emit_vtx( centroid_x, centroid_y, centroid_x / vp_w, centroid_y / vp_h, tint );

		std::vector<float> normals( count * 2 );

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto dx = path[ static_cast< std::size_t >( j ) * 2 ] - path[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = path[ static_cast< std::size_t >( j ) * 2 + 1 ] - path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = -dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = dx / len;
			}
		}

		{
			const auto mid_x = ( path[ 0 ] + path[ 2 ] ) * 0.5f;
			const auto mid_y = ( path[ 1 ] + path[ 3 ] ) * 0.5f;
			const auto dot = normals[ 0 ] * ( mid_x - centroid_x ) + normals[ 1 ] * ( mid_y - centroid_y );

			if ( dot < 0.0f )
			{
				for ( auto i = 0; i < count * 2; ++i )
				{
					normals[ i ] = -normals[ i ];
				}
			}
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto prev = ( i + count - 1 ) % count;

			auto nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
			auto ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;
			auto miter{ 1.0f };

			const auto dm = std::sqrt( nx * nx + ny * ny );
			if ( dm > 0.0001f )
			{
				miter = std::min( 1.0f / dm, 4.0f );
				nx /= dm;
				ny /= dm;
			}

			const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto offset = aa_half * miter;

			const auto inner_x = px - nx * offset;
			const auto inner_y = py - ny * offset;
			const auto outer_x = px + nx * offset;
			const auto outer_y = py + ny * offset;

			this->emit_vtx( inner_x, inner_y, inner_x / vp_w, inner_y / vp_h, tint );
			this->emit_vtx( outer_x, outer_y, outer_x / vp_w, outer_y / vp_h, transparent );
		}

		const auto center = base;
		for ( auto i = 0; i < count; ++i )
		{
			const auto next = ( i + 1 ) % count;
			const auto curr_inner = base + 1 + static_cast< std::uint32_t >( i * 2 );
			const auto next_inner = base + 1 + static_cast< std::uint32_t >( next * 2 );

			this->emit_idx( center, curr_inner, next_inner );
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto next = ( i + 1 ) % count;
			const auto ci = base + 1 + static_cast< std::uint32_t >( i * 2 );
			const auto co = ci + 1;
			const auto ni = base + 1 + static_cast< std::uint32_t >( next * 2 );
			const auto no_ = ni + 1;

			this->emit_quad( ci, ni, no_, co );
		}
	}

	void draw_list::circle_filled( float cx, float cy, float radius, color col, int segments, bool aa )
	{
		if ( radius <= 0.0f || col.a == 0 )
		{
			return;
		}

		if ( segments <= 0 )
		{
			segments = this->auto_segments( radius );
		}

		const auto step = 2.0f * std::numbers::pi_v<float> / static_cast< float >( segments );
		std::vector<float> pts( segments * 2 );

		for ( auto i = 0; i < segments; ++i )
		{
			const auto a = step * static_cast< float >( i );
			pts[ static_cast< std::size_t >( i ) * 2 ] = cx + std::cos( a ) * radius;
			pts[ static_cast< std::size_t >( i ) * 2 + 1 ] = cy + std::sin( a ) * radius;
		}

		this->convex_filled( pts, col, aa );
	}

	void draw_list::triangle_filled( float x0, float y0, float x1, float y1, float x2, float y2, color col, bool aa )
	{
		if ( col.a == 0 )
		{
			return;
		}

		if ( aa )
		{
			const float pts[ ]{ x0, y0, x1, y1, x2, y2 };
			this->convex_filled( std::span<const float>{ pts, 6 }, col, true );
			return;
		}

		this->ensure_cmd( nullptr );

		const auto a = this->emit_vtx( x0, y0, 0, 0, col );
		const auto b = this->emit_vtx( x1, y1, 0, 0, col );
		const auto c = this->emit_vtx( x2, y2, 0, 0, col );

		this->emit_idx( a, b, c );
	}

	void draw_list::convex_filled( std::span<const float> points, color col, bool aa )
	{
		const auto count = static_cast< int >( points.size( ) ) / 2;
		if ( count < 3 || col.a == 0 )
		{
			return;
		}

		this->ensure_cmd( nullptr );

		if ( !aa )
		{
			std::uint32_t first{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto v = this->emit_vtx( points[ i * 2 ], points[ i * 2 + 1 ], 0.5f, 0.5f, col );

				if ( i == 0 )
				{
					first = v;
				}
			}

			for ( auto i = 0; i < count - 2; ++i )
			{
				this->emit_idx( first, first + static_cast< std::uint32_t >( i + 1 ), first + static_cast< std::uint32_t >( i + 2 ) );
			}

			return;
		}

		constexpr auto aa_fringe{ 1.0f };
		constexpr auto aa_half = aa_fringe * 0.5f;

		const auto transparent = col.alpha( 0 );

		auto centroid_x{ 0.0f };
		auto centroid_y{ 0.0f };

		for ( auto i = 0; i < count; ++i )
		{
			centroid_x += points[ static_cast< std::size_t >( i ) * 2 ];
			centroid_y += points[ static_cast< std::size_t >( i ) * 2 + 1 ];
		}

		centroid_x /= static_cast< float >( count );
		centroid_y /= static_cast< float >( count );

		std::vector<float> normals( count * 2 );

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto dx = points[ static_cast< std::size_t >( j ) * 2 ] - points[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = points[ static_cast< std::size_t >( j ) * 2 + 1 ] - points[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = -dx / len;
			}
		}

		{
			const auto mid_x = ( points[ 0 ] + points[ 2 ] ) * 0.5f;
			const auto mid_y = ( points[ 1 ] + points[ 3 ] ) * 0.5f;
			const auto to_mid_x = mid_x - centroid_x;
			const auto to_mid_y = mid_y - centroid_y;
			const auto dot = normals[ 0 ] * to_mid_x + normals[ 1 ] * to_mid_y;

			if ( dot < 0.0f )
			{
				for ( auto i = 0; i < count * 2; ++i )
				{
					normals[ i ] = -normals[ i ];
				}
			}
		}

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );

		for ( auto i = 0; i < count; ++i )
		{
			const auto prev = ( i + count - 1 ) % count;

			auto nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
			auto ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;
			auto miter{ 1.0f };

			const auto dm = std::sqrt( nx * nx + ny * ny );
			if ( dm > 0.0001f )
			{
				miter = 1.0f / dm;
				nx /= dm;
				ny /= dm;
			}

			miter = std::min( miter, 4.0f );

			const auto px = points[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = points[ static_cast< std::size_t >( i ) * 2 + 1 ];

			const auto inset = aa_half * miter;
			const auto outset = aa_half * miter;

			this->emit_vtx( px - nx * inset, py - ny * inset, 0.5f, 0.5f, col );
			this->emit_vtx( px + nx * outset, py + ny * outset, 0.5f, 0.5f, transparent );
		}

		const auto inner0 = base;
		for ( auto i = 0; i < count - 2; ++i )
		{
			this->emit_idx( inner0, base + static_cast< std::uint32_t >( ( i + 1 ) * 2 ), base + static_cast< std::uint32_t >( ( i + 2 ) * 2 ) );
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto ci = base + static_cast< std::uint32_t >( i * 2 );
			const auto co = ci + 1;
			const auto ni = base + static_cast< std::uint32_t >( j * 2 );
			const auto no_ = ni + 1;

			this->emit_quad( ci, ni, no_, co );
		}
	}

	void draw_list::rect( float x, float y, float w, float h, color col, float thickness, bool aa )
	{
		if ( w <= 0.0f || h <= 0.0f || col.a == 0 || thickness <= 0.0f )
		{
			return;
		}

		const float pts[ ]{ x, y, x + w, y, x + w, y + h, x, y + h };
		this->polyline( std::span<const float>{ pts, 8 }, col, true, thickness, aa );
	}

	void draw_list::rect( float x, float y, float w, float h, color col, corner_radius rounding, float thickness, bool aa )
	{
		if ( w <= 0.0f || h <= 0.0f || col.a == 0 || thickness <= 0.0f )
		{
			return;
		}

		if ( rounding.tl <= 0.5f && rounding.tr <= 0.5f && rounding.br <= 0.5f && rounding.bl <= 0.5f )
		{
			this->rect( x, y, w, h, col, thickness, aa );
			return;
		}

		std::vector<float> path{};
		this->build_rounded_rect_path( x, y, w, h, rounding, path );
		this->polyline( path, col, true, thickness, aa );
	}

	void draw_list::circle( float cx, float cy, float radius, color col, float thickness, int segments, bool aa )
	{
		if ( radius <= 0.0f || col.a == 0 || thickness <= 0.0f )
		{
			return;
		}

		if ( segments <= 0 )
		{
			segments = this->auto_segments( radius );
		}

		const auto step = 2.0f * std::numbers::pi_v<float> / static_cast< float >( segments );
		std::vector<float> pts( segments * 2 );

		for ( auto i = 0; i < segments; ++i )
		{
			const auto a = step * static_cast< float >( i );
			pts[ static_cast< std::size_t >( i ) * 2 ] = cx + std::cos( a ) * radius;
			pts[ static_cast< std::size_t >( i ) * 2 + 1 ] = cy + std::sin( a ) * radius;
		}

		this->polyline( pts, col, true, thickness, aa );
	}

	void draw_list::line( float x0, float y0, float x1, float y1, color col, float thickness, bool aa )
	{
		if ( col.a == 0 || thickness <= 0.0f )
		{
			return;
		}

		const auto dx = x1 - x0;
		const auto dy = y1 - y0;
		const auto len = std::sqrt( dx * dx + dy * dy );

		if ( len < 0.0001f )
		{
			return;
		}

		if ( aa )
		{
			const float pts[ ]{ x0, y0, x1, y1 };
			this->polyline( std::span<const float>{ pts, 4 }, col, false, thickness, true );
			return;
		}

		const auto nx = -dy / len;
		const auto ny = dx / len;
		const auto half = thickness * 0.5f;

		this->ensure_cmd( nullptr );

		const auto a = this->emit_vtx( x0 + nx * half, y0 + ny * half, 0, 0, col );
		const auto b = this->emit_vtx( x1 + nx * half, y1 + ny * half, 1, 0, col );
		const auto c = this->emit_vtx( x1 - nx * half, y1 - ny * half, 1, 1, col );
		const auto d = this->emit_vtx( x0 - nx * half, y0 - ny * half, 0, 1, col );

		this->emit_quad( a, b, c, d );
	}

	void draw_list::polyline( std::span<const float> points, color col, bool closed, float thickness, bool aa )
	{
		const auto count = static_cast< int >( points.size( ) ) / 2;
		if ( count < 2 || col.a == 0 || thickness <= 0.0f )
		{
			return;
		}

		const auto seg_count = closed ? count : ( count - 1 );

		this->ensure_cmd( nullptr );

		std::vector<float> normals( seg_count * 2 );

		for ( auto i = 0; i < seg_count; ++i )
		{
			const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
			const auto dx = points[ static_cast< std::size_t >( j ) * 2 ] - points[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = points[ static_cast< std::size_t >( j ) * 2 + 1 ] - points[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = -dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = dx / len;
			}
		}

		auto get_normal = [ & ]( int i, float& out_nx, float& out_ny, float& out_miter )
			{
				if ( closed )
				{
					const auto prev = ( ( i - 1 ) + seg_count ) % seg_count;
					out_nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ ( static_cast< std::size_t >( i ) % seg_count ) * 2 ] ) * 0.5f;
					out_ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ ( static_cast< std::size_t >( i ) % seg_count ) * 2 + 1 ] ) * 0.5f;
				}
				else if ( i == 0 )
				{
					out_nx = normals[ 0 ];
					out_ny = normals[ 1 ];
				}
				else if ( i == count - 1 )
				{
					out_nx = normals[ ( static_cast< std::size_t >( seg_count ) - 1 ) * 2 ];
					out_ny = normals[ ( static_cast< std::size_t >( seg_count ) - 1 ) * 2 + 1 ];
				}
				else
				{
					out_nx = ( normals[ ( static_cast< std::size_t >( i ) - 1 ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
					out_ny = ( normals[ ( static_cast< std::size_t >( i ) - 1 ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;
				}

				const auto dm = std::sqrt( out_nx * out_nx + out_ny * out_ny );
				if ( dm > 0.0001f )
				{
					out_nx /= dm;
					out_ny /= dm;
					out_miter = std::min( 1.0f / dm, 4.0f );
				}
				else
				{
					out_miter = 1.0f;
				}
			};

		if ( !aa )
		{
			const auto half = thickness * 0.5f;
			std::uint32_t first_top{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto px = points[ static_cast< std::size_t >( i ) * 2 ];
				const auto py = points[ static_cast< std::size_t >( i ) * 2 + 1 ];

				float nx, ny, miter;
				get_normal( i, nx, ny, miter );

				const auto ext = half * miter;

				auto top = this->emit_vtx( px + nx * ext, py + ny * ext, 0, 0, col );
				this->emit_vtx( px - nx * ext, py - ny * ext, 1, 1, col );

				if ( i == 0 )
				{
					first_top = top;
				}
			}

			for ( auto i = 0; i < seg_count; ++i )
			{
				const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
				const auto ct = first_top + static_cast< std::uint32_t >( i * 2 );
				const auto cb = ct + 1;
				const auto nt = first_top + static_cast< std::uint32_t >( j * 2 );
				const auto nb = nt + 1;

				this->emit_quad( ct, nt, nb, cb );
			}

			return;
		}

		constexpr auto fringe{ 1.0f };
		const auto half = ( thickness - fringe ) * 0.5f;
		const auto half_stroke = std::max( half, 0.0f );
		const auto half_outer = half_stroke + fringe;
		const auto transparent = col.alpha( 0 );

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );

		for ( auto i = 0; i < count; ++i )
		{
			const auto px = points[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = points[ static_cast< std::size_t >( i ) * 2 + 1 ];

			float nx, ny, miter;
			get_normal( i, nx, ny, miter );

			const auto stroke_ext = half_stroke * miter;
			const auto outer_ext = half_outer * miter;

			this->emit_vtx( px + nx * outer_ext, py + ny * outer_ext, 0, 0, transparent );
			this->emit_vtx( px + nx * stroke_ext, py + ny * stroke_ext, 0, 0, col );
			this->emit_vtx( px - nx * stroke_ext, py - ny * stroke_ext, 1, 1, col );
			this->emit_vtx( px - nx * outer_ext, py - ny * outer_ext, 1, 1, transparent );
		}

		for ( auto i = 0; i < seg_count; ++i )
		{
			const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
			const auto ci = base + static_cast< std::uint32_t >( i * 4 );
			const auto ni = base + static_cast< std::uint32_t >( j * 4 );

			this->emit_quad( ci + 0, ci + 1, ni + 1, ni + 0 );
			this->emit_quad( ci + 1, ci + 2, ni + 2, ni + 1 );
			this->emit_quad( ci + 2, ci + 3, ni + 3, ni + 2 );
		}
	}

	void draw_list::polyline_gradient( std::span<const float> points, std::span<const color> colors, bool closed, float thickness, bool aa )
	{
		const auto count = static_cast< int >( points.size( ) ) / 2;
		if ( count < 2 || thickness <= 0.0f || colors.empty( ) )
		{
			return;
		}

		const auto seg_count = closed ? count : ( count - 1 );

		this->ensure_cmd( nullptr );

		std::vector<float> normals( seg_count * 2 );

		for ( auto i = 0; i < seg_count; ++i )
		{
			const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
			const auto dx = points[ static_cast< std::size_t >( j ) * 2 ] - points[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = points[ static_cast< std::size_t >( j ) * 2 + 1 ] - points[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = -dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = dx / len;
			}
		}

		auto get_normal = [ & ]( int i, float& out_nx, float& out_ny, float& out_miter )
			{
				if ( closed )
				{
					const auto prev = ( ( i - 1 ) + seg_count ) % seg_count;
					out_nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ ( static_cast< std::size_t >( i ) % seg_count ) * 2 ] ) * 0.5f;
					out_ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ ( static_cast< std::size_t >( i ) % seg_count ) * 2 + 1 ] ) * 0.5f;
				}
				else if ( i == 0 )
				{
					out_nx = normals[ 0 ];
					out_ny = normals[ 1 ];
				}
				else if ( i == count - 1 )
				{
					out_nx = normals[ ( static_cast< std::size_t >( seg_count ) - 1 ) * 2 ];
					out_ny = normals[ ( static_cast< std::size_t >( seg_count ) - 1 ) * 2 + 1 ];
				}
				else
				{
					out_nx = ( normals[ ( static_cast< std::size_t >( i ) - 1 ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
					out_ny = ( normals[ ( static_cast< std::size_t >( i ) - 1 ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;
				}

				const auto dm = std::sqrt( out_nx * out_nx + out_ny * out_ny );
				if ( dm > 0.0001f )
				{
					out_nx /= dm;
					out_ny /= dm;
					out_miter = std::min( 1.0f / dm, 4.0f );
				}
				else
				{
					out_miter = 1.0f;
				}
			};

		auto get_color = [ & ]( int i ) -> color
			{
				return colors[ std::min( static_cast< std::size_t >( i ), colors.size( ) - 1 ) ];
			};

		if ( !aa )
		{
			const auto half = thickness * 0.5f;
			std::uint32_t first_top{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto px = points[ static_cast< std::size_t >( i ) * 2 ];
				const auto py = points[ static_cast< std::size_t >( i ) * 2 + 1 ];
				const auto c = get_color( i );

				float nx, ny, miter;
				get_normal( i, nx, ny, miter );

				const auto ext = half * miter;

				auto top = this->emit_vtx( px + nx * ext, py + ny * ext, 0, 0, c );
				this->emit_vtx( px - nx * ext, py - ny * ext, 1, 1, c );

				if ( i == 0 )
				{
					first_top = top;
				}
			}

			for ( auto i = 0; i < seg_count; ++i )
			{
				const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
				const auto ct = first_top + static_cast< std::uint32_t >( i * 2 );
				const auto cb = ct + 1;
				const auto nt = first_top + static_cast< std::uint32_t >( j * 2 );
				const auto nb = nt + 1;

				this->emit_quad( ct, nt, nb, cb );
			}

			return;
		}

		constexpr auto fringe{ 1.0f };
		const auto half = ( thickness - fringe ) * 0.5f;
		const auto half_stroke = std::max( half, 0.0f );
		const auto half_outer = half_stroke + fringe;

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );

		for ( auto i = 0; i < count; ++i )
		{
			const auto px = points[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = points[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto c = get_color( i );
			const auto transparent = c.alpha( 0 );

			float nx, ny, miter;
			get_normal( i, nx, ny, miter );

			const auto stroke_ext = half_stroke * miter;
			const auto outer_ext = half_outer * miter;

			this->emit_vtx( px + nx * outer_ext, py + ny * outer_ext, 0, 0, transparent );
			this->emit_vtx( px + nx * stroke_ext, py + ny * stroke_ext, 0, 0, c );
			this->emit_vtx( px - nx * stroke_ext, py - ny * stroke_ext, 1, 1, c );
			this->emit_vtx( px - nx * outer_ext, py - ny * outer_ext, 1, 1, transparent );
		}

		for ( auto i = 0; i < seg_count; ++i )
		{
			const auto j = closed ? ( ( i + 1 ) % count ) : ( i + 1 );
			const auto ci = base + static_cast< std::uint32_t >( i * 4 );
			const auto ni = base + static_cast< std::uint32_t >( j * 4 );

			this->emit_quad( ci + 0, ci + 1, ni + 1, ni + 0 );
			this->emit_quad( ci + 1, ci + 2, ni + 2, ni + 1 );
			this->emit_quad( ci + 2, ci + 3, ni + 3, ni + 2 );
		}
	}

	void draw_list::text( float x, float y, std::string_view str, color col, font* f )
	{
		if ( str.empty( ) || col.a == 0 )
		{
			return;
		}

		if ( !f )
		{
			f = current_font( );
		}

		if ( !f || !f->atlas_srv )
		{
			return;
		}

		auto cx = std::floor( x );
		auto cy = std::floor( y + f->ascent );

		auto p = str.data( );
		auto end = p + str.size( );

		while ( p < end )
		{
			const auto cp = detail::decode_utf8( p, end );
			if ( cp == U'\n' )
			{
				cx = std::floor( x );
				cy += f->line_height;
				continue;
			}

			if ( cp < 32 )
			{
				continue;
			}

			auto [owner, gl] = f->resolve( cp );

			if ( gl->width > 0.0f && gl->height > 0.0f )
			{
				owner->flush_atlas( );

				const auto inv_w = 1.0f / static_cast< float >( owner->atlas_w );
				const auto inv_h = 1.0f / static_cast< float >( owner->atlas_h );

				this->ensure_cmd( owner->atlas_srv.Get( ) );

				const auto gx = cx + gl->bearing_x;
				const auto gy = cy - gl->bearing_y;
				const auto gw = gl->width;
				const auto gh = gl->height;

				const auto u0 = gl->atlas_x * inv_w;
				const auto v0 = gl->atlas_y * inv_h;
				const auto u1 = ( gl->atlas_x + gl->width ) * inv_w;
				const auto v1 = ( gl->atlas_y + gl->height ) * inv_h;

				auto a = this->emit_vtx( gx, gy, u0, v0, col );
				auto b = this->emit_vtx( gx + gw, gy, u1, v0, col );
				auto c2 = this->emit_vtx( gx + gw, gy + gh, u1, v1, col );
				auto d = this->emit_vtx( gx, gy + gh, u0, v1, col );
				this->emit_quad( a, b, c2, d );
			}

			cx += gl->advance;
		}
	}

	void draw_list::text( float x, float y, std::string_view str, color col, text_style style, font* f )
	{
		this->text( x, y, str, col, style, color{}, f );
	}

	void draw_list::text( float x, float y, std::string_view str, color col, text_style style, color shadow_col, font* f )
	{
		if ( str.empty( ) || col.a == 0 )
		{
			return;
		}

		const auto default_shadow = color{ 0, 0, 0, static_cast< std::uint8_t >( static_cast< float >( col.a ) * 0.64f ) };
		const auto& effect_col = shadow_col.a > 0 ? shadow_col : default_shadow;

		switch ( style )
		{
		case text_style::shadowed:
			this->text( x + 1.0f, y + 1.0f, str, effect_col, f );
			break;

		case text_style::outlined:
		{
			constexpr float offsets[ ][ 2 ]
			{
				{ -1.0f,  0.0f }, { 1.0f, 0.0f },
				{  0.0f, -1.0f }, { 0.0f, 1.0f }
			};

			for ( const auto& [ox, oy] : offsets )
			{
				this->text( x + ox, y + oy, str, effect_col, f );
			}

			break;
		}

		default:
			break;
		}

		this->text( x, y, str, col, f );
	}

	void draw_list::image( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, color tint )
	{
		this->image_uv( x, y, w, h, tex, 0, 0, 1, 1, tint );
	}

	void draw_list::image( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, corner_radius rounding, color tint, bool aa )
	{
		if ( !tex || w <= 0.0f || h <= 0.0f || tint.a == 0 )
		{
			return;
		}

		if ( rounding.tl <= 0.5f && rounding.tr <= 0.5f && rounding.br <= 0.5f && rounding.bl <= 0.5f )
		{
			this->image( x, y, w, h, tex, tint );
			return;
		}

		std::vector<float> path{};
		this->build_rounded_rect_path( x, y, w, h, rounding, path );

		const auto count = static_cast< int >( path.size( ) ) / 2;
		if ( count < 3 )
		{
			return;
		}

		const auto inv_w = 1.0f / w;
		const auto inv_h = 1.0f / h;

		this->ensure_cmd( tex );

		if ( !aa )
		{
			std::uint32_t first{};

			for ( auto i = 0; i < count; ++i )
			{
				const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
				const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
				const auto u = std::clamp( ( px - x ) * inv_w, 0.0f, 1.0f );
				const auto v = std::clamp( ( py - y ) * inv_h, 0.0f, 1.0f );
				const auto vtx = this->emit_vtx( px, py, u, v, tint );

				if ( i == 0 )
				{
					first = vtx;
				}
			}

			for ( auto i = 0; i < count - 2; ++i )
			{
				this->emit_idx( first, first + static_cast< std::uint32_t >( i + 1 ), first + static_cast< std::uint32_t >( i + 2 ) );
			}

			return;
		}

		constexpr auto aa_fringe{ 1.0f };
		constexpr auto aa_half = aa_fringe * 0.5f;

		const auto transparent = tint.alpha( 0 );

		auto centroid_x{ 0.0f };
		auto centroid_y{ 0.0f };

		for ( auto i = 0; i < count; ++i )
		{
			centroid_x += path[ static_cast< std::size_t >( i ) * 2 ];
			centroid_y += path[ static_cast< std::size_t >( i ) * 2 + 1 ];
		}

		centroid_x /= static_cast< float >( count );
		centroid_y /= static_cast< float >( count );

		std::vector<float> normals( count * 2 );

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto dx = path[ static_cast< std::size_t >( j ) * 2 ] - path[ static_cast< std::size_t >( i ) * 2 ];
			const auto dy = path[ static_cast< std::size_t >( j ) * 2 + 1 ] - path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto len = std::sqrt( dx * dx + dy * dy );

			if ( len > 0.0001f )
			{
				normals[ static_cast< std::size_t >( i ) * 2 ] = dy / len;
				normals[ static_cast< std::size_t >( i ) * 2 + 1 ] = -dx / len;
			}
		}

		{
			const auto mid_x = ( path[ 0 ] + path[ 2 ] ) * 0.5f;
			const auto mid_y = ( path[ 1 ] + path[ 3 ] ) * 0.5f;
			const auto dot = normals[ 0 ] * ( mid_x - centroid_x ) + normals[ 1 ] * ( mid_y - centroid_y );

			if ( dot < 0.0f )
			{
				for ( auto i = 0; i < count * 2; ++i )
				{
					normals[ i ] = -normals[ i ];
				}
			}
		}

		const auto base = static_cast< std::uint32_t >( this->vertices.size( ) );

		for ( auto i = 0; i < count; ++i )
		{
			const auto prev = ( i + count - 1 ) % count;
			auto nx = ( normals[ static_cast< std::size_t >( prev ) * 2 ] + normals[ static_cast< std::size_t >( i ) * 2 ] ) * 0.5f;
			auto ny = ( normals[ static_cast< std::size_t >( prev ) * 2 + 1 ] + normals[ static_cast< std::size_t >( i ) * 2 + 1 ] ) * 0.5f;

			const auto dm = std::sqrt( nx * nx + ny * ny );
			auto miter{ 1.0f };

			if ( dm > 0.0001f )
			{
				miter = std::min( 1.0f / dm, 4.0f );
				nx /= dm;
				ny /= dm;
			}

			const auto px = path[ static_cast< std::size_t >( i ) * 2 ];
			const auto py = path[ static_cast< std::size_t >( i ) * 2 + 1 ];
			const auto inset = aa_half * miter;
			const auto outset = aa_half * miter;

			const auto u = std::clamp( ( px - x ) * inv_w, 0.0f, 1.0f );
			const auto v = std::clamp( ( py - y ) * inv_h, 0.0f, 1.0f );

			this->emit_vtx( px - nx * inset, py - ny * inset, u, v, tint );
			this->emit_vtx( px + nx * outset, py + ny * outset, u, v, transparent );
		}

		const auto inner0 = base;
		for ( auto i = 0; i < count - 2; ++i )
		{
			this->emit_idx( inner0, base + static_cast< std::uint32_t >( ( i + 1 ) * 2 ), base + static_cast< std::uint32_t >( ( i + 2 ) * 2 ) );
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto j = ( i + 1 ) % count;
			const auto ci = base + static_cast< std::uint32_t >( i * 2 );
			const auto ni = base + static_cast< std::uint32_t >( j * 2 );

			this->emit_quad( ci, ni, ni + 1, ci + 1 );
		}
	}

	void draw_list::image_uv( float x, float y, float w, float h, ID3D11ShaderResourceView* tex, float u0, float v0, float u1, float v1, color tint )
	{
		if ( !tex || w <= 0.0f || h <= 0.0f || tint.a == 0 )
		{
			return;
		}

		this->ensure_cmd( tex );

		const auto a = this->emit_vtx( x, y, u0, v0, tint );
		const auto b = this->emit_vtx( x + w, y, u1, v0, tint );
		const auto c = this->emit_vtx( x + w, y + h, u1, v1, tint );
		const auto d = this->emit_vtx( x, y + h, u0, v1, tint );

		this->emit_quad( a, b, c, d );
	}

	void draw_list::ensure_cmd( ID3D11ShaderResourceView* texture )
	{
		auto tex = texture ? texture : detail::g.white_srv.Get( );
		const auto has_clip = !this->clip_stack.empty( );
		D3D11_RECT clip{};

		if ( has_clip )
		{
			clip = this->clip_stack.back( );
		}

		if ( !this->commands.empty( ) )
		{
			auto& last = this->commands.back( );
			if ( last.texture == tex && last.has_scissor == has_clip )
			{
				if ( !has_clip )
				{
					return;
				}

				if ( last.scissor.left == clip.left && last.scissor.top == clip.top && last.scissor.right == clip.right && last.scissor.bottom == clip.bottom )
				{
					return;
				}
			}
		}

		draw_cmd cmd{};
		cmd.idx_offset = static_cast< std::uint32_t >( this->indices.size( ) );
		cmd.texture = tex;
		cmd.has_scissor = has_clip;
		cmd.scissor = clip;
		this->commands.push_back( cmd );
	}

	std::uint32_t draw_list::emit_vtx( float x, float y, float u, float v, color c )
	{
		auto idx = static_cast< std::uint32_t >( this->vertices.size( ) );
		this->vertices.push_back( { { x, y }, { u, v }, c } );
		return idx;
	}

	void draw_list::emit_idx( std::uint32_t a, std::uint32_t b, std::uint32_t c )
	{
		this->indices.push_back( a );
		this->indices.push_back( b );
		this->indices.push_back( c );
		this->commands.back( ).idx_count += 3;
	}

	void draw_list::emit_quad( std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d )
	{
		this->emit_idx( a, b, c );
		this->emit_idx( a, c, d );
	}

	int draw_list::auto_segments( float radius ) const
	{
		if ( radius <= 0.0f )
		{
			return 4;
		}

		const auto segs = static_cast< int >( std::ceil( std::numbers::pi_v<float> *2.0f * radius / 1.5f ) );
		return std::clamp( segs, 24, 128 );
	}

	void draw_list::build_rounded_rect_path( float x, float y, float w, float h, corner_radius r, std::vector<float>& path ) const
	{
		path.clear( );

		const auto max_r = std::min( w, h ) * 0.5f;
		r.tl = std::clamp( r.tl, 0.0f, max_r );
		r.tr = std::clamp( r.tr, 0.0f, max_r );
		r.br = std::clamp( r.br, 0.0f, max_r );
		r.bl = std::clamp( r.bl, 0.0f, max_r );

		constexpr auto half_pi = std::numbers::pi_v<float> *0.5f;

		auto arc = [ & ]( float cx, float cy, float radius, float start_angle, int segments )
			{
				if ( radius <= 0.5f )
				{
					if ( path.empty( ) || path[ path.size( ) - 2 ] != cx || path[ path.size( ) - 1 ] != cy )
					{
						path.push_back( cx );
						path.push_back( cy );
					}

					return;
				}

				const auto step = half_pi / static_cast< float >( segments );

				for ( auto i = 0; i < segments; ++i )
				{
					const auto a = start_angle + step * static_cast< float >( i );
					path.push_back( cx + std::cos( a ) * radius );
					path.push_back( cy + std::sin( a ) * radius );
				}
			};

		const auto segs_tl = std::max( 1, this->auto_segments( r.tl ) / 4 );
		arc( x + r.tl, y + r.tl, r.tl, std::numbers::pi_v<float>, segs_tl );

		const auto segs_tr = std::max( 1, this->auto_segments( r.tr ) / 4 );
		arc( x + w - r.tr, y + r.tr, r.tr, std::numbers::pi_v<float> +half_pi, segs_tr );

		const auto segs_br = std::max( 1, this->auto_segments( r.br ) / 4 );
		arc( x + w - r.br, y + h - r.br, r.br, 0.0f, segs_br );

		const auto segs_bl = std::max( 1, this->auto_segments( r.bl ) / 4 );
		arc( x + r.bl, y + h - r.bl, r.bl, half_pi, segs_bl );
	}

	void gif_image::update( float dt )
	{
		if ( this->frames.size( ) <= 1 )
		{
			return;
		}

		this->elapsed += dt;

		while ( this->elapsed >= this->frames[ this->current_idx ].delay )
		{
			this->elapsed -= this->frames[ this->current_idx ].delay;
			this->current_idx = ( this->current_idx + 1 ) % static_cast< int >( this->frames.size( ) );
		}
	}

	void gif_image::reset( )
	{
		this->current_idx = 0;
		this->elapsed = 0.0f;
	}

	ID3D11ShaderResourceView* gif_image::current_srv( ) const
	{
		if ( this->composited.empty( ) )
		{
			return nullptr;
		}

		return this->composited[ this->current_idx ].Get( );
	}

	bool initialize( ID3D11Device* device, ID3D11DeviceContext* context )
	{
		if ( !device || !context )
		{
			return false;
		}

		detail::g.device = device;
		detail::g.context = context;

		if ( !detail::create_shaders( ) )
		{
			return false;
		}

		if ( !detail::create_states( ) )
		{
			return false;
		}

		if ( !detail::create_white_texture( ) )
		{
			return false;
		}

		if ( !detail::create_blur_resources( ) )
		{
			return false;
		}

		if ( !detail::create_buffer( device, detail::g.cb, sizeof( float ) * 16, D3D11_BIND_CONSTANT_BUFFER ) )
		{
			return false;
		}

		detail::g.vb_capacity = detail::k_initial_vb_size;
		detail::g.ib_capacity = detail::k_initial_ib_size;

		if ( !detail::create_buffer( device, detail::g.vb, detail::g.vb_capacity, D3D11_BIND_VERTEX_BUFFER ) )
		{
			return false;
		}

		if ( !detail::create_buffer( device, detail::g.ib, detail::g.ib_capacity, D3D11_BIND_INDEX_BUFFER ) )
		{
			return false;
		}

		const auto inter = load_font( std::span<const std::byte>( reinterpret_cast< const std::byte* >( fonts::inter ), sizeof( fonts::inter ) ), 15.0f );
		if ( !inter )
		{
			return false;
		}

		const auto math = load_font( std::span<const std::byte>( reinterpret_cast< const std::byte* >( fonts::noto_math ), sizeof( fonts::noto_math ) ), 15.0f );
		if ( math )
		{
			inter->fallback = math;
			detail::g.math_font = math;
		}

		QueryPerformanceFrequency( &detail::g.perf_freq );
		QueryPerformanceCounter( &detail::g.last_time );

		return true;
	}

	void begin_frame( bool update_timing )
	{
		if ( update_timing )
		{
			LARGE_INTEGER now{};
			QueryPerformanceCounter( &now );

			const auto ticks = now.QuadPart - detail::g.last_time.QuadPart;
			detail::g.dt = static_cast< float >( ticks ) / static_cast< float >( detail::g.perf_freq.QuadPart );
			detail::g.dt = std::min( detail::g.dt, 0.1f );
			detail::g.last_time = now;

			if ( detail::g.dt > 0.0f )
			{
				const auto instant = 1.0f / detail::g.dt;
				detail::g.fps = detail::g.fps * 0.9f + instant * 0.1f;
			}
		}

		for ( auto i = 0; i < 3; ++i )
		{
			detail::g.lists[ i ].clear( );
			detail::g.glow_lists[ i ].clear( );
		}
	}

	void end_frame( )
	{
		auto& d = detail::g;

		D3D11_VIEWPORT vp{};
		UINT vp_count{ 1 };
		d.context->RSGetViewports( &vp_count, &vp );

		D3D11_RECT full_scissor{};
		full_scissor.right = static_cast< LONG >( std::ceil( vp.Width ) );
		full_scissor.bottom = static_cast< LONG >( std::ceil( vp.Height ) );

		auto bb_w{ 0 };
		auto bb_h{ 0 };

		{
			ComPtr<ID3D11RenderTargetView> rtv{};
			d.context->OMGetRenderTargets( 1, &rtv, nullptr );

			if ( rtv )
			{
				ComPtr<ID3D11Resource> res{};
				rtv->GetResource( &res );

				ComPtr<ID3D11Texture2D> bb_tex{};
				if ( res && SUCCEEDED( res.As( &bb_tex ) ) )
				{
					D3D11_TEXTURE2D_DESC desc{};
					bb_tex->GetDesc( &desc );
					bb_w = static_cast< int >( desc.Width );
					bb_h = static_cast< int >( desc.Height );
				}
			}
		}

		if ( bb_w > 0 && bb_h > 0 && ( bb_w != d.blur_cached_w || bb_h != d.blur_cached_h ) )
		{
			detail::create_blur_textures( bb_w, bb_h );
		}

		{
			D3D11_MAPPED_SUBRESOURCE cb_map{};
			if ( SUCCEEDED( d.context->Map( d.cb.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &cb_map ) ) )
			{
				auto m = static_cast< float* >( cb_map.pData );
				std::memset( m, 0, sizeof( float ) * 16 );

				m[ 0 ] = 2.0f / vp.Width;
				m[ 5 ] = 2.0f / -vp.Height;
				m[ 10 ] = 0.5f;
				m[ 12 ] = -1.0f;
				m[ 13 ] = 1.0f;
				m[ 14 ] = 0.5f;
				m[ 15 ] = 1.0f;

				d.context->Unmap( d.cb.Get( ), 0 );
			}
		}

		auto setup_pipeline = [ & ]( )
			{
				d.context->IASetInputLayout( d.layout.Get( ) );
				d.context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

				const std::uint32_t stride = sizeof( vertex );
				const std::uint32_t zero{ 0 };
				d.context->IASetVertexBuffers( 0, 1, d.vb.GetAddressOf( ), &stride, &zero );
				d.context->IASetIndexBuffer( d.ib.Get( ), DXGI_FORMAT_R32_UINT, 0 );

				d.context->VSSetShader( d.vs.Get( ), nullptr, 0 );
				d.context->VSSetConstantBuffers( 0, 1, d.cb.GetAddressOf( ) );
				d.context->PSSetShader( d.ps.Get( ), nullptr, 0 );
				d.context->PSSetSamplers( 0, 1, d.sampler.GetAddressOf( ) );

				d.context->RSSetState( d.rasterizer.Get( ) );
				d.context->RSSetScissorRects( 1, &full_scissor );

				constexpr float bf[ 4 ]{ 0, 0, 0, 0 };
				d.context->OMSetBlendState( d.blend.Get( ), bf, 0xffffffff );
				d.context->OMSetDepthStencilState( d.depth.Get( ), 0 );
			};

		auto blur_srv = d.blur_chain[ 0 ].srv.Get( );

		auto render_layer = [ & ]( draw_list& dl )
			{
				if ( dl.vertices.empty( ) || dl.commands.empty( ) )
				{
					return;
				}

				const auto vtx_bytes = static_cast< std::uint32_t >( dl.vertices.size( ) ) * static_cast< std::uint32_t >( sizeof( vertex ) );
				const auto idx_bytes = static_cast< std::uint32_t >( dl.indices.size( ) ) * static_cast< std::uint32_t >( sizeof( std::uint32_t ) );

				detail::grow_buffer( d.vb, d.vb_capacity, vtx_bytes, D3D11_BIND_VERTEX_BUFFER );
				detail::grow_buffer( d.ib, d.ib_capacity, idx_bytes, D3D11_BIND_INDEX_BUFFER );

				D3D11_MAPPED_SUBRESOURCE vtx_map{}, idx_map{};

				if ( FAILED( d.context->Map( d.vb.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_map ) ) )
				{
					return;
				}

				if ( FAILED( d.context->Map( d.ib.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_map ) ) )
				{
					d.context->Unmap( d.vb.Get( ), 0 );
					return;
				}

				std::memcpy( vtx_map.pData, dl.vertices.data( ), vtx_bytes );
				std::memcpy( idx_map.pData, dl.indices.data( ), idx_bytes );

				d.context->Unmap( d.vb.Get( ), 0 );
				d.context->Unmap( d.ib.Get( ), 0 );

				auto pipeline_dirty{ true };
				auto blur_stale{ true };
				ID3D11ShaderResourceView* bound_tex{ nullptr };
				auto bound_scissor = full_scissor;

				for ( auto i = 0ull; i < dl.commands.size( ); ++i )
				{
					auto& cmd = dl.commands[ i ];

					if ( cmd.idx_count == 0 )
					{
						continue;
					}

					const auto is_blur = blur_srv && cmd.texture == blur_srv;

					if ( is_blur && blur_stale && d.blur_scene_tex )
					{
						detail::run_blur_pass( );
						pipeline_dirty = true;
						blur_stale = false;
					}

					if ( pipeline_dirty )
					{
						setup_pipeline( );
						pipeline_dirty = false;
						bound_tex = nullptr;
						bound_scissor = full_scissor;
					}

					auto scissor = full_scissor;

					if ( cmd.has_scissor )
					{
						scissor.left = std::max( full_scissor.left, cmd.scissor.left );
						scissor.top = std::max( full_scissor.top, cmd.scissor.top );
						scissor.right = std::min( full_scissor.right, cmd.scissor.right );
						scissor.bottom = std::min( full_scissor.bottom, cmd.scissor.bottom );

						if ( scissor.right <= scissor.left || scissor.bottom <= scissor.top )
						{
							continue;
						}
					}

					if ( scissor.left != bound_scissor.left || scissor.top != bound_scissor.top || scissor.right != bound_scissor.right || scissor.bottom != bound_scissor.bottom )
					{
						d.context->RSSetScissorRects( 1, &scissor );
						bound_scissor = scissor;
					}

					if ( cmd.texture != bound_tex )
					{
						d.context->PSSetShaderResources( 0, 1, &cmd.texture );
						bound_tex = cmd.texture;
					}

					d.context->DrawIndexed( cmd.idx_count, cmd.idx_offset, 0 );

					if ( !is_blur )
					{
						blur_stale = true;
					}
				}
			};

		auto has_any_glow{ false };

		for ( auto i = 0; i < 3; ++i )
		{
			if ( !d.glow_lists[ i ].vertices.empty( ) )
			{
				has_any_glow = true;
				break;
			}
		}

		if ( has_any_glow && d.glow_rtv && d.glow_srv && d.blur_cached_w > 0 && d.blur_cached_h > 0 )
		{
			auto* ctx = d.context.Get( );

			ComPtr<ID3D11RenderTargetView> orig_rtv{};
			ComPtr<ID3D11DepthStencilView> orig_dsv{};
			ctx->OMGetRenderTargets( 1, &orig_rtv, &orig_dsv );

			D3D11_VIEWPORT orig_vp{};
			UINT orig_vp_count{ 1 };
			ctx->RSGetViewports( &orig_vp_count, &orig_vp );

			const float clear_col[ 4 ]{};
			ctx->ClearRenderTargetView( d.glow_rtv.Get( ), clear_col );

			ctx->OMSetRenderTargets( 1, d.glow_rtv.GetAddressOf( ), nullptr );

			D3D11_VIEWPORT glow_vp{};
			glow_vp.Width = static_cast< float >( d.blur_cached_w );
			glow_vp.Height = static_cast< float >( d.blur_cached_h );
			glow_vp.MaxDepth = 1.0f;
			ctx->RSSetViewports( 1, &glow_vp );

			for ( auto i = 0; i < 3; ++i )
			{
				detail::render_draw_list_to_rt( d.glow_lists[ i ], d.blur_cached_w, d.blur_cached_h );
			}

			ctx->OMSetRenderTargets( 1, orig_rtv.GetAddressOf( ), orig_dsv.Get( ) );
			ctx->RSSetViewports( 1, &orig_vp );

			detail::run_glow_blur( 3 );

			auto blurred = d.blur_chain[ 0 ].srv.Get( );
			if ( blurred )
			{
				constexpr auto composite_tint = color{ 255, 255, 255, 255 };

				draw_list composite{};
				composite.ensure_cmd( blurred );

				const auto fw = static_cast< float >( d.blur_cached_w );
				const auto fh = static_cast< float >( d.blur_cached_h );

				auto a = composite.emit_vtx( 0, 0, 0, 0, composite_tint );
				auto b = composite.emit_vtx( fw, 0, 1, 0, composite_tint );
				auto c = composite.emit_vtx( fw, fh, 1, 1, composite_tint );
				auto dd = composite.emit_vtx( 0, fh, 0, 1, composite_tint );
				composite.emit_quad( a, b, c, dd );

				const auto cvb = static_cast< std::uint32_t >( composite.vertices.size( ) * sizeof( vertex ) );
				const auto cib = static_cast< std::uint32_t >( composite.indices.size( ) * sizeof( std::uint32_t ) );

				detail::grow_buffer( d.vb, d.vb_capacity, cvb, D3D11_BIND_VERTEX_BUFFER );
				detail::grow_buffer( d.ib, d.ib_capacity, cib, D3D11_BIND_INDEX_BUFFER );

				D3D11_MAPPED_SUBRESOURCE vm{}, im{};

				if ( SUCCEEDED( ctx->Map( d.vb.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &vm ) ) )
				{
					if ( SUCCEEDED( ctx->Map( d.ib.Get( ), 0, D3D11_MAP_WRITE_DISCARD, 0, &im ) ) )
					{
						std::memcpy( vm.pData, composite.vertices.data( ), cvb );
						std::memcpy( im.pData, composite.indices.data( ), cib );
						ctx->Unmap( d.ib.Get( ), 0 );
					}

					ctx->Unmap( d.vb.Get( ), 0 );
				}

				setup_pipeline( );

				constexpr float bf[ 4 ]{};
				ctx->OMSetBlendState( d.glow_additive_blend.Get( ), bf, 0xffffffff );
				ctx->PSSetShaderResources( 0, 1, &blurred );
				ctx->DrawIndexed( composite.commands[ 0 ].idx_count, 0, 0 );

				ID3D11ShaderResourceView* null_srv{};
				ctx->PSSetShaderResources( 0, 1, &null_srv );
				ctx->OMSetBlendState( d.blend.Get( ), bf, 0xffffffff );
			}
		}

		render_layer( d.lists[ 0 ] );
		render_layer( d.lists[ 1 ] );
		render_layer( d.lists[ 2 ] );
	}

	draw_list& get( layer l )
	{
		return detail::g.lists[ static_cast< int >( l ) ];
	}

	draw_list& get_glow( layer l )
	{
		return detail::g.glow_lists[ static_cast< int >( l ) ];
	}

	ComPtr<ID3D11ShaderResourceView> create_srv_from_rgba( const std::uint8_t* pixels, int w, int h )
	{
		D3D11_TEXTURE2D_DESC td{};
		td.Width = static_cast< UINT >( w );
		td.Height = static_cast< UINT >( h );
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = pixels;
		init.SysMemPitch = static_cast< UINT >( w * 4 );

		ComPtr<ID3D11Texture2D> tex{};
		if ( FAILED( detail::g.device->CreateTexture2D( &td, &init, &tex ) ) )
		{
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = td.Format;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;

		ComPtr<ID3D11ShaderResourceView> srv{};
		if ( FAILED( detail::g.device->CreateShaderResourceView( tex.Get( ), &sv, &srv ) ) )
		{
			return nullptr;
		}

		return srv;
	}

	ComPtr<ID3D11ShaderResourceView> load_texture( std::span<const std::byte> data, int* w, int* h )
	{
		static ComPtr<IWICImagingFactory> factory = [ ]
			{
				ComPtr<IWICImagingFactory> f{};
				( void )CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &f ) );
				return f;
			}( );

		if ( !factory )
		{
			return nullptr;
		}

		ComPtr<IWICStream> stream{};
		if ( FAILED( factory->CreateStream( &stream ) ) )
		{
			return nullptr;
		}

		if ( FAILED( stream->InitializeFromMemory( reinterpret_cast< BYTE* >( const_cast< std::byte* >( data.data( ) ) ), static_cast< DWORD >( data.size( ) ) ) ) )
		{
			return nullptr;
		}

		ComPtr<IWICBitmapDecoder> decoder{};
		if ( FAILED( factory->CreateDecoderFromStream( stream.Get( ), nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) )
		{
			return nullptr;
		}

		ComPtr<IWICBitmapFrameDecode> frame{};
		if ( FAILED( decoder->GetFrame( 0, &frame ) ) )
		{
			return nullptr;
		}

		std::uint32_t width, height;
		frame->GetSize( &width, &height );

		ComPtr<IWICFormatConverter> converter;
		if ( FAILED( factory->CreateFormatConverter( &converter ) ) )
		{
			return nullptr;
		}

		if ( FAILED( converter->Initialize( frame.Get( ), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) )
		{
			return nullptr;
		}

		std::vector<BYTE> pixels( width * height * 4 );
		if ( FAILED( converter->CopyPixels( nullptr, width * 4, static_cast< std::uint32_t >( pixels.size( ) ), pixels.data( ) ) ) )
		{
			return nullptr;
		}

		if ( w ) { *w = static_cast< int >( width ); }
		if ( h ) { *h = static_cast< int >( height ); }

		D3D11_TEXTURE2D_DESC td{};
		td.Width = width;
		td.Height = height;
		td.MipLevels = 0;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		ComPtr<ID3D11Texture2D> tex{};
		if ( FAILED( detail::g.device->CreateTexture2D( &td, nullptr, &tex ) ) )
		{
			return nullptr;
		}

		detail::g.context->UpdateSubresource( tex.Get( ), 0, nullptr, pixels.data( ), width * 4, 0 );

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = td.Format;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = static_cast< UINT >( -1 );

		ComPtr<ID3D11ShaderResourceView> srv{};
		if ( FAILED( detail::g.device->CreateShaderResourceView( tex.Get( ), &sv, &srv ) ) )
		{
			return nullptr;
		}

		detail::g.context->GenerateMips( srv.Get( ) );
		return srv;
	}

	ComPtr<ID3D11ShaderResourceView> load_svg( std::span<const std::byte> data, float scale, int* out_width, int* out_height )
	{
		std::string str( reinterpret_cast< const char* >( data.data( ) ), data.size( ) );

		auto image = nsvgParse( str.data( ), "px", 96.0f );
		if ( !image )
		{
			return nullptr;
		}

		const auto width = static_cast< int >( image->width * scale );
		const auto height = static_cast< int >( image->height * scale );

		if ( width <= 0 || height <= 0 )
		{
			nsvgDelete( image );
			return nullptr;
		}

		auto rast = nsvgCreateRasterizer( );
		if ( !rast )
		{
			nsvgDelete( image );
			return nullptr;
		}

		std::vector<unsigned char> pixels( static_cast< std::size_t >( width ) * height * 4 );
		nsvgRasterize( rast, image, 0, 0, scale, pixels.data( ), width, height, width * 4 );
		nsvgDeleteRasterizer( rast );
		nsvgDelete( image );

		if ( out_width ) { *out_width = width; }
		if ( out_height ) { *out_height = height; }

		static ComPtr<IWICImagingFactory> factory = [ ]
			{
				ComPtr<IWICImagingFactory> f{};
				( void )CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &f ) );
				return f;
			}( );

		if ( !factory )
		{
			return nullptr;
		}

		ComPtr<IWICBitmap> wic_bitmap{};
		if ( FAILED( factory->CreateBitmapFromMemory( static_cast< UINT >( width ), static_cast< UINT >( height ), GUID_WICPixelFormat32bppRGBA, static_cast< UINT >( width ) * 4, static_cast< UINT >( pixels.size( ) ), pixels.data( ), &wic_bitmap ) ) )
		{
			return nullptr;
		}

		ComPtr<IStream> mem_stream{};
		if ( FAILED( CreateStreamOnHGlobal( nullptr, TRUE, &mem_stream ) ) )
		{
			return nullptr;
		}

		ComPtr<IWICBitmapEncoder> encoder{};
		if ( FAILED( factory->CreateEncoder( GUID_ContainerFormatPng, nullptr, &encoder ) ) )
		{
			return nullptr;
		}

		if ( FAILED( encoder->Initialize( mem_stream.Get( ), WICBitmapEncoderNoCache ) ) )
		{
			return nullptr;
		}

		ComPtr<IWICBitmapFrameEncode> frame{};
		if ( FAILED( encoder->CreateNewFrame( &frame, nullptr ) ) )
		{
			return nullptr;
		}

		if ( FAILED( frame->Initialize( nullptr ) ) )
		{
			return nullptr;
		}

		if ( FAILED( frame->SetSize( static_cast< UINT >( width ), static_cast< UINT >( height ) ) ) )
		{
			return nullptr;
		}

		auto fmt = GUID_WICPixelFormat32bppRGBA;
		if ( FAILED( frame->SetPixelFormat( &fmt ) ) )
		{
			return nullptr;
		}

		if ( FAILED( frame->WriteSource( wic_bitmap.Get( ), nullptr ) ) )
		{
			return nullptr;
		}

		if ( FAILED( frame->Commit( ) ) || FAILED( encoder->Commit( ) ) )
		{
			return nullptr;
		}

		STATSTG stat{};
		if ( FAILED( mem_stream->Stat( &stat, 1 ) ) )
		{
			return nullptr;
		}

		const auto png_size = static_cast< std::size_t >( stat.cbSize.QuadPart );
		std::vector<std::byte> png_data( png_size );

		LARGE_INTEGER seek{};
		mem_stream->Seek( seek, STREAM_SEEK_SET, nullptr );

		ULONG read{};
		if ( FAILED( mem_stream->Read( png_data.data( ), static_cast< ULONG >( png_size ), &read ) ) )
		{
			return nullptr;
		}

		return load_texture( std::span<const std::byte>{ png_data.data( ), read }, nullptr, nullptr );
	}

	ComPtr<ID3D11ShaderResourceView> load_svg( const char* svg_text, float scale, int* out_width, int* out_height )
	{
		return load_svg( std::span<const std::byte>{ reinterpret_cast< const std::byte* >( svg_text ), std::strlen( svg_text ) }, scale, out_width, out_height );
	}

	gif_image load_gif( std::span<const std::byte> data )
	{
		gif_image result{};

		static ComPtr<IWICImagingFactory> factory = [ ]
			{
				ComPtr<IWICImagingFactory> f{};
				( void )CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &f ) );
				return f;
			}( );

		if ( !factory || !detail::g.device )
		{
			return result;
		}

		ComPtr<IWICStream> stream{};
		if ( FAILED( factory->CreateStream( &stream ) ) )
		{
			return result;
		}

		if ( FAILED( stream->InitializeFromMemory( reinterpret_cast< BYTE* >( const_cast< std::byte* >( data.data( ) ) ), static_cast< DWORD >( data.size( ) ) ) ) )
		{
			return result;
		}

		ComPtr<IWICBitmapDecoder> decoder{};
		if ( FAILED( factory->CreateDecoderFromStream( stream.Get( ), nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) )
		{
			return result;
		}

		UINT frame_count{};
		if ( FAILED( decoder->GetFrameCount( &frame_count ) ) || frame_count == 0 )
		{
			return result;
		}

		{
			ComPtr<IWICMetadataQueryReader> meta{};
			if ( SUCCEEDED( decoder->GetMetadataQueryReader( &meta ) ) )
			{
				PROPVARIANT val{};

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/logscrdesc/Width", &val ) ) && val.vt == VT_UI2 )
				{
					result.canvas_w = val.uiVal;
				}
				PropVariantClear( &val );

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/logscrdesc/Height", &val ) ) && val.vt == VT_UI2 )
				{
					result.canvas_h = val.uiVal;
				}
				PropVariantClear( &val );
			}
		}

		if ( result.canvas_w <= 0 || result.canvas_h <= 0 )
		{
			ComPtr<IWICBitmapFrameDecode> f0{};
			if ( SUCCEEDED( decoder->GetFrame( 0, &f0 ) ) )
			{
				UINT w{}, h{};
				f0->GetSize( &w, &h );
				result.canvas_w = static_cast< int >( w );
				result.canvas_h = static_cast< int >( h );
			}
		}

		if ( result.canvas_w <= 0 || result.canvas_h <= 0 )
		{
			return result;
		}

		struct raw_frame
		{
			std::vector<std::uint8_t> pixels;
			int x{}, y{}, w{}, h{};
			float delay{};
			int disposal{};
		};

		std::vector<raw_frame> raw( frame_count );
		result.frames.resize( frame_count );

		for ( UINT i = 0; i < frame_count; ++i )
		{
			ComPtr<IWICBitmapFrameDecode> frame{};
			if ( FAILED( decoder->GetFrame( i, &frame ) ) )
			{
				return {};
			}

			UINT fw{}, fh{};
			frame->GetSize( &fw, &fh );

			auto& rf = raw[ i ];
			rf.w = static_cast< int >( fw );
			rf.h = static_cast< int >( fh );

			ComPtr<IWICMetadataQueryReader> meta{};
			if ( SUCCEEDED( frame->GetMetadataQueryReader( &meta ) ) )
			{
				PROPVARIANT val{};

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/imgdesc/Left", &val ) ) && val.vt == VT_UI2 )
				{
					rf.x = val.uiVal;
				}
				PropVariantClear( &val );

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/imgdesc/Top", &val ) ) && val.vt == VT_UI2 )
				{
					rf.y = val.uiVal;
				}
				PropVariantClear( &val );

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/grctlext/Delay", &val ) ) && val.vt == VT_UI2 )
				{
					rf.delay = static_cast< float >( val.uiVal ) * 0.01f;
					if ( rf.delay < 0.02f )
					{
						rf.delay = 0.1f;
					}
				}
				else
				{
					rf.delay = 0.1f;
				}
				PropVariantClear( &val );

				PropVariantInit( &val );
				if ( SUCCEEDED( meta->GetMetadataByName( L"/grctlext/Disposal", &val ) ) && val.vt == VT_UI1 )
				{
					rf.disposal = val.bVal;
				}
				PropVariantClear( &val );
			}
			else
			{
				rf.delay = 0.1f;
			}

			ComPtr<IWICFormatConverter> converter{};
			if ( FAILED( factory->CreateFormatConverter( &converter ) ) )
			{
				return {};
			}

			if ( FAILED( converter->Initialize( frame.Get( ), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) )
			{
				return {};
			}

			rf.pixels.resize( static_cast< std::size_t >( fw ) * fh * 4 );
			if ( FAILED( converter->CopyPixels( nullptr, fw * 4, static_cast< UINT >( rf.pixels.size( ) ), rf.pixels.data( ) ) ) )
			{
				return {};
			}

			result.frames[ i ].delay = rf.delay;
			result.frames[ i ].x = rf.x;
			result.frames[ i ].y = rf.y;
			result.frames[ i ].w = rf.w;
			result.frames[ i ].h = rf.h;
			result.frames[ i ].disposal = rf.disposal;
		}

		const auto canvas_bytes = static_cast< std::size_t >( result.canvas_w ) * result.canvas_h * 4;
		std::vector<std::uint8_t> canvas( canvas_bytes, 0 );
		std::vector<std::uint8_t> prev_canvas;

		result.composited.resize( frame_count );

		for ( UINT i = 0; i < frame_count; ++i )
		{
			auto& rf = raw[ i ];

			if ( rf.disposal == 3 )
			{
				prev_canvas = canvas;
			}

			for ( int row = 0; row < rf.h; ++row )
			{
				const auto dy = rf.y + row;
				if ( dy < 0 || dy >= result.canvas_h )
				{
					continue;
				}

				for ( int col = 0; col < rf.w; ++col )
				{
					const auto dx = rf.x + col;
					if ( dx < 0 || dx >= result.canvas_w )
					{
						continue;
					}

					const auto src = ( static_cast< std::size_t >( row ) * rf.w + col ) * 4;
					const auto dst = ( static_cast< std::size_t >( dy ) * result.canvas_w + dx ) * 4;
					const auto sa = rf.pixels[ src + 3 ];

					if ( sa == 255 )
					{
						canvas[ dst + 0 ] = rf.pixels[ src + 0 ];
						canvas[ dst + 1 ] = rf.pixels[ src + 1 ];
						canvas[ dst + 2 ] = rf.pixels[ src + 2 ];
						canvas[ dst + 3 ] = 255;
					}
					else if ( sa > 0 )
					{
						const auto da = canvas[ dst + 3 ];
						const auto inv = 255 - sa;
						const auto out_a = sa + ( ( da * inv ) / 255 );

						if ( out_a > 0 )
						{
							canvas[ dst + 0 ] = static_cast< std::uint8_t >( ( rf.pixels[ src + 0 ] * sa + canvas[ dst + 0 ] * da * inv / 255 ) / out_a );
							canvas[ dst + 1 ] = static_cast< std::uint8_t >( ( rf.pixels[ src + 1 ] * sa + canvas[ dst + 1 ] * da * inv / 255 ) / out_a );
							canvas[ dst + 2 ] = static_cast< std::uint8_t >( ( rf.pixels[ src + 2 ] * sa + canvas[ dst + 2 ] * da * inv / 255 ) / out_a );
							canvas[ dst + 3 ] = static_cast< std::uint8_t >( out_a );
						}
					}
				}
			}

			result.composited[ i ] = create_srv_from_rgba( canvas.data( ), result.canvas_w, result.canvas_h );
			if ( !result.composited[ i ] )
			{
				result.frames.clear( );
				result.composited.clear( );
				return result;
			}

			switch ( rf.disposal )
			{
			case 2:
			{
				for ( int row = 0; row < rf.h; ++row )
				{
					const auto dy = rf.y + row;
					if ( dy < 0 || dy >= result.canvas_h )
					{
						continue;
					}

					const auto x0 = std::max( 0, rf.x );
					const auto x1 = std::min( result.canvas_w, rf.x + rf.w );

					if ( x0 < x1 )
					{
						const auto off = ( static_cast< std::size_t >( dy ) * result.canvas_w + x0 ) * 4;
						std::memset( &canvas[ off ], 0, static_cast< std::size_t >( x1 - x0 ) * 4 );
					}
				}
				break;
			}

			case 3:
				if ( !prev_canvas.empty( ) )
				{
					canvas = prev_canvas;
				}

				break;

			default:
				break;
			}
		}

		return result;
	}

	font* load_font( std::span<const std::byte> data, float size_px, int atlas_w, int atlas_h )
	{
		FT_Library ft{};
		if ( FT_Init_FreeType( &ft ) != 0 )
		{
			return nullptr;
		}

		FT_Face face{};
		if ( FT_New_Memory_Face( ft, reinterpret_cast< const FT_Byte* >( data.data( ) ), static_cast< FT_Long >( data.size( ) ), 0, &face ) != 0 )
		{
			FT_Done_FreeType( ft );
			return nullptr;
		}

		FT_Size_RequestRec req{};
		req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
		req.height = static_cast< FT_Long >( size_px * 64.0f );
		FT_Request_Size( face, &req );

		auto f = std::make_unique<font>( );
		f->size = size_px;
		f->atlas_w = atlas_w;
		f->atlas_h = atlas_h;
		f->ascent = static_cast< float >( FT_MulFix( face->ascender, face->size->metrics.y_scale ) ) / 64.0f;
		f->descent = static_cast< float >( FT_MulFix( face->descender, face->size->metrics.y_scale ) ) / 64.0f;
		f->line_height = static_cast< float >( face->size->metrics.height ) / 64.0f;
		f->ft_library = ft;
		f->ft_face = face;
		f->atlas_bitmap.resize( static_cast< std::size_t >( atlas_w * atlas_h ) * 4, 0 );

		for ( auto i = 0; i < 96; ++i )
		{
			f->rasterize( static_cast< char32_t >( 32 + i ) );
		}

		D3D11_TEXTURE2D_DESC td{};
		td.Width = static_cast< UINT >( atlas_w );
		td.Height = static_cast< UINT >( atlas_h );
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = f->atlas_bitmap.data( );
		init.SysMemPitch = static_cast< UINT >( atlas_w * 4 );

		if ( FAILED( detail::g.device->CreateTexture2D( &td, &init, &f->atlas_tex ) ) )
		{
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = td.Format;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;

		if ( FAILED( detail::g.device->CreateShaderResourceView( f->atlas_tex.Get( ), &sv, &f->atlas_srv ) ) )
		{
			return nullptr;
		}

		f->atlas_dirty = false;

		detail::g.fonts.push_back( std::move( f ) );
		auto result = detail::g.fonts.back( ).get( );

		if ( !detail::g.primary_font )
		{
			detail::g.primary_font = result;
			detail::g.font_stack.push_back( result );
		}

		return result;
	}

	void push_font( font* f )
	{
		detail::g.font_stack.push_back( f ? f : detail::g.primary_font );
	}

	void pop_font( )
	{
		if ( detail::g.font_stack.size( ) > 1 )
		{
			detail::g.font_stack.pop_back( );
		}
	}

	font* current_font( )
	{
		return detail::g.font_stack.empty( ) ? detail::g.primary_font : detail::g.font_stack.back( );
	}

	font* primary_font( )
	{
		return detail::g.primary_font;
	}

	std::pair<int, int> viewport_size( )
	{
		D3D11_VIEWPORT vp{};
		UINT count{ 1 };

		if ( detail::g.context )
		{
			detail::g.context->RSGetViewports( &count, &vp );
		}

		if ( count > 0 && vp.Width > 0 )
		{
			return { static_cast< int >( vp.Width ), static_cast< int >( vp.Height ) };
		}

		return { 0, 0 };
	}

	std::pair<float, float> measure_text( std::string_view str, font* f )
	{
		auto ff = f ? f : current_font( );
		if ( !ff )
		{
			return { 0.0f, 0.0f };
		}

		return ff->measure( str );
	}

	float delta_time( )
	{
		return detail::g.dt;
	}

	float framerate( )
	{
		return detail::g.fps;
	}

	ID3D11Device* device( )
	{
		return detail::g.device.Get( );
	}

} // namespace xdraw