/**
 * @file tooltip_window.hpp
 * @brief Layered tooltip window and monitor helpers.
 * @license GPL v2.0 License
 *
 * @details
 * - The window is a per-pixel-alpha layered popup (WS_EX_LAYERED)
 * - It never takes focus (WS_EX_NOACTIVATE) and hides from the taskbar
 * - It is redrawn only through Window::present(); moving uses SetWindowPos
 * - Auto-hide uses a native window timer, so it needs no Finalize hook and
 *   dies together with the window
 *
 * @note All calls must happen on the thread that created the window
 *       (the Rainmeter main thread).
 */

#pragma once

#include <Windows.h>

#include <cstdint>
#include <optional>

#include "tooltip_options.hpp"
#include "tooltip_surface.hpp"

namespace tooltip {

	/// @brief Result of Window::setBackdrop().
	enum class BackdropResult : std::uint8_t {
		Ok,
		Unsupported,
		Failed
	};


	/**
	 * @struct MonitorInfo
	 * @brief Work area and effective DPI of the monitor nearest to a point.
	 */
	struct MonitorInfo {

		/// @brief Work area (excludes the taskbar).
		RECT Work{};

		/// @brief Effective DPI (96 = 100%).
		UINT Dpi = 96;

		/**
		 * @brief Queries the monitor nearest to @p point.
		 * @param point Screen coordinates.
		 * @return Work area and DPI.
		 */
		static MonitorInfo at( POINT point ) noexcept;
	};



	/**
	 * @struct Window
	 * @brief RAII wrapper over the layered tooltip window.
	 */
	struct Window {

		/// @brief Native handle (nullptr when destroyed).
		HWND Handle = nullptr;

		/// @brief Cached topmost state used when showing/raising.
		bool Topmost = true;



		Window() = default;
		~Window();

		Window( const Window & ) = delete;
		Window &operator=( const Window & ) = delete;



		/// @brief Registers the window class. Safe to call more than once.
		static bool registerClass() noexcept;

		/// @brief Unregisters the window class.
		static void unregisterClass() noexcept;

		/// @brief Module handle that owns the window procedure.
		static HINSTANCE module() noexcept;



		/**
		 * @brief Creates the (hidden) window.
		 * @param topmost      Keep above other windows.
		 * @param clickThrough Let mouse input pass through the tooltip.
		 * @return true on success.
		 */
		bool create( bool topmost, bool clickThrough ) noexcept;

		/// @brief Destroys the window (idempotent).
		void destroy() noexcept;

		/**
		 * @brief Pushes the surface content to the window.
		 *
		 * Also resizes and moves the window to @p x, @p y.
		 *
		 * @param surface Source surface (32-bit premultiplied).
		 * @param x       Screen X.
		 * @param y       Screen Y.
		 * @param opacity Constant alpha 0..255.
		 */
		bool present( const Surface &surface, int x, int y, std::uint8_t opacity ) noexcept;

		/// @brief Moves the window without redrawing.
		void moveTo( int x, int y ) noexcept;

		/// @brief Shows the window without activating it.
		void show() noexcept;

		/// @brief Hides the window and stops the auto-hide timer.
		void hide() noexcept;

		/// @brief Re-applies the topmost z-order (no-op when not topmost).
		void raise() noexcept;

		/// @brief True when the window is currently visible.
		bool visible() const noexcept;

		/// @brief Switches the topmost style.
		void setTopmost( bool topmost ) noexcept;

		/// @brief Switches mouse pass-through.
		void setClickThrough( bool clickThrough ) noexcept;

		/**
		 * @brief Starts or restarts the auto-hide timer.
		 * @param milliseconds Delay; 0 stops the timer.
		 */
		void setTimeout( std::uint32_t milliseconds ) noexcept;

		/**
		 * @brief Applies (or removes) a system backdrop through DWM.
		 *
		 * @details
		 * - Backdrop material: Windows 11 22H2 (build 22621) or newer
		 * - Corner / border attributes: Windows 11 (build 22000) or newer
		 * - While a backdrop is active the DWM border is set to "no color", so only
		 *   the border drawn by the tooltip is visible and DWM just rounds the corners
		 * - With Backdrop::None every attribute is reset to its default
		 *
		 * @param effect Material to apply.
		 * @param corner Corner style used by the system.
		 * @param dark   true forces the dark variant, false the light one,
		 *               nullopt follows the system app theme.
		 * @return Ok, Unsupported (OS too old) or Failed (DWM rejected the call).
		 */
		BackdropResult setBackdrop( Backdrop effect, Corner corner, std::optional<bool> dark ) noexcept;
	};

} // namespace tooltip
