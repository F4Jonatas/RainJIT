/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


/**
 * @file trident_util.cpp
 * @brief Shared helpers: process-wide state, Lua registry keys, color and navigation-flag helpers.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Private window message posted to the skin window when the user clicks inside the browser (item 34).
	 *
	 * Registered with RegisterWindowMessage so it cannot collide with another plugin's WM_APP+n message
	 * on the shared Rainmeter window; ParentSubclassProc consumes it (it is never forwarded to Rainmeter).
	 */
	UINT FocusMessage() {
		static const UINT msg = RegisterWindowMessageW( L"RainJIT.Trident.Focus.v1" );
		return msg;
	}

	/**
	 * @brief Navigate2 flags for programmatic navigations (item 32).
	 *
	 * By default the control neither reads from nor writes to the WinINet cache it shares with Internet
	 * Explorer (item 8: no cross-contamination with the user's browser). `create{ cache = true }` switches
	 * to the normal cache behavior. (The old code OR-ed 0x100, which is navNewWindowsManaged, not a cache
	 * flag: navNoWriteToCache is 0x08.)
	 */
	long NavigateFlags( const Control &ctrl ) {
		constexpr long kNavNoReadFromCache = 0x04;
		constexpr long kNavNoWriteToCache = 0x08;
		return ctrl.useCache ? 0 : ( kNavNoReadFromCache | kNavNoWriteToCache );
	}

	/// Globals
	std::unordered_map<Rain *, std::shared_ptr<Context>> g_contexts;

	std::mutex g_contextsMutex;

	/**
	 * @brief Process-wide source of control ids (item 22).
	 *
	 * The id is used as the Win32 subclass id on the (shared) skin window, as the key of the event queue
	 * and inside Lua registry keys. A per-Rain counter restarted at 1 for every measure, so two measures
	 * in the same skin replaced each other's subclass (same window + same proc + same id) and leaked it.
	 */
	std::atomic<int> g_nextControlId{ 1 };

	bool g_comInitialized = false;

	bool g_comNeedsUninitialize = false;

	/**
	 * @brief Builds a Lua registry key that is unique per control.
	 *
	 * The key embeds the skin name (readability only) and the process-wide control id (uniqueness),
	 * so keys can never collide between controls, measures or skins (items 22/23).
	 *
	 * @param prefix    Key family, e.g. "trident_callback_".
	 * @param rain      Owning Rain instance.
	 * @param configId  Process-unique control id.
	 */
	static std::string MakeRegistryKey( const char *prefix, Rain *rain, int configId ) {
		std::string name;
		if ( rain ) {
			std::wstring skinName = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
			name = wstring_to_utf8( skinName );
			std::replace( name.begin(), name.end(), '\\', '_' );
			std::replace( name.begin(), name.end(), '/', '_' );
			std::replace( name.begin(), name.end(), '.', '_' );
		}
		return std::string( prefix ) + name + "_" + std::to_string( configId );
	}

	/// Registry key of a control's callback function.
	std::string GetCallbackKey( Rain *rain, int configId ) {
		return MakeRegistryKey( "trident_callback_", rain, configId );
	}

	/// Registry key of a control's Lua browser table. Independent of the callback (item 23).
	std::string GetBrowserKey( Rain *rain, int configId ) {
		return MakeRegistryKey( "trident_browser_", rain, configId );
	}

	/// Item 15: Lua colors are 0xRRGGBB; Win32 wants a COLORREF (0x00BBGGRR). One conversion for create() and setTransparent().
	COLORREF RgbToColorRef( lua_Integer rgb ) {
		return RGB( ( rgb >> 16 ) & 0xFF, ( rgb >> 8 ) & 0xFF, rgb & 0xFF );
	}

	/// COLORREF -> "#RRGGBB" (CSS color for the injected body background).
	std::wstring ColorRefToCss( COLORREF c ) {
		wchar_t buf[8];
		swprintf_s( buf, L"#%02X%02X%02X", GetRValue( c ), GetGValue( c ), GetBValue( c ) );
		return buf;
	}


} // namespace trident
