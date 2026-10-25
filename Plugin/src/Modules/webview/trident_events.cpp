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
 * @file trident_events.cpp
 * @brief DWebBrowserEvents2 sink, per-context event queue and the hidden dispatch window.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Single entry point for every event queued from COM callbacks.
	 *
	 * Truncates free text, enforces the event-count and byte budgets, coalesces the chatty
	 * "latest value wins" events (status text, progress) and wakes the drain only on the
	 * empty -> non-empty transition. Returns false when the event was dropped.
	 */
	bool EnqueueEvent( Context &ctx, MshtmlEvent ev ) {
		if ( ev.title.size() > kMaxEventText )
			ev.title.resize( kMaxEventText );
		if ( ev.type != EVENT_EXTERNAL && ev.data.size() > kMaxEventText )
			ev.data.resize( kMaxEventText );

		const bool coalesce = ev.type == EVENT_STATUS_TEXT_CHANGE || ev.type == EVENT_PROGRESS_CHANGE;
		const size_t cost = sizeof( MshtmlEvent ) + ( ev.title.size() + ev.name.size() + ev.data.size() ) * sizeof( wchar_t );
		bool wake = false;
		{
			std::lock_guard<std::mutex> lock( ctx.eventMutex );
			if ( coalesce && !ctx.eventBuffer.empty() && ctx.eventBuffer.back().type == ev.type && ctx.eventBuffer.back().configId == ev.configId ) {
				MshtmlEvent &last = ctx.eventBuffer.back();
				const size_t oldCost = sizeof( MshtmlEvent ) + ( last.title.size() + last.name.size() + last.data.size() ) * sizeof( wchar_t );
				ctx.queuedBytes = ctx.queuedBytes - oldCost + cost;
				last = std::move( ev );
				return true;
			}
			if ( ctx.eventBuffer.size() >= kMaxEventQueue || ctx.queuedBytes + cost > kMaxQueuedBytes )
				return false;
			ctx.queuedBytes += cost;
			ctx.eventBuffer.push( std::move( ev ) );
			wake = !ctx.hasPendingEvents;
			ctx.hasPendingEvents = true;
		}
		if ( wake && ctx.hiddenWindow )
			PostMessage( ctx.hiddenWindow, WM_TRIDENT_EVENT, 0, 0 );
		return true;
	}

	// -------------------------------------------------------------------------
	// Event drain
	// -------------------------------------------------------------------------

	/**
	 * @brief Drains the event queue for a context and invokes Lua callbacks.
	 *
	 * Called both from the hidden window's WndProc (immediate) and from
	 * ProcessMessages (fallback). The lock must NOT be held by the caller.
	 */
	int DrainEventQueue( Context *ctx, Rain *rain ) {
		if ( !ctx || !ctx->alive || !ctx->hasPendingEvents || !rain )
			return 0;
		if ( ctx->draining )
			return 0; // nested call (a callback pumped messages): the outer drain reposts for what is left

		ReentryGuard guard; // item 36: quit() from a callback is deferred until this frame is gone
		ctx->draining = true;
		struct DrainDone {
			Context *ctx;
			~DrainDone() {
				ctx->draining = false;
				if ( ctx->alive && ctx->hasPendingEvents && ctx->hiddenWindow )
					PostMessageW( ctx->hiddenWindow, WM_TRIDENT_EVENT, 0, 0 );
			}
		} drainDone{ ctx };

		std::queue<MshtmlEvent> events;
		{
			std::lock_guard<std::mutex> lock( ctx->eventMutex );
			events.swap( ctx->eventBuffer );
			ctx->hasPendingEvents = false;
			ctx->queuedBytes = 0;
		}

		int processed = 0;
		while ( !events.empty() ) {
			const MshtmlEvent &ev = events.front();
			auto ctrlIt = ctx->controls.find( ev.configId );

			if ( ctrlIt != ctx->controls.end() && ctrlIt->second->enabled && rain->L ) {
				const Control &ctrl = *ctrlIt->second;

				if ( !ctrl.callbackKey.empty() ) {
					lua_State *L = rain->L;

					lua_getfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
					if ( lua_isfunction( L, -1 ) ) {

						if ( !ctrl.browserKey.empty() )
							lua_getfield( L, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
						else
							lua_pushnil( L );

						lua_newtable( L );

						const char *typeStr = "unknown";
						switch ( ev.type ) {
						case EVENT_DOCUMENT_COMPLETE:
							typeStr = "documentcomplete";
							break;
						case EVENT_NAVIGATE_COMPLETE:
							typeStr = "navigatecomplete";
							break;
						case EVENT_TITLE_CHANGE:
							typeStr = "titlechange";
							break;
						case EVENT_STATUS_TEXT_CHANGE:
							typeStr = "statustextchange";
							break;
						case EVENT_PROGRESS_CHANGE:
							typeStr = "progresschange";
							break;
						case EVENT_DOWNLOAD_BEGIN:
							typeStr = "downloadbegin";
							break;
						case EVENT_DOWNLOAD_COMPLETE:
							typeStr = "downloadcomplete";
							break;
						case EVENT_NAVIGATE_ERROR:
							typeStr = "navigateerror";
							break;
						case EVENT_COMMAND_STATE_CHANGE:
							typeStr = "commandstatechange";
							break;
						case EVENT_FILE_DOWNLOAD:
							typeStr = "filedownload"; // informational: the download was already cancelled unless allowDownloads
							break;
						case EVENT_EXTERNAL:
							// Fix #2: the page-controlled name is NEVER used as the event type
							// (it could spoof "documentcomplete", "navigate", ...). It goes in event.name.
							typeStr = "external";
							break;
						default:
							break;
						}

						lua_pushstring( L, typeStr );
						lua_setfield( L, -2, "type" );

						if ( !ev.title.empty() ) {
							std::string title = wstring_to_utf8( ev.title );
							lua_pushlstring( L, title.c_str(), title.size() );
							lua_setfield( L, -2, "title" );
						}
						if ( !ev.name.empty() ) {
							std::string name = wstring_to_utf8( ev.name );
							lua_pushlstring( L, name.c_str(), name.size() );
							lua_setfield( L, -2, "name" );
						}
						if ( !ev.data.empty() ) {
							// Fix #1: data is ALWAYS delivered as a plain string. Nothing coming from the
							// page is compiled or executed. Decode JSON in Lua (inside pcall), e.g.
							// local ok, t = pcall( json.decode, event.data )
							std::string data = wstring_to_utf8( ev.data );
							lua_pushlstring( L, data.c_str(), data.size() );
							lua_setfield( L, -2, "data" );
						}
						if ( ev.progress != 0 || ev.progressMax != 0 ) {
							lua_pushinteger( L, ev.progress );
							lua_setfield( L, -2, "progress" );
							lua_pushinteger( L, ev.progressMax );
							lua_setfield( L, -2, "progressMax" );
						}

						if ( ev.statusCode != 0 ) {
							lua_pushinteger( L, ev.statusCode );
							// commandstatechange uses "command", navigateerror uses "statusCode"
							lua_setfield( L, -2, ev.type == EVENT_COMMAND_STATE_CHANGE ? "command" : "statusCode" );
						}

						if ( ev.type == EVENT_COMMAND_STATE_CHANGE ) {
							lua_pushboolean( L, ev.cancel ? 1 : 0 ); // "enabled" no commandstatechange
							lua_setfield( L, -2, "enabled" );
						}

						lua_pushnumber( L, (lua_Number)ev.timestamp );
						lua_setfield( L, -2, "timestamp" );

						if ( lua_pcall( L, 2, 0, 0 ) != LUA_OK ) {
							// Item 27: the error value may not be a string (lua_tostring would return NULL).
							RN_LOG( rain, LOG_ERROR, ( L"[RainJIT:Trident] callback error: " + utf8_to_wstring( luaVariant::ToDisplayString( L, -1 ) ) ).c_str() );
							lua_pop( L, 1 );
						}
					} else {
						lua_pop( L, 1 );
					}
				}
			}

			events.pop();
			++processed;
		}
		return processed;
	}

	// -------------------------------------------------------------------------
	// Hidden window
	// -------------------------------------------------------------------------

	static LRESULT CALLBACK TridentWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
		if ( uMsg == WM_TRIDENT_EVENT ) {
			Context *ctx = (Context *)GetWindowLongPtrW( hWnd, GWLP_USERDATA );
			if ( ctx && ctx->rain )
				DrainEventQueue( ctx, ctx->rain );
			return 0;
		}

		if ( uMsg == WM_TRIDENT_FLUSH ) {
			FlushDeferred(); // no-op while a guarded frame is still on the stack
			return 0;
		}

		if ( uMsg == WM_CREATE ) {
			CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
			if ( cs )
				SetWindowLongPtrW( hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
			return 0;
		}

		return DefWindowProcW( hWnd, uMsg, wParam, lParam );
	}

	// -------------------------------------------------------------------------
	// Hidden window class (item 35)
	//
	// The class belongs to THIS module (not to the host executable): its window procedure lives in this
	// DLL, so registering it with the exe's HINSTANCE left a class with a dangling procedure behind when
	// the plugin was unloaded and loaded again. It is unregistered when the last context is cleaned up.
	// -------------------------------------------------------------------------

	static const wchar_t kHiddenClassName[] = L"RainJIT_Trident_Hidden";
	static bool g_hiddenClassRegistered = false;

	/// HINSTANCE of the module that contains this code (the plugin DLL), not of the host process.
	static HINSTANCE ModuleInstance() {
		HMODULE module = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &TridentWndProc ), &module );
		return module;
	}

	static bool RegisterHiddenWindowClass() {
		if ( g_hiddenClassRegistered )
			return true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof( WNDCLASSEXW );
		wc.lpfnWndProc = TridentWndProc;
		wc.hInstance = ModuleInstance();
		wc.lpszClassName = kHiddenClassName;
		g_hiddenClassRegistered = RegisterClassExW( &wc ) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
		return g_hiddenClassRegistered;
	}

	void UnregisterHiddenWindowClass() {
		if ( !g_hiddenClassRegistered )
			return;
		if ( UnregisterClassW( kHiddenClassName, ModuleInstance() ) )
			g_hiddenClassRegistered = false;
	}

	HWND CreateHiddenWindow( Context *ctx ) {
		if ( !RegisterHiddenWindowClass() )
			return nullptr;
		// clang-format off
		return CreateWindowExW(
			0,
			kHiddenClassName,
			nullptr,
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			nullptr,
			ModuleInstance(),
			ctx
		);
		// clang-format on
	}

	/// Is `v` (the pDisp argument of a DWebBrowserEvents2 event) the control's top-level browser?
	static bool IsTopLevelDisp( const Control &ctrl, const VARIANT &v ) {
		const VARIANT *p = &v;
		if ( p->vt == ( VT_BYREF | VT_VARIANT ) && p->pvarVal )
			p = p->pvarVal;
		if ( p->vt != VT_DISPATCH || !p->pdispVal || !ctrl.webBrowser )
			return false;
		return SameObject( p->pdispVal, ctrl.webBrowser );
	}

	// -------------------------------------------------------------------------
	// EventSink - DWebBrowserEvents2
	// -------------------------------------------------------------------------

	/**
	 * @brief Implements IDispatch to receive DWebBrowserEvents2.
	 *
	 * Buffers events for later processing on the main thread. Also injects
	 * `background-color:transparent` into the document when transparency is enabled.
	 */
	struct EventSink : IDispatch {
		LONG m_refCount;
		std::shared_ptr<Context> m_ctx; ///< Fix #11: keeps the Context alive while MSHTML holds this sink.
		int m_configId;

		EventSink( std::shared_ptr<Context> ctx, int configId ) :
			m_refCount( 1 ),
			m_ctx( std::move( ctx ) ),
			m_configId( configId ) {
		}

		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) {
			if ( riid == IID_IUnknown || riid == IID_IDispatch ) {
				*ppv = this;
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		STDMETHODIMP_( ULONG ) AddRef() {
			return InterlockedIncrement( &m_refCount );
		}
		STDMETHODIMP_( ULONG ) Release() {
			LONG ref = InterlockedDecrement( &m_refCount );
			if ( ref == 0 )
				delete this;
			return ref;
		}

		STDMETHODIMP GetTypeInfoCount( UINT * ) {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo ** ) {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *, UINT, LCID, DISPID * ) {
			return E_NOTIMPL;
		}

		/**
		 * @brief Asks the Lua callback whether a page popup may be opened (item 33).
		 *
		 * The callback receives `{ type = "newwindow", url = <target or ""> }` and returns true to allow.
		 * No callback / error / any other value = refuse.
		 */
		bool AskNewWindow( const std::wstring &target ) {
			auto ctrlIt = m_ctx->controls.find( m_configId );
			if ( ctrlIt == m_ctx->controls.end() )
				return false;
			Control &ctrl = *ctrlIt->second;
			if ( !ctrl.enabled || ctrl.callbackKey.empty() || !m_ctx->rain->L )
				return false;

			bool allow = false;
			lua_State *LS = m_ctx->rain->L;
			lua_getfield( LS, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
			if ( lua_isfunction( LS, -1 ) ) {
				if ( !ctrl.browserKey.empty() )
					lua_getfield( LS, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
				else
					lua_pushnil( LS );
				lua_newtable( LS );
				lua_pushstring( LS, "newwindow" );
				lua_setfield( LS, -2, "type" );
				const std::string u8 = wstring_to_utf8( target.size() > kMaxEventText ? target.substr( 0, kMaxEventText ) : target );
				lua_pushlstring( LS, u8.c_str(), u8.size() );
				lua_setfield( LS, -2, "url" );
				if ( lua_pcall( LS, 2, 1, 0 ) == LUA_OK )
					allow = ( lua_isboolean( LS, -1 ) && lua_toboolean( LS, -1 ) );
				lua_pop( LS, 1 );
			} else {
				lua_pop( LS, 1 );
			}
			return allow;
		}

		/**
		 * @brief Opens an allowed popup target in the user's default browser (item 33).
		 *
		 * Only http, https and mailto are handed to the shell; anything else (file:, javascript:, ms-*: ...)
		 * is refused and logged, so a page cannot use an allowed popup to start an arbitrary handler.
		 */
		void OpenInDefaultBrowser( const std::wstring &target ) {
			std::string lower = wstring_to_utf8( target );
			std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
			const bool safe = lower.compare( 0, 7, "http://" ) == 0 || lower.compare( 0, 8, "https://" ) == 0 || lower.compare( 0, 7, "mailto:" ) == 0;
			if ( !safe ) {
				RN_LOG( m_ctx->rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] popup not opened (only http, https and mailto): " + target ).c_str() );
				return;
			}
			ShellExecuteW( nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL );
		}

		STDMETHODIMP Invoke( DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *pDispParams, VARIANT *, EXCEPINFO *, UINT * ) {
			// Fix #11: after Cleanup() the Rain instance may be gone; ignore late events.
			if ( !m_ctx || !m_ctx->alive || !m_ctx->rain )
				return S_OK;

			// Item 36: Lua callbacks below run inside this COM event. quit() / navigate() from them must not
			// free the control (or the browser that is firing this event) until the whole frame is gone.
			ComEventGuard guard;

			auto pushEvent = [&]( EventType type, std::wstring title = {} ) {
				MshtmlEvent ev;
				ev.type = type;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.title = std::move( title );
				EnqueueEvent( *m_ctx, std::move( ev ) );
			};

			if ( dispIdMember == DISPID_DOCUMENTCOMPLETE ) {
				bool fromWrite = false;
				auto it = m_ctx->controls.find( m_configId );

				// Item 20: DocumentComplete(pDisp, URL) is raised for every frame too. Only the top-level
				// browser (pDisp == the control's IWebBrowser2) produces an event / handler install / re-subclass.
				// (Control not registered yet = create() still running: nothing to compare against, treat as top.)
				if ( it != m_ctx->controls.end() && pDispParams && pDispParams->cArgs >= 2 && !IsTopLevelDisp( *it->second, pDispParams->rgvarg[1] ) )
					return S_OK;

				if ( it != m_ctx->controls.end() ) {
					Control &w = *it->second;
					if ( w.writePending ) {
						if ( GetTickCount64() - w.writeTick > kWriteEventWindowMs )
							w.writePending = false; // stale: close() never produced an event
						else {
							w.writePending = false; // consumed: this is the event caused by write()
							fromWrite = true;
						}
					}
				}
				if ( !fromWrite )
					pushEvent( EVENT_DOCUMENT_COMPLETE );

				// Item 31: a deferred write() waits for an about:blank document (fresh, local origin).
				if ( it != m_ctx->controls.end() && it->second->hasPendingWrite && it->second->webBrowser ) {
					Control &pc = *it->second;
					if ( IsBlankLocation( pc ) ) {
						std::string html = std::move( pc.pendingHtml );
						pc.pendingHtml.clear();
						pc.hasPendingWrite = false;
						std::string werr;
						if ( !WriteIntoDocument( pc, html, werr ) ) {
							RN_LOG( m_ctx->rain, LOG_WARNING, ( L"[RainJIT:Trident] deferred write() failed: " + utf8_to_wstring( werr ) ).c_str() );
						} else {
							ResetThemeState( pc );
							ApplyThemeData( pc );
						}
					} else {
						pc.programmaticNav = true;
						CComVariant vBlank( L"about:blank" );
						pc.webBrowser->Navigate2( &vBlank, nullptr, nullptr, nullptr, nullptr );
						pc.programmaticNav = false;
					}
				}

				if ( it != m_ctx->controls.end() ) {
					Control &ctrl = *it->second;
					if ( ctrl.webBrowser ) {
						CComPtr<IDispatch> docDisp;
						if ( SUCCEEDED( ctrl.webBrowser->get_Document( &docDisp ) ) && docDisp ) {

							// FIX #1: pass m_ctx so GetExternal can use it directly
							InstallUiHandler( docDisp, ctrl.configId, m_ctx );

							// Unconditional VBScript tripwire (the engine itself is already blocked): drop the page.
							CComQIPtr<IHTMLDocument2> vbsDoc( docDisp );
							if ( vbsDoc && DocumentUsesVbScript( vbsDoc ) ) {
								RN_LOG( m_ctx->rain, LOG_WARNING, L"[RainJIT:Trident] [SECURITY] VBScript detected in the document - about:blank." );
								ctrl.programmaticNav = true;
								ctrl.webBrowser->Stop();
								CComVariant vBlank( L"about:blank" );
								ctrl.webBrowser->Navigate2( &vBlank, nullptr, nullptr, nullptr, nullptr );
								ctrl.programmaticNav = false;
							}

							// data-theme and data-* attributes on <html> (again: NavigateComplete2 may have run too early).
							ResetThemeState( ctrl );
							ApplyThemeData( ctrl );

							if ( ctrl.transparent ) {
								CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
								if ( htmlDoc ) {
									CComPtr<IHTMLElement> body;
									if ( SUCCEEDED( htmlDoc->get_body( &body ) ) && body ) {
										CComQIPtr<IHTMLStyle> style;
										body->get_style( &style );
										if ( style ) {
											// Item 15: the layered window only punches out the color key, so the body must
											// be painted with exactly that color ("transparent" would show the default white).
											CComVariant value( ColorRefToCss( ctrl.colorKey ).c_str() );
											style->put_backgroundColor( value );
										}
									}
								}
							}
						}
					}

					// Reinstall IE Server subclass after each navigation.
					// Fix #3: inside the "found" branch - the event can arrive before l_create
					// inserts the control into the map, and it->second on end() was UB (crash).
					EnumAndSubclassIEServer( &ctrl );
				}
			}

			else if ( dispIdMember == DISPID_NAVIGATECOMPLETE2 ) {
				// Items 7/20: only the top-level document is reported and handled here (frames are ignored).
				auto ctrlIt = m_ctx->controls.find( m_configId );
				const bool known = ctrlIt != m_ctx->controls.end() && pDispParams && pDispParams->cArgs >= 2;
				if ( !known || IsTopLevelDisp( *ctrlIt->second, pDispParams->rgvarg[1] ) )
					pushEvent( EVENT_NAVIGATE_COMPLETE );

				if ( known ) {
					Control &ctrl = *ctrlIt->second;
					if ( ctrl.webBrowser && IsTopLevelDisp( ctrl, pDispParams->rgvarg[1] ) ) {
						// Install the UI handler (external, drop filter, context menu) before the page is parsed.
						CComPtr<IDispatch> docDisp;
						if ( SUCCEEDED( ctrl.webBrowser->get_Document( &docDisp ) ) && docDisp ) {
							InstallUiHandler( docDisp, ctrl.configId, m_ctx );
							// As early as possible, so CSS selectors on [data-theme] / [data-*] match from the first paint.
							ResetThemeState( ctrl );
							ApplyThemeData( ctrl );
						}

						// Redirects (HTTP 3xx, meta refresh) do not fire BeforeNavigate2 again:
						// validate the FINAL url here and drop to about:blank if it is not allowed.
						VARIANT *pUrl = &pDispParams->rgvarg[0];
						if ( pUrl->vt == ( VT_BYREF | VT_VARIANT ) && pUrl->pvarVal )
							pUrl = pUrl->pvarVal;
						if ( pUrl->vt == VT_BSTR && pUrl->bstrVal && !IsNavigationAllowed( ctrl, pUrl->bstrVal ) ) {
							RN_LOG( m_ctx->rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] navigation ended on a blocked URL (redirect?) - about:blank. Blocked: " + std::wstring( pUrl->bstrVal ) ).c_str() );
							ctrl.programmaticNav = true;
							ctrl.webBrowser->Stop();
							CComVariant vBlank( L"about:blank" );
							ctrl.webBrowser->Navigate2( &vBlank, nullptr, nullptr, nullptr, nullptr );
							ctrl.programmaticNav = false;
						}
					}
				}
			}

			else if ( dispIdMember == DISPID_TITLECHANGE && pDispParams && pDispParams->cArgs >= 1 && pDispParams->rgvarg[0].vt == VT_BSTR )
				pushEvent( EVENT_TITLE_CHANGE, pDispParams->rgvarg[0].bstrVal );

			else if ( dispIdMember == DISPID_BEFORENAVIGATE2 && pDispParams && pDispParams->cArgs >= 7 ) {
				VARIANT *pUrl = &pDispParams->rgvarg[5];
				if ( pUrl->vt == ( VT_BYREF | VT_VARIANT ) && pUrl->pvarVal )
					pUrl = pUrl->pvarVal;
				if ( pUrl->vt == VT_BSTR && pUrl->bstrVal ) {
					std::wstring url = pUrl->bstrVal;
					if ( url != L"about:blank" ) {
						bool cancel = false;
						auto ctrlIt = m_ctx->controls.find( m_configId );
						if ( ctrlIt != m_ctx->controls.end() ) {
							Control &ctrl = *ctrlIt->second;

							// Item 7: navigation policy, applied to EVERY navigation (clicked links,
							// location=, forms, meta refresh, frames), not only to navigate()/create().
							const bool top = IsTopLevelDisp( ctrl, pDispParams->rgvarg[6] );
							bool policyBlocked = false;
							if ( !IsNavigationAllowed( ctrl, url ) ) {
								policyBlocked = true;
								RN_LOG( m_ctx->rain, LOG_WARNING, ( L"[RainJIT:Trident] [SECURITY] navigation blocked: " + url ).c_str() );
							} else if ( top && ctrl.navigationMode == NAV_LOCKED && !ctrl.programmaticNav ) {
								policyBlocked = true; // navigation = "locked": only navigate()/back()/forward()/refresh()
								RN_LOG( m_ctx->rain, LOG_DEBUG, ( L"[RainJIT:Trident] navigation locked, cancelled: " + url ).c_str() );
							}
							if ( top ) {
								ctrl.programmaticNav = false; // the programmatic request (if any) has been consumed
								ctrl.writePending = false;    // a real navigation supersedes a pending write() event
								ctrl.hasPendingWrite = false; // ... and a deferred write()
								ctrl.pendingHtml.clear();
							}

							if ( policyBlocked )
								cancel = true;
							else if ( top && ctrl.enabled && !ctrl.callbackKey.empty() && m_ctx->rain->L ) {
								// Item 20: the Lua "navigate" callback only sees top-level navigations
								// (iframe loads would otherwise trigger its bangs/returns). Frames still obey the policy above.
								lua_State *L = m_ctx->rain->L;
								lua_getfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
								if ( lua_isfunction( L, -1 ) ) {
									if ( !ctrl.browserKey.empty() )
										lua_getfield( L, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
									else
										lua_pushnil( L );
									lua_newtable( L );
									lua_pushstring( L, "navigate" );
									lua_setfield( L, -2, "type" );
									std::string urlUtf8 = wstring_to_utf8( url );
									lua_pushlstring( L, urlUtf8.c_str(), urlUtf8.size() );
									lua_setfield( L, -2, "data" );
									if ( lua_pcall( L, 2, 1, 0 ) == LUA_OK ) {
										cancel = ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) );
										lua_pop( L, 1 );
									} else {
										if ( m_ctx && m_ctx->rain )
											RN_LOG( m_ctx->rain, LOG_ERROR, ( L"[RainJIT:Trident] navigate callback error: " + utf8_to_wstring( luaVariant::ToDisplayString( L, -1 ) ) ).c_str() );
										lua_pop( L, 1 );
									}
								} else {
									lua_pop( L, 1 );
								}
							}
						}
						VARIANT &vCancel = pDispParams->rgvarg[0];
						if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
							*vCancel.pboolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
						else
							vCancel.boolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
					}
				}
			}

			else if ( dispIdMember == DISPID_STATUSTEXTCHANGE && pDispParams && pDispParams->cArgs >= 1 && pDispParams->rgvarg[0].vt == VT_BSTR ) {
				MshtmlEvent ev;
				ev.type = EVENT_STATUS_TEXT_CHANGE;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.data = pDispParams->rgvarg[0].bstrVal ? pDispParams->rgvarg[0].bstrVal : L"";
				EnqueueEvent( *m_ctx, std::move( ev ) );
			}

			else if ( dispIdMember == DISPID_PROGRESSCHANGE && pDispParams && pDispParams->cArgs >= 2 ) {
				MshtmlEvent ev;
				ev.type = EVENT_PROGRESS_CHANGE;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.progress = ( pDispParams->rgvarg[1].vt == VT_I4 ) ? pDispParams->rgvarg[1].lVal : 0;
				ev.progressMax = ( pDispParams->rgvarg[0].vt == VT_I4 ) ? pDispParams->rgvarg[0].lVal : 0;
				EnqueueEvent( *m_ctx, std::move( ev ) );
			}

			else if ( dispIdMember == DISPID_DOWNLOADBEGIN )
				pushEvent( EVENT_DOWNLOAD_BEGIN );

			else if ( dispIdMember == DISPID_DOWNLOADCOMPLETE )
				pushEvent( EVENT_DOWNLOAD_COMPLETE );

			else if ( dispIdMember == DISPID_NAVIGATEERROR && pDispParams && pDispParams->cArgs >= 5 ) {
				// Item 20: NavigateError(pDisp, URL, TargetFrameName, StatusCode, Cancel) - frame errors are not reported.
				{
					auto errIt = m_ctx->controls.find( m_configId );
					if ( errIt != m_ctx->controls.end() && !IsTopLevelDisp( *errIt->second, pDispParams->rgvarg[4] ) )
						return S_OK;
				}
				MshtmlEvent ev;
				ev.type = EVENT_NAVIGATE_ERROR;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				// pDispParams->rgvarg[3] = URL, [1] = statusCode
				VARIANT *pUrl = &pDispParams->rgvarg[3];
				if ( pUrl->vt == ( VT_BYREF | VT_VARIANT ) && pUrl->pvarVal )
					pUrl = pUrl->pvarVal;
				if ( pUrl->vt == VT_BSTR && pUrl->bstrVal )
					ev.data = pUrl->bstrVal;
				VARIANT *pCode = &pDispParams->rgvarg[1];
				if ( pCode->vt == ( VT_BYREF | VT_VARIANT ) && pCode->pvarVal )
					pCode = pCode->pvarVal;
				if ( pCode->vt == VT_I4 )
					ev.statusCode = pCode->lVal;
				EnqueueEvent( *m_ctx, std::move( ev ) );
			}

			else if ( dispIdMember == DISPID_FILEDOWNLOAD && pDispParams && pDispParams->cArgs >= 2 ) {
				// Item 7: FileDownload( ActiveDocument, Cancel ) - arguments arrive reversed.
				// ActiveDocument == TRUE means a document the browser can show; FALSE is a real file
				// download (save/open dialog). Downloads are cancelled unless create{ allowDownloads = true }.
				const VARIANT &vActive = pDispParams->rgvarg[1];
				bool activeDoc = ( vActive.vt == VT_BOOL && vActive.boolVal != VARIANT_FALSE ) || ( vActive.vt == ( VT_BOOL | VT_BYREF ) && vActive.pboolVal && *vActive.pboolVal != VARIANT_FALSE );
				if ( !activeDoc ) {
					bool allow = false;
					auto ctrlIt = m_ctx->controls.find( m_configId );
					if ( ctrlIt != m_ctx->controls.end() )
						allow = ctrlIt->second->allowDownloads;
					VARIANT &vCancel = pDispParams->rgvarg[0];
					if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
						*vCancel.pboolVal = allow ? VARIANT_FALSE : VARIANT_TRUE;
					if ( !allow )
						RN_LOG( m_ctx->rain, LOG_WARNING, L"[RainJIT:Trident] [SECURITY] file download blocked (create{ allowDownloads = true } to allow)." );
					pushEvent( EVENT_FILE_DOWNLOAD );
				}
			}

			else if ( dispIdMember == DISPID_NEWWINDOW3 && pDispParams && pDispParams->cArgs >= 5 ) {
				// Item 33: NewWindow3( ppDisp, Cancel, dwFlags, bstrUrlContext, bstrUrl ) - arguments arrive
				// reversed: rgvarg[4] = ppDisp, [3] = Cancel, [2] = dwFlags, [1] = bstrUrlContext, [0] = bstrUrl.
				// Unlike NewWindow2 this tells us WHERE the page wants to go.
				std::wstring target;
				const VARIANT &vUrl = pDispParams->rgvarg[0];
				if ( vUrl.vt == VT_BSTR && vUrl.bstrVal )
					target = vUrl.bstrVal;
				else if ( vUrl.vt == ( VT_BSTR | VT_BYREF ) && vUrl.pbstrVal && *vUrl.pbstrVal )
					target = *vUrl.pbstrVal;
				const bool allow = AskNewWindow( target );
				VARIANT &vCancel = pDispParams->rgvarg[3];
				if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
					*vCancel.pboolVal = VARIANT_TRUE; // never let MSHTML open its own unmanaged window
				if ( allow )
					OpenInDefaultBrowser( target );
			}

			else if ( dispIdMember == DISPID_NEWWINDOW2 && pDispParams && pDispParams->cArgs >= 2 ) {
				// NewWindow2( ppDisp, Cancel ) - only used when NewWindow3 is not raised (the URL is unknown here).
				// Arguments arrive reversed: rgvarg[0] = Cancel, rgvarg[1] = ppDisp. Without the URL nothing can
				// be opened on the caller's behalf, so the popup is cancelled; the callback still gets the event.
				AskNewWindow( std::wstring() );
				VARIANT &vCancel = pDispParams->rgvarg[0];
				if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
					*vCancel.pboolVal = VARIANT_TRUE;
			}

			else if ( dispIdMember == DISPID_WINDOWCLOSING ) {
				// Same pattern - cancelable directly
				auto ctrlIt = m_ctx->controls.find( m_configId );
				if ( ctrlIt != m_ctx->controls.end() ) {
					Control &ctrl = *ctrlIt->second;
					if ( ctrl.enabled && !ctrl.callbackKey.empty() && m_ctx->rain->L ) {
						lua_State *LS = m_ctx->rain->L;
						lua_getfield( LS, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
						if ( lua_isfunction( LS, -1 ) ) {
							if ( !ctrl.browserKey.empty() )
								lua_getfield( LS, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
							else
								lua_pushnil( LS );
							lua_newtable( LS );
							lua_pushstring( LS, "windowclosing" );
							lua_setfield( LS, -2, "type" );
							if ( lua_pcall( LS, 2, 1, 0 ) == LUA_OK ) {
								bool cancel = ( lua_isboolean( LS, -1 ) && !lua_toboolean( LS, -1 ) );
								lua_pop( LS, 1 );
								if ( pDispParams && pDispParams->cArgs >= 2 ) {
									VARIANT &vCancel = pDispParams->rgvarg[0];
									if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
										*vCancel.pboolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
								}
							} else {
								lua_pop( LS, 1 );
							}
						} else {
							lua_pop( LS, 1 );
						}
					}
				}
			}

			else if ( dispIdMember == DISPID_COMMANDSTATECHANGE && pDispParams && pDispParams->cArgs >= 2 ) {
				MshtmlEvent ev;
				ev.type = EVENT_COMMAND_STATE_CHANGE;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.statusCode = ( pDispParams->rgvarg[1].vt == VT_I4 ) ? pDispParams->rgvarg[1].lVal : 0;
				ev.cancel = ( pDispParams->rgvarg[0].vt == VT_BOOL ) && ( pDispParams->rgvarg[0].boolVal == VARIANT_TRUE );
				EnqueueEvent( *m_ctx, std::move( ev ) );
			}

			return S_OK;
		}
	};

	// -------------------------------------------------------------------------
	// Event sink connection management
	// -------------------------------------------------------------------------

	void ConnectEventSink( Control &ctrl, std::shared_ptr<Context> ctx ) {
		if ( !ctrl.webBrowser )
			return;

		CComPtr<IConnectionPointContainer> cpc;
		if ( FAILED( ctrl.webBrowser->QueryInterface( IID_IConnectionPointContainer, (void **)&cpc ) ) )
			return;

		CComPtr<IConnectionPoint> cp;
		if ( FAILED( cpc->FindConnectionPoint( DIID_DWebBrowserEvents2, &cp ) ) )
			return;

		EventSink *sink = new EventSink( std::move( ctx ), ctrl.configId );

		DWORD cookie = 0;
		if ( SUCCEEDED( cp->Advise( sink, &cookie ) ) ) {
			ctrl.eventCookie = cookie;
			ctrl.eventCP = cp.Detach();
			ctrl.eventSink = sink;
		} else {
			sink->Release();
		}
	}

	void DisconnectEventSink( Control &ctrl ) {
		if ( ctrl.eventCP && ctrl.eventSink ) {
			ctrl.eventCP->Unadvise( ctrl.eventCookie );
			ctrl.eventCP->Release();
			ctrl.eventCP = nullptr;
			ctrl.eventSink->Release();
			ctrl.eventSink = nullptr;
			ctrl.eventCookie = 0;
		}
	}


} // namespace trident
