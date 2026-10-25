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
 * @file trident_lifetime.cpp
 * @brief Control lifetime: the single teardown path, deferred closing and the re-entrancy guard.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * @details
 * Lua callbacks run inside COM events (BeforeNavigate2, NewWindow3, WindowClosing, window.external calls,
 * COM event handlers written in Lua) and inside nested message loops (write(), execScript(), Navigate2).
 * Destroying a control while one of those frames is still on the stack frees objects that the frame is
 * about to use (item 36). The rules are therefore:
 *
 *  - Code that can re-enter Lua or COM holds a ReentryGuard (a process-wide depth counter).
 *  - quit() / garbage collection while the depth is above zero only disarms the Lua handle, stops the
 *    callbacks of the control and queues the close; the real teardown runs from a posted message once the
 *    depth is back to zero (see FlushDeferred()).
 *  - navigate(), back(), forward(), refresh() and write() requested from inside a COM event of the browser
 *    are queued the same way and run after the event (see DeferToIdle()).
 *  - Skin unload (Cleanup) and the deferred close share TeardownControl(), so the two paths can no longer
 *    diverge (item 37).
 */

#include "trident_internal.hpp"

namespace trident {

	/// Depth of frames that may re-enter Lua/COM (see ReentryGuard).
	int g_reentry = 0;
	/// Depth of frames that run inside a COM event of the browser.
	int g_comEvent = 0;
	/// Number of queued closes and deferred actions waiting for the depth to reach zero.
	static int g_pendingWork = 0;

