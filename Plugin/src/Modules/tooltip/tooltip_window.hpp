/**
 * @file tooltip_window.hpp
 * @brief Layered tooltip window, fade animation and monitor helpers.
 * @license GPL v2.0 License
 *
 * @details
 * - The window is a per-pixel-alpha layered popup (WS_EX_LAYERED)
 * - It never takes focus (WS_EX_NOACTIVATE) and hides from the taskbar
 * - It is redrawn only through Window::present(); moving uses SetWindowPos
 * - Auto-hide and fade animations use native window timers, so no Finalize
 *   hook is needed and the timers die together with the window
 * - Fading only changes the constant alpha of the layered window
 *   (UpdateLayeredWindow without a source DC), so the bitmap is never redrawn
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

		/// @brief Called after the window destroyed itself at the end of a fade-out (see close()).
		using GoneFn = void ( * )( void *context );

		/// @brief What happens when the current fade reaches its target.
		enum class After : std::uint8_t {
			None,
			Hide,
			Destroy
		};



		/// @brief Native handle (nullptr when destroyed).
		HWND Handle = nullptr;

		/// @brief Cached topmost state used when showing/raising.
		bool Topmost = true;

		/// @brief Alpha of the fully visible tooltip (Options::Opacity).
		std::uint8_t Level = 255;

		/// @brief Alpha currently applied to the window.
		std::uint8_t Current = 0;

		/// @brief Fade time used when the auto-hide timeout expires (Options::FadeDuration).
		std::uint32_t AutoFadeMs = 0;

		/// @brief Notified when the window destroyed itself after a fade-out.
		GoneFn OnGone = nullptr;

		/// @brief Argument passed to @ref OnGone.
		void *Context = nullptr;



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

		/// @brief Destroys the window immediately (idempotent). Does not call OnGone.
		void destroy() noexcept;

		/**
		 * @brief Pushes the surface content to the window.
		 *
		 * Also resizes and moves the window to @p x, @p y. The constant alpha is the
		 * current one (@ref Current), so a redraw in the middle of a fade keeps fading.
		 *
		 * @param surface Source surface (32-bit premultiplied).
		 * @param x       Screen X.
		 * @param y       Screen Y.
		 */
		bool present( const Surface &surface, int x, int y ) noexcept;

		/// @brief Moves the window without redrawing.
		void moveTo( int x, int y ) noexcept;

		/**
		 * @brief Shows the window without activating it.
		 *
		 * Cancels a pending hide / destroy. A hidden window fades in from 0 to
		 * @ref Level; a window that is fading out fades back in.
		 *
		 * @param fadeMs  Fade-in time. 0 shows it at once.
		 * @param raiseIt Re-apply the topmost z-order when it is already visible.
		 */
		void show( std::uint32_t fadeMs, bool raiseIt ) noexcept;

		/**
		 * @brief Hides the window and stops the auto-hide timer.
		 * @param fadeMs Fade-out time. 0 hides it at once.
		 */
		void hide( std::uint32_t fadeMs ) noexcept;

		/**
		 * @brief Destroys the window after an optional fade-out.
		 *
		 * With a fade the window is destroyed when it ends and @ref OnGone is called.
		 *
		 * @param fadeMs Fade-out time. 0 destroys it at once (OnGone is still called).
		 */
		void close( std::uint32_t fadeMs ) noexcept;

		/// @brief Re-applies the topmost z-order (no-op when not topmost).
		void raise() noexcept;

		/// @brief True when the window is currently visible.
		bool visible() const noexcept;

		/// @brief Switches the topmost style.
		void setTopmost( bool topmost ) noexcept;

		/// @brief Switches mouse pass-through.
		void setClickThrough( bool clickThrough ) noexcept;

		/**
		 * @brief Sets the alpha of the fully visible tooltip.
		 *
		 * Applied at once when the window is visible and not fading out.
		 *
		 * @param level Alpha 0..255.
		 */
		void setLevel( std::uint8_t level ) noexcept;

		/**
		 * @brief Starts or restarts the auto-hide timer.
		 * @param milliseconds Delay; 0 stops the timer.
		 */
		void setTimeout( std::uint32_t milliseconds ) noexcept;

		/**
		 * @brief Window timer entry point (auto-hide and fade steps).
		 * @param id Timer id.
		 */
		void onTimer( UINT_PTR id ) noexcept;

		/**
		 * @brief Applies (or removes) a system backdrop.
		 *
		 * @details
		 * - Acrylic: SetWindowCompositionAttribute accent policy (Windows 10 1803,
		 *   build 17134, or newer). The effect keeps working while the window is not active
		 * - Mica / MicaAlt: DWM system backdrop (Windows 11 22H2, build 22621, or newer),
		 *   with the "active window appearance" forced on
		 * - Corner / border / dark attributes: Windows 11 (build 22000) or newer
		 * - While a backdrop is active the DWM border is set to "no color", so only
		 *   the border drawn by the tooltip is visible and DWM just rounds the corners
		 * - With Backdrop::None every attribute is reset to its default
		 *
		 * @param effect Material to apply.
		 * @param corner Corner style used by the system.
		 * @param dark   true forces the dark variant, false the light one,
		 *               nullopt follows the system app theme.
		 * @param tint   Acrylic tint 0xRRGGBBAA. nullopt derives it from the theme.
		 * @return Ok, Unsupported (OS too old) or Failed (the system rejected the call).
		 */
		BackdropResult setBackdrop( Backdrop effect, Corner corner, std::optional<bool> dark, std::optional<Rgba> tint ) noexcept;



	private:
		/// @brief Fade bookkeeping.
		bool Fading = false;
		After Then = After::None;
		std::uint8_t FadeFrom = 0;
		std::uint8_t FadeTo = 0;
		std::uint32_t FadeMs = 0;
		ULONGLONG FadeStart = 0;

		/// @brief Sets the constant alpha without touching the bitmap.
		void applyAlpha( std::uint8_t alpha ) noexcept;

		/// @brief Starts a fade (or jumps to the target when @p ms is 0).
		void fadeTo( std::uint8_t target, std::uint32_t ms, After then ) noexcept;

		/// @brief Advances the fade by one timer step.
		void tick() noexcept;

		/// @brief Stops the fade timer.
		void stopFade() noexcept;

		/// @brief Runs the action scheduled for the end of a fade.
		void complete() noexcept;

		/// @brief Destroys the window and notifies @ref OnGone.
		void finish() noexcept;
	};

} // namespace tooltip
