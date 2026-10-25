/**
 * @file tooltip_runtime.hpp
 * @brief Reference-counted process resources shared by all tooltips.
 * @license GPL v2.0 License
 *
 * @details
 * GDI+ (image decoding), the text engine (Direct2D / DirectWrite) and the
 * window class are started when the first tooltip is created and released
 * when the last one is destroyed. Because the Rainmeter plugin
 * has no Finalize hook, every Tooltip pairs acquire() with release() inside
 * its own destroy(), which is also reached from the Lua `__gc`.
 *
 * @note Single-threaded by design (Rainmeter main thread).
 */

#pragma once

namespace tooltip {

	/**
	 * @struct Runtime
	 * @brief Shared GDI+ / text engine / window-class lifetime manager.
	 */
	struct Runtime {

		/**
		 * @brief Adds a reference, starting GDI+, the text engine and the window class on first use.
		 * @return true on success. On failure no reference is held.
		 */
		static bool acquire() noexcept;

		/**
		 * @brief Drops a reference, shutting everything down when it reaches zero.
		 *
		 * @pre Every GDI+ object created by the caller must already be destroyed.
		 */
		static void release() noexcept;
	};

} // namespace tooltip