	std::shared_ptr<Context> FindContext( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_contextsMutex );
		auto it = g_contexts.find( rain );
		return it != g_contexts.end() ? it->second : nullptr;
	}

	void ReentryEnter() {
		++g_reentry;
	}

	void ReentryLeave() {
		if ( g_reentry > 0 )
			--g_reentry;
		if ( g_reentry == 0 && g_pendingWork > 0 )
			PostFlushRequests();
	}

	ReentryGuard::ReentryGuard() {
		ReentryEnter();
	}

	ReentryGuard::~ReentryGuard() {
		ReentryLeave();
	}

	void ComCallEnter() {
		++g_comEvent;
		ReentryEnter();
	}

	void ComCallLeave() {
		if ( g_comEvent > 0 )
			--g_comEvent;
		ReentryLeave();
	}

	ComEventGuard::ComEventGuard() {
		++g_comEvent; // the base class (ReentryGuard) has already entered the generic depth
	}

	ComEventGuard::~ComEventGuard() {
		if ( g_comEvent > 0 )
			--g_comEvent;
	}

	bool InComEvent() {
		return g_comEvent > 0;
	}

	Control *FindControl( Rain *rain, int configId ) {
		std::shared_ptr<Context> ctx = FindContext( rain );
		if ( !ctx )
			return nullptr;
		std::lock_guard<std::mutex> lock( g_contextsMutex );
		auto it = ctx->controls.find( configId );
		return it != ctx->controls.end() ? it->second.get() : nullptr;
	}

	void DeferToIdle( Rain *rain, std::function<void()> fn ) {
		std::shared_ptr<Context> ctx = FindContext( rain );
		if ( !ctx )
			return;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			ctx->deferred.push_back( std::move( fn ) );
			++g_pendingWork;
		}
		if ( g_reentry == 0 )
			PostFlushRequests();
		// otherwise the guard that brings the depth to zero posts the flush (ReentryLeave)
	}

	void PostFlushRequests() {
		std::vector<HWND> targets;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			for ( auto &pair : g_contexts ) {
				if ( ( !pair.second->pendingClose.empty() || !pair.second->deferred.empty() ) && pair.second->hiddenWindow )
					targets.push_back( pair.second->hiddenWindow );
			}
		}
		for ( HWND h : targets )
			PostMessageW( h, WM_TRIDENT_FLUSH, 0, 0 );
	}

	/**
	 * @brief Releases every resource of a control. Idempotent; the Control object itself is not freed.
	 *
	 * Order matters: the event sink is disconnected first (no more events), the window subclasses are removed
	 * before the windows go away, the browser is released before its window is destroyed, and
	 * window.external is shut down last because it needs `ctx->alive` and the control still in the map.
	 */
	void TeardownControl( Control &ctrl, Rain *rain ) {
		ctrl.enabled = false;
		ctrl.closing = true;
		const int configId = ctrl.configId;

		DisconnectEventSink( ctrl );

		if ( ctrl.subclassData ) {
			ParentSubclassData *data = (ParentSubclassData *)ctrl.subclassData;
			ctrl.subclassData = nullptr;
			RemoveWindowSubclass( data->hWndParent, ParentSubclassProc, (UINT_PTR)configId );
			delete data;
		}
		if ( ctrl.ieServerSubclassData ) {
			IEServerSubclassData *data = (IEServerSubclassData *)ctrl.ieServerSubclassData;
			ctrl.ieServerSubclassData = nullptr;
			if ( ctrl.hwndIEServer && IsWindow( ctrl.hwndIEServer ) )
				RemoveWindowSubclass( ctrl.hwndIEServer, IEServerSubclassProc, (UINT_PTR)configId );
			ctrl.hwndIEServer = nullptr;
			delete data;
		}
		if ( ctrl.controlSubclassData ) {
			ControlSubclassData *data = (ControlSubclassData *)ctrl.controlSubclassData;
			ctrl.controlSubclassData = nullptr;
			if ( ctrl.hwndControl && IsWindow( ctrl.hwndControl ) )
				RemoveWindowSubclass( ctrl.hwndControl, ControlSubclassProc, (UINT_PTR)configId );
			delete data;
		}
		if ( ctrl.webBrowser ) {
			ctrl.webBrowser->Stop();
			ctrl.webBrowser->Release();
			ctrl.webBrowser = nullptr;
		}
		if ( ctrl.oleObject ) {
			ctrl.oleObject->Close( OLECLOSE_NOSAVE );
			ctrl.oleObject->Release();
			ctrl.oleObject = nullptr;
		}
		if ( ctrl.hwndControl ) {
			HWND h = ctrl.hwndControl;
			ctrl.hwndControl = nullptr;
			DestroyWindow( h );
		}
		if ( ctrl.externalDispatch ) {
			void *ext = ctrl.externalDispatch;
			ctrl.externalDispatch = nullptr;
			ExternalShutdown( ext ); // needs ctx->alive and the control still in the map
		}

		if ( rain && rain->L ) {
			ClearRegistryKey( rain->L, ctrl.callbackKey );
			ClearRegistryKey( rain->L, ctrl.browserKey );
		}
	}

	void ClearRegistryKey( lua_State *L, const std::string &key ) {
		if ( !L || key.empty() )
			return;
		lua_pushnil( L );
		lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
	}

	void CloseControl( Rain *rain, int configId ) {
		std::shared_ptr<Context> ctx = FindContext( rain );
		if ( !ctx )
			return;
		Control *ctrl = nullptr;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = ctx->controls.find( configId );
			if ( it == ctx->controls.end() )
				return;
			ctrl = it->second.get();
		}

		TeardownControl( *ctrl, rain ); // unlocked: these calls can re-enter our own COM objects

		std::lock_guard<std::mutex> lock( g_contextsMutex );
		ctx->controls.erase( configId );
	}

	void RequestCloseControl( Rain *rain, int configId ) {
		std::shared_ptr<Context> ctx = FindContext( rain );
		if ( !ctx )
			return;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = ctx->controls.find( configId );
			if ( it == ctx->controls.end() )
				return;
			it->second->enabled = false; // no more Lua callbacks for this control from now on
			it->second->closing = true;
			if ( g_reentry > 0 ) {
				ctx->pendingClose.push_back( configId );
				++g_pendingWork;
				return; // closed by FlushDeferred() once no frame that could re-enter is left
			}
		}
		CloseControl( rain, configId );
	}

	void FlushDeferred() {
		if ( g_reentry > 0 )
			return; // a guarded frame is still on the stack; the guard reposts the flush when it leaves

		struct Work {
			std::shared_ptr<Context> ctx;
			std::vector<std::function<void()>> actions;
			std::vector<int> closes;
		};
		std::vector<Work> work;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			for ( auto &pair : g_contexts ) {
				Context &ctx = *pair.second;
				if ( ctx.pendingClose.empty() && ctx.deferred.empty() )
					continue;
				Work w;
				w.ctx = pair.second;
				w.actions = std::move( ctx.deferred );
				w.closes = std::move( ctx.pendingClose );
				ctx.deferred.clear();
				ctx.pendingClose.clear();
				work.push_back( std::move( w ) );
			}
		}
		for ( Work &w : work ) {
			g_pendingWork -= (int)( w.actions.size() + w.closes.size() );
			if ( g_pendingWork < 0 )
				g_pendingWork = 0;
			if ( !w.ctx->alive || !w.ctx->rain )
				continue;
			for ( auto &fn : w.actions )
				fn(); // navigate / write requested inside a COM event; they look the control up again
			for ( int id : w.closes )
				CloseControl( w.ctx->rain, id );
		}
	}

	void DiscardDeferred( Context &ctx ) {
		g_pendingWork -= (int)( ctx.pendingClose.size() + ctx.deferred.size() );
		if ( g_pendingWork < 0 )
			g_pendingWork = 0;
		ctx.pendingClose.clear();
		ctx.deferred.clear();
	}

} // namespace trident
