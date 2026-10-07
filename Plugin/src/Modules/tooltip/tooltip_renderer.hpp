/**
 * @file tooltip_renderer.hpp
 * @brief GDI+ renderer that draws a tooltip into a Surface.
 * @license GPL v2.0 License
 *
 * @details
 * Layout (top to bottom): title row (icon + title), image, text.
 * Blocks are separated by a fixed gap and aligned according to Options::Alignment.
 * The surface is sized to fit the content, margins and border included.
 */

#pragma once

#include <memory>
#include <string>

#include "tooltip_options.hpp"
#include "tooltip_surface.hpp"

namespace Gdiplus {
	class Bitmap;
}

namespace tooltip {

	/**
	 * @struct RenderRequest
	 * @brief Everything the renderer needs to draw one frame.
	 */
	struct RenderRequest {

		/// @brief Merged options.
		const Options &Opts;

		/// @brief Body text (UTF-8). Empty means no text block.
		const std::string &Text;

		/// @brief Optional title icon (only drawn when a title exists).
		Gdiplus::Bitmap *Icon;

		/// @brief Optional image block.
		Gdiplus::Bitmap *Image;

		/// @brief DPI scale factor (dpi / 96.0).
		double Scale;
	};



	/**
	 * @struct Renderer
	 * @brief Stateless drawing helpers.
	 */
	struct Renderer {

		/**
		 * @brief Draws the tooltip into @p surface, resizing it as needed.
		 *
		 * @param surface Target surface.
		 * @param request Content and options.
		 * @return true on success.
		 *
		 * @pre GDI+ must be running (see Runtime::acquire).
		 */
		static bool render( Surface &surface, const RenderRequest &request ) noexcept;

		/**
		 * @brief Loads an image file into a detached 32-bit premultiplied bitmap.
		 *
		 * The file is copied and released immediately, so it is never kept locked.
		 *
		 * @param path File path (UTF-8).
		 * @return The bitmap, or nullptr on failure.
		 */
		static std::unique_ptr<Gdiplus::Bitmap> load( const std::string &path ) noexcept;
	};

} // namespace tooltip
