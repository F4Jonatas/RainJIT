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
 * @file trident_methods.cpp
 * @brief Lua methods of the browser object (navigate, write, execScript, quit, ...).
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Resolves the ControlHandle of the running Lua closure (item 23).
	 *
	 * Every method closure holds the control's handle *box* - a full userdata whose `__gc` frees the
	 * ControlHandle - as upvalue 1. A closure therefore keeps the box (and the handle) alive for as long
	 * as it exists, so a method saved by the user and called after quit() or after the browser table was
	 * collected can no longer touch a freed handle.
	 *
	 * @return The handle, or nullptr if the box was already finalized.
	 */
	ControlHandle *HandleFromUpvalue( lua_State *L ) {
		ControlHandle **box = static_cast<ControlHandle **>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		return box ? *box : nullptr;
	}

	// -------------------------------------------------------------------------
	// Lua bindings
	// -------------------------------------------------------------------------

	/**
	 * @brief Converts a control's SanitizeFlags bitmask into sanitize::Options.
	 */
	static sanitize::Options FlagsToOptions( const Control &ctrl ) {
		const uint32_t flags = ctrl.sanitizeFlags;
		sanitize::Options opts;
		opts.blockScripts = ( flags & BLOCK_SCRIPTS ) != 0;
		opts.blockEvents = ( flags & BLOCK_EVENTS ) != 0;
		opts.blockStyle = ( flags & BLOCK_STYLE ) != 0;
		opts.filterCss = ( flags & FILTER_CSS ) != 0;
		opts.validateUrls = ( flags & VALIDATE_URLS ) != 0;
		opts.allowLocal = ( flags & ALLOW_LOCAL ) != 0;
		opts.blockClobbering = ( flags & BLOCK_CLOBBERING ) != 0;
		opts.blockOverlay = ( flags & BLOCK_OVERLAY ) != 0;
		opts.restrictHosts = ctrl.restrictHosts;
		opts.allowedHosts = ctrl.allowedHosts;
		opts.reservedNames = ctrl.reservedNames;
		return opts;
	}

	/**
	 * @brief Core close logic shared by l_quit and l_gc.
	 *
	 * Disarms the handle first (every later call from Lua is a no-op), then asks for the close. Inside a
	 * guarded frame - a Lua callback fired by a COM event, a bound function called by the page, a nested
	 * message loop - the teardown is deferred until the frame is gone (item 36); otherwise it runs now.
	 * The resources are released by TeardownControl(), the same path Cleanup() uses (item 37).
	 */
	static void DoQuit( ControlHandle *h, lua_State * ) {
		if ( !h || !h->ctrl )
			return;

		Control *ctrl = h->ctrl;
		h->ctrl = nullptr; // disarm all closures
		ctrl->handle = nullptr; // the handle may be freed right after this call (l_gc)

		RequestCloseControl( ctrl->rain, ctrl->configId );
	}

	/**
	 * @brief Queues @p action when called from inside a COM event of the browser (item 36).
	 *
	 * Navigating, reloading or rewriting the document from a BeforeNavigate2 / NewWindow / ... callback
	 * re-enters MSHTML while it is still dispatching that event. The action is therefore run from the idle
	 * message instead; the control is looked up again by id, so it is simply dropped if it was closed meanwhile.
	 *
	 * @return true when the action was queued (the caller must not run it), false when it can run now.
	 */
	static bool DeferInComEvent( Control *ctrl, std::function<void( Control & )> action ) {
		if ( !InComEvent() )
			return false;
		Rain *rain = ctrl->rain;
		const int id = ctrl->configId;
		DeferToIdle( rain, [rain, id, action]() {
			Control *c = FindControl( rain, id );
			if ( c && c->webBrowser && !c->closing )
				action( *c );
		} );
		return true;
	}

	/**
	 * @brief Validates and performs a navigation (body of l_navigate).
	 */
	static void ApplyNavigate( Control &ctrl, const std::string &url ) {
		ReentryGuard guard; // Navigate2 fires BeforeNavigate2 synchronously

		bool isLocal = false;
		std::wstring wurl = ResolveUrl( ctrl.rain, url.c_str(), isLocal );

		if ( wurl.empty() ) {
			RN_LOG( ctrl.rain, LOG_WARNING, ( L"[RainJIT:Trident] navigate() failed to resolve: " + utf8_to_wstring( url.c_str() ) ).c_str() );
			return;
		}

		if ( ctrl.sanitizeFlags & VALIDATE_URLS ) {
			bool allowLocal = ( ctrl.sanitizeFlags & ALLOW_LOCAL ) != 0;
			if ( !sanitize::IsUrlSafe( wstring_to_utf8( wurl ), allowLocal ) ) {
				RN_LOG( ctrl.rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] navigate() blocked: " + wurl ).c_str() );
				return;
			}
		}

		// Unconditional VBScript / local-file guard (also runs when sanitize = false / allow_urls).
		if ( !IsNavigationAllowed( ctrl, wurl ) ) {
			RN_LOG( ctrl.rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] navigate() blocked (VBScript or disallowed URL): " + wurl ).c_str() );
			return;
		}

		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( NavigateFlags( ctrl ) ); // item 32: isolated from IE's cache unless create{ cache = true }
		ctrl.programmaticNav = true; // lets BeforeNavigate2 tell this apart from clicks/scripts (navigation = "locked")
		ctrl.webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );
		ctrl.programmaticNav = false;
	}

	enum class NavStep { Back, Forward, Refresh };

	/**
	 * @brief Performs back() / forward() / refresh() on the control.
	 */
	static void ApplyNavStep( Control &ctrl, NavStep step ) {
		ReentryGuard guard;
		ctrl.programmaticNav = true;
		switch ( step ) {
			case NavStep::Back: ctrl.webBrowser->GoBack(); break;
			case NavStep::Forward: ctrl.webBrowser->GoForward(); break;
			case NavStep::Refresh: ctrl.webBrowser->Refresh(); break;
		}
		ctrl.programmaticNav = false;
	}

	/**
	 * @brief Writes already sanitized HTML (body of l_write, after validation).
	 *
	 * @param[out] err      Failure message when the function returns false.
	 * @param[out] deferred Set when the HTML will be written once about:blank has loaded.
	 * @return true when written or queued, false on failure.
	 */
	static bool ApplyWrite( Control &ctrl, std::string content, std::string &err, bool &deferred ) {
		ReentryGuard guard; // document.write / script execution can pump messages and fire events
		deferred = false;

		CComPtr<IDispatch> docDisp;
		const bool hasDocument = SUCCEEDED( ctrl.webBrowser->get_Document( &docDisp ) ) && docDisp;

		if ( hasDocument && IsBlankLocation( ctrl ) ) {
			ctrl.hasPendingWrite = false;
			ctrl.pendingHtml.clear();
			if ( !WriteIntoDocument( ctrl, content, err ) )
				return false;
			ResetThemeState( ctrl ); // a new document: write data-theme / data-* on it right away
			ApplyThemeData( ctrl );
			return true;
		}

		// Defer: write when about:blank has been loaded (see the DocumentComplete handler).
		ctrl.pendingHtml = std::move( content );
		ctrl.hasPendingWrite = true;
		deferred = true;
		if ( hasDocument ) {
			ctrl.programmaticNav = true;
			CComVariant vBlank( L"about:blank" );
			ctrl.webBrowser->Navigate2( &vBlank, nullptr, nullptr, nullptr, nullptr );
			ctrl.programmaticNav = false;
		}
		return true;
	}

	int l_navigate( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;

		const std::string url = luaL_checkstring( L, 2 );
		if ( DeferInComEvent( ctrl, [url]( Control &c ) { ApplyNavigate( c, url ); } ) )
			return 0; // called from a browser event: runs right after it
		ApplyNavigate( *ctrl, url );
		return 0;
	}

	int l_back( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		if ( DeferInComEvent( ctrl, []( Control &c ) { ApplyNavStep( c, NavStep::Back ); } ) )
			return 0;
		ApplyNavStep( *ctrl, NavStep::Back );
		return 0;
	}

	int l_forward( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		if ( DeferInComEvent( ctrl, []( Control &c ) { ApplyNavStep( c, NavStep::Forward ); } ) )
			return 0;
		ApplyNavStep( *ctrl, NavStep::Forward );
		return 0;
	}

	int l_refresh( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		if ( DeferInComEvent( ctrl, []( Control &c ) { ApplyNavStep( c, NavStep::Refresh ); } ) )
			return 0;
		ApplyNavStep( *ctrl, NavStep::Refresh );
		return 0;
	}

	int l_stop( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ReentryGuard guard; // item 36
		ctrl->webBrowser->Stop();
		return 0;
	}

	/**
	 * @brief browser:write( html ) - replaces the page with sanitized HTML.
	 *
	 * Returns `true` when the HTML was written (or `true, "deferred"` when it will be written as soon as an
	 * about:blank document is loaded), or `nil, message` on failure (item 31: it used to fail silently).
	 *
	 * The HTML is always written into an about:blank document: if the current page came from a URL or a
	 * file, the control first navigates to about:blank, because document.open() on a remote document keeps
	 * its (remote) origin. The same happens when no document exists yet (create() just ran).
	 */
	int l_write( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser ) {
			lua_pushnil( L );
			lua_pushstring( L, "The browser is closed" );
			return 2;
		}
		size_t htmlLen = 0;
		const char *htmlPtr = luaL_checklstring( L, 2, &htmlLen );
		const std::string html( htmlPtr, htmlLen );
		std::string content = html;
		const bool sanitizing = ( ctrl->sanitizeFlags & ( BLOCK_SCRIPTS | BLOCK_EVENTS | FILTER_CSS | BLOCK_STYLE | VALIDATE_URLS | BLOCK_CLOBBERING | BLOCK_OVERLAY ) ) != 0;
		// Unconditional: with the sanitizer off the content is written verbatim, so it is refused
		// if it mentions VBScript in any spelling. (When the sanitizer runs, its serialiser drops
		// VBScript structurally, whatever the Options.)
		if ( !sanitizing && sanitize::ContainsVbScript( html ) ) {
			RN_LOG( ctrl->rain, LOG_WARNING, L"[RainJIT:Trident] [SECURITY] write() refused: content contains VBScript." );
			lua_pushnil( L );
			lua_pushstring( L, "VBScript is blocked" );
			return 2;
		}
		if ( sanitizing ) {
			sanitize::Options opts = FlagsToOptions( *ctrl );
			content = sanitize::Html( html, opts ); // item 16: full document (write() resets it with document.open()); keeps DOCTYPE/head/body attributes
			if ( content != html )
				RN_LOG( ctrl->rain, LOG_WARNING,
								L"[RainJIT:Trident] [SECURITY] write() content was modified - "
								L"dangerous tags, event attributes, or blocked CSS were removed." );
			if ( content.empty() && !html.empty() ) {
				lua_pushnil( L );
				lua_pushstring( L, "The HTML was rejected by the sanitizer (too large, too deeply nested or unstable)" );
				return 2;
			}
		}

		if ( DeferInComEvent( ctrl, [content]( Control &c ) {
					std::string err;
					bool deferred = false;
					if ( !ApplyWrite( c, content, err, deferred ) )
						RN_LOG( c.rain, LOG_WARNING, ( L"[RainJIT:Trident] write() failed: " + utf8_to_wstring( err.c_str() ) ).c_str() );
				} ) ) {
			lua_pushboolean( L, 1 );
			lua_pushstring( L, "deferred" ); // written right after the current browser event
			return 2;
		}

		std::string err;
		bool deferred = false;
		if ( !ApplyWrite( *ctrl, std::move( content ), err, deferred ) ) {
			lua_pushnil( L );
			lua_pushstring( L, err.c_str() );
			return 2;
		}
		lua_pushboolean( L, 1 );
		if ( !deferred )
			return 1;
		lua_pushstring( L, "deferred" );
		return 2;
	}

	/**
	 * @brief browser:redraw( [data [, theme]] ) - re-applies the theme and the data-* attributes to the page.
	 *
	 *  - `redraw()`           re-reads the Windows theme (when create{ theme } was not given) and writes data-theme
	 *                         and the data-* attributes again.
	 *  - `redraw( { ... } )`  REPLACES every data-* attribute by the ones in the table (the theme is kept);
	 *                         `redraw( {} )` removes them all.
	 *  - `redraw( nil, "dark" )` also changes the theme: "light", "dark", or "" / "auto" to follow Windows again.
	 *
	 * Returns the data table now in effect and the theme in use ("light" / "dark"), or `nil, message` on
	 * invalid arguments (nothing changes then). Called from a browser event, the page is updated right after it.
	 */
	int l_redraw( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser ) {
			lua_pushnil( L );
			lua_pushstring( L, "The browser is closed" );
			return 2;
		}

		std::map<std::string, std::string> data = ctrl->data;
		if ( lua_istable( L, 2 ) ) {
			data.clear();
			std::vector<std::string> problems;
			ReadDataTable( L, 2, data, problems );
			if ( !problems.empty() ) {
				lua_pushnil( L );
				lua_pushstring( L, ( "redraw: " + problems.front() ).c_str() );
				return 2;
			}
		} else if ( !lua_isnoneornil( L, 2 ) ) {
			lua_pushnil( L );
			lua_pushstring( L, "redraw: data must be a table" );
			return 2;
		}

		std::string theme = ctrl->themeOption;
		if ( lua_type( L, 3 ) == LUA_TSTRING ) {
			bool ok = true;
			theme = NormalizeThemeName( lua_tostring( L, 3 ), ok );
			if ( !ok ) {
				lua_pushnil( L );
				lua_pushstring( L, "redraw: theme must be \"light\", \"dark\" or \"\" (follow Windows)" );
				return 2;
			}
		} else if ( !lua_isnoneornil( L, 3 ) ) {
			lua_pushnil( L );
			lua_pushstring( L, "redraw: theme must be a string" );
			return 2;
		}

		ctrl->themeOption = theme;
		ctrl->data = data;

		if ( !DeferInComEvent( ctrl, []( Control &c ) { ApplyThemeData( c ); } ) )
			ApplyThemeData( *ctrl );

		PushDataTable( L, ctrl->data );
		const std::string effective = EffectiveTheme( *ctrl );
		lua_pushlstring( L, effective.c_str(), effective.size() );
		return 2;
	}

	int l_setTransparent( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl )
			return 0;
		bool enable = lua_toboolean( L, 2 );
		COLORREF color = ctrl->colorKey;
		if ( lua_isnumber( L, 3 ) ) {
			color = RgbToColorRef( lua_tointeger( L, 3 ) );
		}
		ctrl->transparent = enable;
		ctrl->colorKey = color;
		if ( ctrl->hwndControl && IsWindow( ctrl->hwndControl ) ) {
			LONG_PTR exStyle = GetWindowLongPtr( ctrl->hwndControl, GWL_EXSTYLE );
			if ( enable ) {
				SetWindowLongPtr( ctrl->hwndControl, GWL_EXSTYLE, exStyle | WS_EX_LAYERED );
				SetLayeredWindowAttributes( ctrl->hwndControl, color, 0, LWA_COLORKEY );
			} else {
				SetWindowLongPtr( ctrl->hwndControl, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED );
			}
		}
		return 0;
	}

	/**
	 * @brief browser:execScript( jsOrFile ) - runs JavaScript in the page and returns its result.
	 *
	 * Works in every document mode (IE7/IE8 have no `window.eval`; see EvalInWindow).
	 *
	 * The argument is the BODY of a function (use `return`), or the path of a `.js` file (relative to the
	 * skin, "./x.js", or with Rainmeter variables). The result keeps its type: number, string, boolean,
	 * nil (null/undefined) or a ComProxy for objects. On failure it returns `nil, message`.
	 * No "use strict" is added: it has no effect in the IE7 engine mode and would break sloppy-mode pages
	 * in newer document modes.
	 */
	int l_execScript( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser ) {
			lua_pushnil( L );
			lua_pushstring( L, "The browser is closed" );
			return 2;
		}

		size_t argLen = 0;
		const char *arg = luaL_checklstring( L, 2, &argLen );
		std::string script( arg, argLen );

		if ( LooksLikeScriptFile( script ) ) {
			std::wstring warg = utf8_to_wstring( script );
			// "./" or ".\" -> skin directory, same as ResolveUrl
			if ( warg.size() >= 2 && warg[0] == L'.' && ( warg[1] == L'/' || warg[1] == L'\\' ) )
				warg = L"#CURRENTPATH#" + warg.substr( 2 );
			std::wstring resolved = ctrl->rain->absPath( warg );
			if ( resolved.empty() || !fs::fileExists( resolved ) ) {
				lua_pushnil( L );
				lua_pushstring( L, "Script file not found" );
				return 2;
			}
			if ( !fs::ReadTextFile( resolved, script ) ) {
				lua_pushnil( L );
				lua_pushstring( L, "Failed to read script file" );
				return 2;
			}
		}

		ReentryGuard guard; // item 36: document.write / script execution can pump messages and fire events

		CComPtr<IDispatch> docDisp;
		if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
			lua_pushnil( L );
			lua_pushstring( L, "No document" );
			return 2;
		}
		CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
		if ( !htmlDoc ) {
			lua_pushnil( L );
			lua_pushstring( L, "Invalid document" );
			return 2;
		}
		CComPtr<IHTMLWindow2> window;
		if ( FAILED( htmlDoc->get_parentWindow( &window ) ) || !window ) {
			lua_pushnil( L );
			lua_pushstring( L, "No window" );
			return 2;
		}

		CComVariant result;
		std::wstring error;
		if ( !EvalInWindow( window, utf8_to_wstring( script ), &result, error ) ) {
			const std::string msg = wstring_to_utf8( error );
			lua_pushnil( L );
			lua_pushlstring( L, msg.c_str(), msg.size() );
			return 2;
		}

		luaVariant::Push( L, result ); // typed: number / string / boolean / nil / ComProxy
		return 1;
	}

	int l_getURL( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser ) {
			lua_pushnil( L );
			return 1;
		}
		BSTR url = nullptr;
		if ( SUCCEEDED( ctrl->webBrowser->get_LocationURL( &url ) ) && url ) {
			std::string utf8 = wstring_to_utf8( url );
			lua_pushlstring( L, utf8.c_str(), utf8.size() );
			SysFreeString( url );
		} else {
			lua_pushnil( L );
		}
		return 1;
	}

	int l_reposition( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl || !ctrl->hwndParent )
			return 0;
		UpdateControlPosition( ctrl );
		return 0;
	}

	int l_show( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = false;
		ShowWindow( ctrl->hwndControl, SW_SHOWNOACTIVATE );
		UpdateControlPosition( ctrl );
		return 0;
	}

	int l_hide( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = true;
		ShowWindow( ctrl->hwndControl, SW_HIDE );
		return 0;
	}

	int l_setContextMenu( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl )
			return 0;

		// Item 14: method call (browser:setContextMenu(false)) - the user argument is at index 2.
		if ( lua_isboolean( L, 2 ) ) {
			ctrl->contextMenu = lua_toboolean( L, 2 ) != 0;
			ApplyAmbientHostUi( *ctrl ); // item 21: keep the host-level policy in sync
		}

		return 0;
	}

	int l_quit( lua_State *L ) {
		ControlHandle *h = HandleFromUpvalue( L );
		DoQuit( h, L );
		return 0;
	}

	int l_gc( lua_State *L ) {
		ControlHandle **pp = (ControlHandle **)lua_touserdata( L, 1 );
		if ( !pp || !*pp )
			return 0;
		ControlHandle *h = *pp;
		*pp = nullptr;
		DoQuit( h, L );
		delete h;
		return 0;
	}


} // namespace trident
