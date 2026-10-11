/**
 * @file tooltip_text.cpp
 * @brief Shared DirectWrite / Direct2D objects used to measure and draw tooltips.
 * @license GPL v2.0 License
 */

#include "tooltip_text.hpp"

#include <dxgiformat.h>
#include <wrl/client.h>

#include <cwchar>

#pragma comment( lib, "d2d1.lib" )
#pragma comment( lib, "dwrite.lib" )

namespace tooltip {

	namespace {

		using Microsoft::WRL::ComPtr;

		ComPtr<ID2D1Factory> g_d2d;
		ComPtr<IDWriteFactory> g_dwrite;
		ComPtr<ID2D1DCRenderTarget> g_target;
		wchar_t g_locale[LOCALE_NAME_MAX_LENGTH] = L"en-us";

	} // namespace



	bool TextEngine::start() noexcept {
		if ( g_d2d && g_dwrite )
			return true;

		stop();

		// clang-format off
	HRESULT hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		__uuidof( ID2D1Factory ),
		nullptr,
		reinterpret_cast<void **>( g_d2d.GetAddressOf() )
	);
		// clang-format on

		if ( FAILED( hr ) ) {
			stop();
			return false;
		}

		// clang-format off
	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof( IDWriteFactory ),
		reinterpret_cast<IUnknown **>( g_dwrite.GetAddressOf() )
	);
		// clang-format on

		if ( FAILED( hr ) ) {
			stop();
			return false;
		}

		if ( GetUserDefaultLocaleName( g_locale, LOCALE_NAME_MAX_LENGTH ) == 0 )
			wcscpy_s( g_locale, L"en-us" );

		return true;
	}



	void TextEngine::stop() noexcept {
		g_target.Reset();
		g_dwrite.Reset();
		g_d2d.Reset();
	}



	IDWriteFactory *TextEngine::dwrite() noexcept {
		return g_dwrite.Get();
	}



	ID2D1DCRenderTarget *TextEngine::target() noexcept {
		if ( g_target )
			return g_target.Get();

		if ( !g_d2d )
			return nullptr;

		// clang-format off
	const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED ),
		96.0f,
		96.0f,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	);
		// clang-format on

		if ( FAILED( g_d2d->CreateDCRenderTarget( &properties, g_target.GetAddressOf() ) ) ) {
			g_target.Reset();
			return nullptr;
		}

		return g_target.Get();
	}



	void TextEngine::resetTarget() noexcept {
		g_target.Reset();
	}



	const wchar_t *TextEngine::locale() noexcept {
		return g_locale;
	}

} // namespace tooltip
