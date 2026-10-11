/**
 * @file tooltip_text.hpp
 * @brief Shared DirectWrite / Direct2D objects used to measure and draw tooltips.
 * @license GPL v2.0 License
 *
 * @details
 * - DirectWrite performs the layout, so missing glyphs go through the system
 *   font fallback (symbols, CJK, emoji) instead of showing an empty box
 * - Direct2D draws everything into the tooltip DIB through a DC render target,
 *   with color glyphs enabled (Segoe UI Emoji and other color fonts)
 * - The factories and the render target are created once and shared by every
 *   tooltip. Their lifetime is driven by Runtime::acquire() / Runtime::release()
 *
 * @note Single-threaded by design (Rainmeter main thread).
 */

#pragma once

#include <Windows.h>

#include <d2d1.h>
#include <dwrite.h>

namespace tooltip {

	/**
	 * @struct TextEngine
	 * @brief Process-wide DirectWrite factory and Direct2D DC render target.
	 */
	struct TextEngine {

		/**
		 * @brief Creates the Direct2D and DirectWrite factories.
		 * @return true on success. Safe to call more than once.
		 */
		static bool start() noexcept;

		/// @brief Releases the render target and both factories (idempotent).
		static void stop() noexcept;

		/// @brief DirectWrite factory, or nullptr before start().
		static IDWriteFactory *dwrite() noexcept;

		/**
		 * @brief Premultiplied B8G8R8A8 DC render target (created on first use).
		 *
		 * The caller binds it to a device context with BindDC() before every frame.
		 *
		 * @return The render target, or nullptr on failure.
		 */
		static ID2D1DCRenderTarget *target() noexcept;

		/// @brief Drops the render target (after D2DERR_RECREATE_TARGET).
		static void resetTarget() noexcept;

		/// @brief User locale name used for text formats (e.g. L"pt-BR").
		static const wchar_t *locale() noexcept;
	};

} // namespace tooltip
