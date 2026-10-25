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
 * @file trident.cpp
 * @brief Module lifecycle: ProcessMessages(), Cleanup(), luaopen_trident() and RegisterModule().
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * FIX #4: g_contextsMutex is NOT held while ProcessMessages calls
	 * DrainEventQueue. Callbacks fired during drain may call browser:quit()
	 * which acquires the mutex in DoQuit - holding it here would deadlock.
	 */
	int ProcessMessages( Rain *rain ) {
		std::shared_ptr<Context> ctx;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = g_contexts.find( rain );
			if ( it == g_contexts.end() )
				return 0;
			ctx = it->second;
		}
		return DrainEventQueue( ctx.get(), rain );
	}

	/**
	 * Fix #10: g_contextsMutex is held ONLY to detach the context from g_contexts.
	 * All teardown (DestroyWindow, COM Release, ExternalDispatch cleanup) runs unlocked:
	 * those calls can re-enter our own COM objects (GetExternal, event sink), and
	 * holding a non-recursive mutex there deadlocked.
	 *
	 * Fix #11: the Context is a shared_ptr and is NOT deleted here. COM objects handed to
	 * MSHTML (EventSink, WebBrowserSite, ExternalDispatch) keep it alive and see
	 * `alive == false` / an empty `controls` map after this function returns.
	 */
	void Cleanup( Rain *rain ) {
		std::shared_ptr<Context> ctx;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = g_contexts.find( rain );
			if ( it == g_contexts.end() )
				return;
			ctx = it->second;
			g_contexts.erase( it );
		}

		if ( ctx->hiddenWindow ) {
			HWND hidden = ctx->hiddenWindow;
			ctx->hiddenWindow = nullptr;
			DestroyWindow( hidden );
		}

		DiscardDeferred( *ctx );

		for ( auto &pair : ctx->controls ) {
			Control &ctrl = *pair.second;

			if ( ctrl.handle ) {
				static_cast<ControlHandle *>( ctrl.handle )->ctrl = nullptr;
				ctrl.handle = nullptr;
			}

			TeardownControl( ctrl, rain ); // item 37: the same path as quit()
		}

		// Items 25/26: Lua functions/tables that were handed to COM (event handlers, callbacks) must stop
		// referencing the state before it goes away; COM may still hold them.
		if ( rain && rain->L ) {
			luaVariant::ReleaseAll( rain->L );
			ComProxy::ReleaseAll( rain->L ); // item 37: drop the COM references before OleUninitialize
		}

		ctx->alive = false;
		ctx->rain = nullptr;
		ctx->controls.clear();
		// ctx is released when the last COM object referencing it goes away.

		bool lastContext;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			lastContext = g_contexts.empty();
		}
		if ( lastContext ) {
			UnblockVbScriptEngine(); // must run on the thread that registered the classes, before OleUninitialize
			UnregisterHiddenWindowClass(); // item 35: all hidden windows are gone; do not leave a class with a stale procedure
		}
		if ( lastContext && g_comInitialized && g_comNeedsUninitialize ) {
			OleUninitialize();
			g_comInitialized = false;
			g_comNeedsUninitialize = false;
		}
	}

	// -------------------------------------------------------------------------
	// Module entry point
	// -------------------------------------------------------------------------

	extern "C" int luaopen_trident( lua_State *L ) {
		Rain *rain = (Rain *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		lua_newtable( L );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, l_create, 1 );
		lua_setfield( L, -2, "create" );
		return 1;
	}

	void RegisterModule( lua_State *L, Rain *rain ) {
		ComProxy::Register( L );
		// Item 36: Lua functions called by COM (event handlers written in Lua) count as re-entrant frames.
		luaVariant::CallEnterHook() = &ComCallEnter;
		luaVariant::CallLeaveHook() = &ComCallLeave;
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_trident, 1 );
		lua_setfield( L, -2, "webview.trident" );
		lua_pop( L, 2 );
	}


} // namespace trident
