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
 * @file trident_options.cpp
 * @brief Parsing of the trident.create() options table and of the initial URL.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	// -------------------------------------------------------------------------
	// l_create
	// -------------------------------------------------------------------------

	/**
	 * @brief Reads the options table at stack index 1 into `ctrl` (defaults included).
	 *
	 * Every option defaults to its safe value (see trident.hpp). The Lua callback, when given, is stored in
	 * the Lua registry under GetCallbackKey(). `ctrl.configId` and `ctrl.rain` must already be set.
	 *
	 * @param L     Lua state; the options table is at index 1.
	 * @param rain  Owning Rain instance.
	 * @param ctrl  Control to fill.
	 */
	void ParseCreateOptions( lua_State *L, Rain *rain, Control &ctrl ) {
		ctrl.width = 800;
		ctrl.height = 600;
		ctrl.left = 0;
		ctrl.top = 0;
		ctrl.transparent = false;
		ctrl.colorKey = RGB( 255, 0, 255 );
		ctrl.enabled = true;
		ctrl.silent = true;
		ctrl.hwndParent = rain->hwnd;
		ctrl.insideSkin = true;
		ctrl.padLeft = ctrl.padTop = ctrl.padWidth = ctrl.padHeight = 0;
		ctrl.cornerRadius = 0;
		ctrl.sanitizeFlags = SANITIZE_ALL;
		ctrl.hidden            = false;
		ctrl.contextMenu       = true;
		ctrl.hwndIEServer         = nullptr;
		ctrl.ieServerSubclassData = nullptr;
		ctrl.controlSubclassData  = nullptr;

		auto getNum = [&]( const char *field, long &out ) {
			lua_getfield( L, 1, field );
			if ( lua_isnumber( L, -1 ) )
				out = (long)lua_tonumber( L, -1 );
			lua_pop( L, 1 );
		};
		getNum( "width", ctrl.width );
		getNum( "height", ctrl.height );
		getNum( "left", ctrl.left );
		getNum( "top", ctrl.top );

		lua_getfield( L, 1, "transparent" );
		ctrl.transparent = lua_toboolean( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 1, "colorKey" );
		if ( lua_isnumber( L, -1 ) )
			ctrl.colorKey = RgbToColorRef( lua_tointeger( L, -1 ) ); // item 15: 0xRRGGBB, same as setTransparent()
		lua_pop( L, 1 );

		// Item 13: default is true; only an explicit value overrides it.
		lua_getfield( L, 1, "silent" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.silent = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// Item 32: cache = true uses the shared WinINet cache like a normal browser (default false).
		lua_getfield( L, 1, "cache" );
		ctrl.useCache = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// Item 4/7 options (all default to the safe value):
		//   navigation     = "free" (default) | "locked": "locked" cancels every top-level navigation
		//                    that was not requested by navigate()/back()/forward()/refresh()/create{url}.
		//   allowDownloads = true: do not cancel file downloads (default false).
		//   allowActiveX   = true: do not deny ActiveX / shell / Java actions in the security manager
		//                    (default false: CreateObject("WScript.Shell") and friends are refused).
		ctrl.navigationMode = NAV_FREE;
		lua_getfield( L, 1, "navigation" );
		if ( lua_type( L, -1 ) == LUA_TSTRING ) {
			std::string mode = lua_tostring( L, -1 );
			if ( mode == "locked" )
				ctrl.navigationMode = NAV_LOCKED;
			else if ( mode != "free" )
				RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] navigation: unknown mode ignored (use \"free\" or \"locked\"): " + utf8_to_wstring( mode ) ).c_str() );
		}
		lua_pop( L, 1 );

		lua_getfield( L, 1, "allowDownloads" );
		ctrl.allowDownloads = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, 1, "allowActiveX" );
		ctrl.allowActiveX = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// Item 8: WinINet is shared with Internet Explorer. By default pages get no silent NTLM/Kerberos
		// logon (anonymous only) and no cookies; third-party cookies are never allowed.
		lua_getfield( L, 1, "allowCredentials" );
		ctrl.allowCredentials = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, 1, "allowCookies" );
		ctrl.allowCookies = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// Fix #8: window.external exposure. `external = false` disables it;
		// `externalOrigins = { "https://host[:port]" }` adds trusted remote origins.
		ctrl.externalEnabled = true;
		lua_getfield( L, 1, "external" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.externalEnabled = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, 1, "externalOrigins" );
		if ( lua_istable( L, -1 ) ) {
			lua_pushnil( L );
			while ( lua_next( L, -2 ) != 0 ) {
				if ( lua_type( L, -1 ) == LUA_TSTRING ) {
					// Only "http(s)://host[:port]" (optional trailing "/") is accepted: no userinfo,
					// no path, no javascript:/file:. Stored canonical (lowercase, default port omitted)
					// so it can be compared with the parsed origin of the document.
					std::string canonical = ParseOrigin( std::string( lua_tostring( L, -1 ), lua_objlen( L, -1 ) ), true ).canonical();
					if ( canonical.empty() )
						RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] externalOrigins: invalid origin ignored: " + utf8_to_wstring( luaVariant::ToDisplayString( L, -1 ) ) ).c_str() );
					else
						ctrl.externalOrigins.push_back( canonical );
				}
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		// allowedHosts = { "cdn.example.com", "*.example.com" }: when present (even empty), remote
		// sub-resources (img/src/CSS url()) of sanitized content may only load from these hosts.
		// reservedNames = { "myGlobal" }: extra id/name values dropped to prevent DOM clobbering.
		auto readStringList = [&]( const char *field, std::vector<std::string> &out, bool &present ) {
			lua_getfield( L, 1, field );
			if ( lua_istable( L, -1 ) ) {
				present = true;
				lua_pushnil( L );
				while ( lua_next( L, -2 ) != 0 ) {
					if ( lua_type( L, -1 ) == LUA_TSTRING && lua_objlen( L, -1 ) > 0 && lua_objlen( L, -1 ) <= 255 ) {
						std::string v( lua_tostring( L, -1 ), lua_objlen( L, -1 ) );
						std::transform( v.begin(), v.end(), v.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
						out.push_back( v );
					}
					lua_pop( L, 1 );
				}
			}
			lua_pop( L, 1 );
		};
		bool hostsPresent = false, namesPresent = false;
		readStringList( "allowedHosts", ctrl.allowedHosts, hostsPresent );
		ctrl.restrictHosts = hostsPresent;
		readStringList( "reservedNames", ctrl.reservedNames, namesPresent );

		lua_getfield( L, 1, "contextMenu" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.contextMenu = lua_toboolean( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 1, "insideSkin" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.insideSkin = lua_toboolean( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 1, "padding" );
		if ( lua_istable( L, -1 ) ) {
			lua_rawgeti( L, -1, 1 );
			if ( lua_isnumber( L, -1 ) )
				ctrl.padLeft = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			lua_rawgeti( L, -1, 2 );
			if ( lua_isnumber( L, -1 ) )
				ctrl.padTop = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			lua_rawgeti( L, -1, 3 );
			if ( lua_isnumber( L, -1 ) )
				ctrl.padWidth = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			lua_rawgeti( L, -1, 4 );
			if ( lua_isnumber( L, -1 ) )
				ctrl.padHeight = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );

		lua_getfield( L, 1, "cornerRadius" );
		if ( lua_isnumber( L, -1 ) )
			ctrl.cornerRadius = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 1, "hide" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.hidden = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, 1, "sanitize" );
		if ( lua_isboolean( L, -1 ) ) {
			ctrl.sanitizeFlags = lua_toboolean( L, -1 ) ? SANITIZE_ALL : SANITIZE_NONE;
		} else if ( lua_istable( L, -1 ) ) {
			ctrl.sanitizeFlags = SANITIZE_ALL;
			lua_pushnil( L );
			while ( lua_next( L, -2 ) != 0 ) {
				if ( lua_isstring( L, -1 ) ) {
					std::string tok = lua_tostring( L, -1 );
					if ( tok == "allow_scripts" )
						ctrl.sanitizeFlags &= ~BLOCK_SCRIPTS;
					else if ( tok == "allow_events" )
						ctrl.sanitizeFlags &= ~BLOCK_EVENTS;
					else if ( tok == "allow_style" )
						ctrl.sanitizeFlags &= ~( FILTER_CSS | BLOCK_STYLE );
					else if ( tok == "allow_css" )
						ctrl.sanitizeFlags &= ~FILTER_CSS;
					else if ( tok == "allow_urls" )
						ctrl.sanitizeFlags &= ~VALIDATE_URLS;
					else if ( tok == "allow_local" )
						ctrl.sanitizeFlags |= ALLOW_LOCAL;
					else if ( tok == "allow_clobbering" )
						ctrl.sanitizeFlags &= ~BLOCK_CLOBBERING;
					else if ( tok == "allow_overlay" )
						ctrl.sanitizeFlags &= ~BLOCK_OVERLAY;
				}
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		{
			const wchar_t *lvl = ctrl.sanitizeFlags == SANITIZE_ALL ? L"ALL" : ctrl.sanitizeFlags == SANITIZE_NONE ? L"NONE" : L"CUSTOM";
			wchar_t buf[128];
			swprintf_s( buf, L"[RainJIT:Trident] sanitize=%s (flags=0x%X)", lvl, ctrl.sanitizeFlags );
			RN_LOG( rain, LOG_DEBUG, buf );
		}

		// theme = "light" | "dark": written as data-theme on <html>. Without it the Windows app theme is
		// used (and re-read by redraw()).
		lua_getfield( L, 1, "theme" );
		if ( lua_type( L, -1 ) == LUA_TSTRING ) {
			bool ok = true;
			ctrl.themeOption = NormalizeThemeName( lua_tostring( L, -1 ), ok );
			if ( !ok )
				RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] theme: unknown value ignored (use \"light\" or \"dark\"): " + utf8_to_wstring( lua_tostring( L, -1 ) ) ).c_str() );
		}
		lua_pop( L, 1 );

		// data = { name = value, ... }: written as data-<name>="<value>" on <html>, next to data-theme.
		lua_getfield( L, 1, "data" );
		if ( lua_istable( L, -1 ) ) {
			std::vector<std::string> problems;
			ReadDataTable( L, lua_gettop( L ), ctrl.data, problems );
			for ( const std::string &p : problems )
				RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] data: " + utf8_to_wstring( p ) ).c_str() );
		} else if ( !lua_isnil( L, -1 ) ) {
			RN_LOG( rain, LOG_WARNING, L"[RainJIT:Trident] data: a table is expected; ignored." );
		}
		lua_pop( L, 1 );

		lua_getfield( L, 1, "callback" );
		if ( lua_isfunction( L, -1 ) ) {
			ctrl.callbackKey = GetCallbackKey( rain, ctrl.configId );
			lua_pushvalue( L, -1 );
			lua_setfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
		} else {
			ctrl.callbackKey.clear();
		}
		lua_pop( L, 1 );

	}

	/**
	 * @brief Resolves and validates the `url` option for the initial navigation.
	 *
	 * @return The URL to navigate to, or "about:blank" when it is missing, unresolvable or refused by the
	 *         sanitizer policy or by IsNavigationAllowed().
	 */
	std::wstring ResolveInitialUrl( lua_State *L, Rain *rain, const Control &ctrl ) {
		std::wstring wurl = L"about:blank";
		lua_getfield( L, 1, "url" );
		if ( lua_isstring( L, -1 ) && lua_objlen( L, -1 ) > 0 ) {
			std::string candidate = lua_tostring( L, -1 );
			bool isLocal = false;
			std::wstring resolved = ResolveUrl( rain, candidate, isLocal );

			if ( !resolved.empty() ) {
				if ( ctrl.sanitizeFlags & VALIDATE_URLS ) {
					bool allowLocal = ( ctrl.sanitizeFlags & ALLOW_LOCAL ) != 0;
					if ( sanitize::IsUrlSafe( wstring_to_utf8( resolved ), allowLocal ) )
						wurl = resolved;
					else
						RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] create() URL blocked - about:blank. Blocked: " + resolved ).c_str() );
				} else {
					wurl = resolved;
				}
			}
		}
		lua_pop( L, 1 );

		// Unconditional VBScript / local-file guard for the initial URL.
		if ( wurl != L"about:blank" && !IsNavigationAllowed( ctrl, wurl ) ) {
			RN_LOG( rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] create() URL blocked (VBScript or disallowed) - about:blank. Blocked: " + wurl ).c_str() );
			wurl = L"about:blank";
		}

		return wurl;
	}


} // namespace trident
