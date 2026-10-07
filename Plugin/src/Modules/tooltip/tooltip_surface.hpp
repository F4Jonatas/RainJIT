/**
 * @file tooltip_surface.hpp
 * @brief Off-screen 32-bit premultiplied-alpha DIB surface.
 * @license GPL v2.0 License
 *
 * @details
 * The surface owns a top-down DIB section selected into a memory DC.
 * GDI+ draws directly into its pixel memory (PixelFormat32bppPARGB) and the
 * layered window blits it with UpdateLayeredWindow, so no extra copy occurs.
 *
 * The surface is sized to the tooltip content, not to the virtual desktop.
 */

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstring>

namespace tooltip {

	/**
	 * @struct Surface
	 * @brief RAII wrapper over a memory DC + DIB section.
	 */
	struct Surface {

		HDC Dc = nullptr;
		HBITMAP Bitmap = nullptr;
		HGDIOBJ Previous = nullptr;
		void *Bits = nullptr;
		int Width = 0;
		int Height = 0;



		Surface() = default;

		Surface( const Surface & ) = delete;
		Surface &operator=( const Surface & ) = delete;

		~Surface() {
			release();
		}



		/**
		 * @brief Ensures the surface has the requested size.
		 *
		 * Reuses the current DIB when the size is unchanged.
		 *
		 * @param width  Width in pixels (> 0).
		 * @param height Height in pixels (> 0).
		 * @return true on success.
		 */
		bool resize( int width, int height ) noexcept {
			if ( width <= 0 || height <= 0 )
				return false;

			if ( Bits && width == Width && height == Height )
				return true;

			release();

			Dc = CreateCompatibleDC( nullptr );
			if ( !Dc )
				return false;

			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
			info.bmiHeader.biWidth = width;
			info.bmiHeader.biHeight = -height; // top-down
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;

			Bitmap = CreateDIBSection( Dc, &info, DIB_RGB_COLORS, &Bits, nullptr, 0 );
			if ( !Bitmap || !Bits ) {
				release();
				return false;
			}

			Previous = SelectObject( Dc, Bitmap );
			Width = width;
			Height = height;

			return true;
		}



		/// @brief Fills the surface with fully transparent pixels.
		void clear() noexcept {
			if ( Bits )
				std::memset( Bits, 0, static_cast<std::size_t>( Width ) * static_cast<std::size_t>( Height ) * 4u );
		}



		/// @brief Releases the DIB and the memory DC.
		void release() noexcept {
			if ( Dc && Previous )
				SelectObject( Dc, Previous );

			if ( Bitmap )
				DeleteObject( Bitmap );

			if ( Dc )
				DeleteDC( Dc );

			Dc = nullptr;
			Bitmap = nullptr;
			Previous = nullptr;
			Bits = nullptr;
			Width = 0;
			Height = 0;
		}
	};

} // namespace tooltip
