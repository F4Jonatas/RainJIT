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
 * @file trident_create.cpp
 * @brief trident.create(): shared state, control creation and the three-phase locking protocol.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Creates a new browser control.
	 *
	 * Lua signature: trident.create(table)
	 *
	 * Lock discipline - three distinct phases:
	 *   PHASE 1 (locked)   - COM init, Context creation.
	 *   PHASE 2 (unlocked) - Lua option parsing, CreateWebBrowserControl, Navigate2.
	 *                         Trident may call GetExternal here; lock must NOT be held.
	 *   PHASE 3 (locked)   - Insert Control into map, create ExternalDispatch/ControlHandle.
	 *
	 * FIX #5: AtlAxWinInit() moved outside the mutex (idempotent, no shared state to protect).
	 */
	int l_create( lua_State *L ) {
		Rain *rain = (Rain *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !rain || !rain->hwnd ) {
			lua_pushnil( L );
			lua_pushstring( L, "Invalid Rain instance" );
			return 2;
		}

		// FIX #5: outside the lock - idempotent Win32 class registration.
		AtlAxWinInit();

		// -------------------------------------------------------------------
		// PHASE 1 - shared state only, safe to hold the lock.
		// -------------------------------------------------------------------
		std::shared_ptr<Context> ctx;
		int configId = 0;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );

			if ( !g_comInitialized ) {
				HRESULT hr = OleInitialize( nullptr ); // not CoInitializeEx
				if ( hr == S_OK || hr == S_FALSE ) {
					g_comInitialized = true;
					g_comNeedsUninitialize = true;
				} else if ( hr == RPC_E_CHANGED_MODE ) {
					g_comInitialized = true;
					g_comNeedsUninitialize = false;
					RN_LOG( rain, LOG_WARNING, L"[RainJIT:Trident] COM already initialized with incompatible threading model." );
				} else {
					lua_pushnil( L );
					lua_pushstring( L, "OLE initialization failed" );
					return 2;
				}
			}
			ApplyProcessHardening(); // item 8 (idempotent)
			BlockVbScriptEngine(); // unconditional VBScript block (idempotent)

			auto it = g_contexts.find( rain );
			if ( it == g_contexts.end() ) {
				ctx = std::make_shared<Context>();
				ctx->rain = rain;
				ctx->hasPendingEvents = false;
				ctx->hiddenWindow = CreateHiddenWindow( ctx.get() );
				g_contexts[rain] = ctx;
			} else {
				ctx = it->second;
			}

			configId = g_nextControlId.fetch_add( 1 ); // item 22: process-unique, not per Rain
		}
		// g_contextsMutex is now FREE - Trident can call GetExternal safely.

		// -------------------------------------------------------------------
		// PHASE 2 - Lua parsing + COM operations. No lock held.
		// -------------------------------------------------------------------
		luaL_checktype( L, 1, LUA_TTABLE );
		Control ctrl = {};
		ctrl.configId = configId;
		ctrl.rain = rain;
		ParseCreateOptions( L, rain, ctrl );

		const std::wstring wurl = ResolveInitialUrl( L, rain, ctrl );

		std::string errorMsg;
		if ( !CreateWebBrowserControl( ctrl, rain, errorMsg ) ) {
			// Item 37: ParseCreateOptions() already stored the callback in the Lua registry; nobody owns it now.
			ClearRegistryKey( L, ctrl.callbackKey );
			lua_pushnil( L );
			lua_pushstring( L, errorMsg.c_str() );
			return 2;
		}

		ConnectEventSink( ctrl, ctx );

		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( NavigateFlags( ctrl ) ); // item 32: isolated from IE's cache unless create{ cache = true }
		ctrl.webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );

		// -------------------------------------------------------------------
		// PHASE 3 - insert into map, build Lua object. Lock re-acquired.
		// -------------------------------------------------------------------
		Control *storedCtrl = nullptr;
		ControlHandle *handle = nullptr;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );

			ctx->controls[ctrl.configId] = std::make_unique<Control>( ctrl );
			storedCtrl = ctx->controls[ctrl.configId].get();

			if ( storedCtrl->subclassData ) {
				ParentSubclassData *sd = (ParentSubclassData *)storedCtrl->subclassData;
				sd->ctrl = storedCtrl;
			}
			// Fix #12/#25: the subclass procs dereference data->ctrl on WM_NCDESTROY, so it
			// must point at the stored Control, not at the (now dead) stack copy.
			if ( storedCtrl->controlSubclassData )
				( (ControlSubclassData *)storedCtrl->controlSubclassData )->ctrl = storedCtrl;

			storedCtrl->externalDispatch = ExternalCreate( ctx, storedCtrl->configId );

			handle = new ControlHandle{ storedCtrl };
			storedCtrl->handle = handle;
		}

		return PushBrowserObject( L, rain, storedCtrl, handle );
	}


} // namespace trident
