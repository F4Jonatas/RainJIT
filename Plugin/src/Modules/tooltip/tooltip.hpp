/**
 * @file tooltip.hpp
 * @brief Custom layered tooltip bound to a Lua object.
 * @license GPL v2.0 License
 *
 * Provides a lightweight tooltip drawn with GDI+ into a per-pixel-alpha
 * layered window. The bitmap is rendered only when the content or style
 * changes; moving the tooltip never redraws it.
 *
 * @details
 * - All strings exposed to Lua are UTF-8
 * - Colors are 0xRRGGBBAA
 * - Every tooltip owns its own window and a content-sized bitmap
 * - Lifetime is tied to the Lua userdata: `destroy()` or `__gc` release
 *   the window, so no Finalize hook is required in the plugin
 *
 * @example
 * @code{.lua}
 * local tooltip = require("tooltip")
 * local tt = tooltip("Hello", { title = "Info", timeout = 3000 })
 * tt:update("World")
 * tt:destroy()
 * @endcode
 */

#pragma once

#include <Windows.h>

#include <memory>
#include <optional>
#include <string>

#include "tooltip_options.hpp"
#include "tooltip_surface.hpp"
#include "tooltip_window.hpp"

struct lua_State;
struct Rain;

namespace Gdiplus {
	class Bitmap;
}

namespace tooltip {

	/**
	 * @struct Tooltip
	 * @brief A single tooltip: options, content, window and cached bitmaps.
	 *
	 * This struct is not copyable. It is heap-allocated by the Lua constructor
	 * and owned by a userdata.
	 */
	struct Tooltip {

		/// @brief Current merged options.
		Options Opts;

		/// @brief Current body text (UTF-8).
		std::string Text;

		/// @brief Pending warning for the host to log (cleared by takeWarning()).
		std::string Warning;

		/// @brief Layered window.
		Window Win;

		/// @brief Content-sized pixel surface.
		Surface Canvas;

		/// @brief Cached title icon.
		std::unique_ptr<Gdiplus::Bitmap> Icon;

		/// @brief Cached image.
		std::unique_ptr<Gdiplus::Bitmap> Image;

		/// @brief Path that produced @ref Icon.
		std::string IconPath;

		/// @brief Path that produced @ref Image.
		std::string ImagePath;

		/// @brief DPI used by the last render.
		UINT LastDpi = 0;

		/// @brief True while this tooltip holds a Runtime reference.
		bool Acquired = false;

		/// @name Backdrop state last applied to the window
		/// @{
		Backdrop AppliedEffect = Backdrop::None;
		Corner AppliedCorner = Corner::Default;
		std::optional<bool> AppliedDark;
		/// @}



		Tooltip();
		~Tooltip();

		Tooltip( const Tooltip & ) = delete;
		Tooltip &operator=( const Tooltip & ) = delete;



		/**
		 * @brief Acquires shared resources, creates the window and shows the tooltip.
		 *
		 * @param text    Initial body text (UTF-8, may be empty).
		 * @param options Initial options.
		 * @return true on success. On failure the object is left destroyed.
		 */
		bool open( std::string text, Options options ) noexcept;

		/**
		 * @brief Replaces text/options and refreshes the tooltip.
		 *
		 * @param options Merged options.
		 * @param text    New body text.
		 * @param dirty   What changed (decides redraw vs. move).
		 * @return true on success.
		 */
		bool commit( Options options, std::string text, const Dirty &dirty ) noexcept;

		/**
		 * @brief Moves the tooltip to an absolute position without redrawing.
		 *
		 * @param x Screen X.
		 * @param y Screen Y.
		 */
		bool moveTo( int x, int y ) noexcept;

		/// @brief Shows the tooltip and restarts the auto-hide timer.
		void show() noexcept;

		/// @brief Hides the tooltip.
		void hide() noexcept;

		/// @brief True when the tooltip is currently visible.
		bool visible() const noexcept;

		/// @brief True until destroy() is called.
		bool alive() const noexcept;

		/// @brief Releases the window, bitmaps and the Runtime reference (idempotent).
		void destroy() noexcept;

		/// @brief Returns and clears the pending warning.
		std::string takeWarning() noexcept;

		/**
		 * @brief Re-renders (if needed), repositions and presents the window.
		 *
		 * @param dirty What changed since the last refresh.
		 * @return true on success.
		 */
		bool refresh( const Dirty &dirty ) noexcept;

		/// @brief Reloads icon/image bitmaps when their paths changed.
		void loadImages();

		/**
		 * @brief Applies the backdrop options to the window when they changed.
		 *
		 * Adds a warning when the operating system does not support the effect.
		 */
		void applyBackdrop();

		/// @brief Throwing implementation behind refresh().
		bool refreshImpl( const Dirty &dirty );
	};

} // namespace tooltip



/**
 * @brief Lua module entry point for Tooltip.
 *
 * Called when Lua executes `require("tooltip")`.
 *
 * @param L Lua state.
 * @return 1 (module table with __call metamethod).
 *
 * @note Requires a Rain* pointer as upvalue 1.
 */
extern "C" int luaopen_tooltip( lua_State *L );



namespace tooltip {

	/**
	 * @brief Register the tooltip Lua module into package.preload.
	 *
	 * Consistent with depot::RegisterModule, xml::RegisterModule and
	 * html::RegisterModule.
	 *
	 * @param L    Lua state.
	 * @param rain Rainmeter measure context (passed as upvalue to luaopen_tooltip).
	 *
	 * @example
	 * @code{.cpp}
	 * tooltip::RegisterModule( L, rain );
	 * @endcode
	 */
	void RegisterModule( lua_State *L, Rain *rain );

} // namespace tooltip
