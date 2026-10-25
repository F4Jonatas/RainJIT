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
 * @file trident_object.cpp
 * @brief Construction of the Lua browser object returned by trident.create().
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Builds the Lua browser object of a created control (item 23).
	 *
	 * Pushes the handle box (a full userdata whose __gc is l_gc), the method closures that share it as
	 * upvalue 1, `bind`, `hwnd` and the live `document` / `window` properties, and registers the table in
	 * the Lua registry so it lives until quit() or skin unload.
	 *
	 * @return 1: the browser table is on top of the stack (the box stays below it).
	 */
	int PushBrowserObject( lua_State *L, Rain *rain, Control *storedCtrl, ControlHandle *handle ) {
		// Item 23: handle box. A full userdata whose __gc (l_gc) quits the control and frees the handle.
		// It is the upvalue of EVERY method closure, so the handle outlives every closure that can use it.
		ControlHandle **handleBox = static_cast<ControlHandle **>( lua_newuserdata( L, sizeof( ControlHandle * ) ) );
		*handleBox = handle;
		lua_newtable( L );
		lua_pushcfunction( L, l_gc );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );
		const int boxIndex = lua_gettop( L );

		// Build Lua browser table (no lock needed - unique_ptr keeps address stable).
		lua_newtable( L );

		auto pushMethod = [&]( const char *name, lua_CFunction fn ) {
			lua_pushvalue( L, boxIndex );
			lua_pushcclosure( L, fn, 1 );
			lua_setfield( L, -2, name );
		};

		pushMethod( "navigate", l_navigate );
		pushMethod( "back", l_back );
		pushMethod( "forward", l_forward );
		pushMethod( "refresh", l_refresh );
		pushMethod( "stop", l_stop );
		pushMethod( "write", l_write );
		pushMethod( "setTransparent", l_setTransparent );
		pushMethod( "execScript", l_execScript );
		pushMethod( "getURL", l_getURL );
		pushMethod( "quit", l_quit );
		pushMethod( "reposition", l_reposition );
		pushMethod( "show",           l_show           );
		pushMethod( "hide",           l_hide           );
		pushMethod( "setContextMenu", l_setContextMenu );
		pushMethod( "redraw",         l_redraw         );

		// browser:bind(name, func)
		lua_pushvalue( L, boxIndex );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					ControlHandle *h = HandleFromUpvalue( L );
					Control *ctrl = h ? h->ctrl : nullptr;
					if ( !ctrl || !ctrl->externalDispatch ) {
						luaL_error( L, "Browser not ready or externalDispatch missing" );
						return 0;
					}
					const char *name = luaL_checkstring( L, 2 );
					luaL_checktype( L, 3, LUA_TFUNCTION );
					if ( FAILED( ExternalBind( ctrl->externalDispatch, name, L, 3 ) ) )
						luaL_error( L, "Failed to bind method '%s' (empty or reserved name, or too many names)", name );
					return 0;
				},
				1 );
		lua_setfield( L, -2, "bind" );

		lua_pushlightuserdata( L, (void *)storedCtrl->hwndControl );
		lua_setfield( L, -2, "hwnd" );

		// browser.document and browser.window are live properties resolved via
		// __index - no () needed, consistent with comProxy behaviour.
		lua_newtable( L ); // metatable

		lua_pushvalue( L, boxIndex );
		lua_pushcclosure( L, []( lua_State *L ) -> int {
			const char *key = lua_tostring( L, 2 );
			if ( !key ) { lua_pushnil( L ); return 1; }

			ControlHandle *h = HandleFromUpvalue( L );
			Control *ctrl = h ? h->ctrl : nullptr;
			if ( !ctrl || !ctrl->webBrowser ) { lua_pushnil( L ); return 1; }

			if ( strcmp( key, "document" ) == 0 ) {
				CComPtr<IDispatch> docDisp;
				if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
					lua_pushnil( L ); return 1;
				}
				ComProxy::Push( L, docDisp );
				return 1;
			}

			if ( strcmp( key, "window" ) == 0 ) {
				CComPtr<IDispatch> docDisp;
				if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
					lua_pushnil( L ); return 1;
				}
				CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
				if ( !htmlDoc ) { lua_pushnil( L ); return 1; }
				CComPtr<IHTMLWindow2> win;
				if ( FAILED( htmlDoc->get_parentWindow( &win ) ) || !win ) {
					lua_pushnil( L ); return 1;
				}
				CComQIPtr<IDispatch> winDisp( win );
				ComProxy::Push( L, winDisp );
				return 1;
			}

			// Any other key falls back to the browser table itself
			lua_rawget( L, 1 );
			return 1;
		}, 1 );

		lua_setfield( L, -2, "__index" );
		lua_setmetatable( L, -2 );

		// Item 23: the key no longer depends on the callback (it used to be just "trident_browser_" for
		// controls without one, shared by all of them). The registry keeps the table - and so the control -
		// alive until quit() / skin unload; keeping the return value of create() is optional.
		storedCtrl->browserKey = GetBrowserKey( rain, storedCtrl->configId );
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, storedCtrl->browserKey.c_str() );

		// Callback API is explicitly (browser, event) - no environment injection needed.
		// All call sites (DrainEventQueue, BeforeNavigate2) already pass browser as arg1.

		// The box is also reachable from the table (browser.__sentinel), as before.
		lua_pushvalue( L, boxIndex );
		lua_setfield( L, -2, "__sentinel" );

		return 1; // the browser table (top of stack); the box stays below it.
	}


} // namespace trident
