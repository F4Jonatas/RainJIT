/**
 * @file tooltip_runtime.cpp
 * @brief Reference-counted process resources shared by all tooltips.
 * @license GPL v2.0 License
 */

#include "tooltip_runtime.hpp"

#include "tooltip_gdiplus.hpp"
#include "tooltip_text.hpp"
#include "tooltip_window.hpp"

#include <cstddef>

namespace tooltip {

	namespace {

		std::size_t g_references = 0;
		ULONG_PTR g_token = 0;

	} // namespace



	bool Runtime::acquire() noexcept {
		if ( g_references == 0 ) {
			Gdiplus::GdiplusStartupInput input;

			if ( Gdiplus::GdiplusStartup( &g_token, &input, nullptr ) != Gdiplus::Ok )
				return false;

			if ( !TextEngine::start() ) {
				Gdiplus::GdiplusShutdown( g_token );
				g_token = 0;
				return false;
			}

			if ( !Window::registerClass() ) {
				TextEngine::stop();
				Gdiplus::GdiplusShutdown( g_token );
				g_token = 0;
				return false;
			}
		}

		++g_references;
		return true;
	}



	void Runtime::release() noexcept {
		if ( g_references == 0 )
			return;

		if ( --g_references != 0 )
			return;

		Window::unregisterClass();
		TextEngine::stop();
		Gdiplus::GdiplusShutdown( g_token );
		g_token = 0;
	}

} // namespace tooltip
