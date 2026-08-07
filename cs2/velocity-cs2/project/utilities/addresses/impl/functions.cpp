#include <pch/pch.hpp>
#include <utilities/logging/logging.hpp>
#include "../addresses.hpp"

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )

namespace {

	constexpr std::size_t present_index{ 8 };
	constexpr std::size_t resize_buffers_index{ 13 };

	bool acquire_swap_chain_functions( std::uintptr_t& present, std::uintptr_t& resize_buffers )
	{
		WNDCLASSEXA window_class{};
		window_class.cbSize = sizeof( window_class );
		window_class.lpfnWndProc = DefWindowProcA;
		window_class.hInstance = GetModuleHandleA( nullptr );
		window_class.lpszClassName = "velocity_dummy_window";

		if ( !RegisterClassExA( &window_class ) )
		{
			logging::console::print( xs( "[error] failed to register dummy D3D11 window | {}" ), GetLastError( ) );
			return false;
		}

		const auto window = CreateWindowExA(
			0,
			window_class.lpszClassName,
			"",
			WS_OVERLAPPEDWINDOW,
			0,
			0,
			64,
			64,
			nullptr,
			nullptr,
			window_class.hInstance,
			nullptr
		);

		if ( !window )
		{
			logging::console::print( xs( "[error] failed to create dummy D3D11 window | {}" ), GetLastError( ) );
			UnregisterClassA( window_class.lpszClassName, window_class.hInstance );
			return false;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		desc.BufferCount = 1;
		desc.BufferDesc.Width = 64;
		desc.BufferDesc.Height = 64;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.OutputWindow = window;
		desc.SampleDesc.Count = 1;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		const D3D_FEATURE_LEVEL feature_levels[] {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_0
		};

		IDXGISwapChain* swap_chain{};
		ID3D11Device* device{};
		ID3D11DeviceContext* context{};

		const auto result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			0,
			feature_levels,
			static_cast< UINT >( std::size( feature_levels ) ),
			D3D11_SDK_VERSION,
			&desc,
			&swap_chain,
			&device,
			nullptr,
			&context
		);

		if ( SUCCEEDED( result ) && swap_chain )
		{
			const auto vtable = *reinterpret_cast< std::uintptr_t** >( swap_chain );
			present = vtable[ present_index ];
			resize_buffers = vtable[ resize_buffers_index ];
		}
		else
		{
			logging::console::print( xs( "[error] failed to create dummy D3D11 swap chain | {}" ), result );
		}

		if ( context )
			context->Release( );
		if ( device )
			device->Release( );
		if ( swap_chain )
			swap_chain->Release( );

		DestroyWindow( window );
		UnregisterClassA( window_class.lpszClassName, window_class.hInstance );
		return present && resize_buffers;
	}

} // namespace

namespace addresses::functions {

	bool initialize( )
	{
		return acquire_swap_chain_functions( present, resize_buffers );
	}

} // namespace addresses::functions
