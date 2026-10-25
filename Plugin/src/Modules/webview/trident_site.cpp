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
 * @file trident_site.cpp
 * @brief OLE client site of the WebBrowser control (IDocHostUIHandler, IServiceProvider, IOleCommandTarget).
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	// -------------------------------------------------------------------------
	// WebBrowserSite - IOleClientSite + IDocHostUIHandler
	// FIX #1: added m_ctx member and updated constructor signature
	// -------------------------------------------------------------------------

	/**
	 * @brief DOCHOSTUIFLAG_* the control reports to MSHTML.
	 *
	 * Single source of truth for IDocHostUIHandler::GetHostInfo and for the ATL host's ambient flags (item 21).
	 */
	static DWORD ComputeDocHostFlags( const Control *ctrl ) {
		// Item 29: no "silent" flag here. 0x40000 is DOCHOSTUIFLAG_THEME, not a way to hide script errors;
		// those are suppressed by IWebBrowser2::put_Silent and by the OLECMDID_SHOWSCRIPTERROR handler below.
		(void)ctrl;
		return DOCHOSTUIFLAG_NO3DBORDER;
	}

	/**
	 * @brief Applies the host-level UI settings through the ATL host window (item 21).
	 *
	 * Our IDocHostUIHandler can only be attached to a document once it exists (ICustomDoc::SetUIHandler at
	 * NavigateComplete2), so until then MSHTML used the ATL defaults: context menu enabled, flags unset.
	 * The ATL host's ambient properties are consulted from the very first page and survive navigations, so
	 * the border flags and the context-menu policy are already right while a page is loading, which also
	 * removes the visible flicker. Called when the control is created and whenever setContextMenu() changes.
	 *
	 * @param ctrl  Control whose popup window hosts the browser.
	 */
	void ApplyAmbientHostUi( const Control &ctrl ) {
		if ( !ctrl.hwndControl || !IsWindow( ctrl.hwndControl ) )
			return;
		CComPtr<IUnknown> host;
		if ( FAILED( AtlAxGetHost( ctrl.hwndControl, &host ) ) || !host )
			return;
		CComQIPtr<IAxWinAmbientDispatch> ambient( host );
		if ( !ambient )
			return;
		ambient->put_DocHostFlags( ComputeDocHostFlags( &ctrl ) );
		ambient->put_AllowContextMenu( ctrl.contextMenu ? VARIANT_TRUE : VARIANT_FALSE );
	}

	/**
	 * @brief Implements IOleClientSite and IDocHostUIHandler for the browser.
	 *
	 * Suppresses context menus, 3D borders, scrollbars, and optionally script errors.
	 * Exposes window.external via GetExternal().
	 */
	/// CGID_DocHostCommandHandler (defined here so no extra import library is needed).
	static const GUID kCgidDocHostCommandHandler = { 0xf38bc242, 0xb950, 0x11d1, { 0x89, 0x18, 0x00, 0xc0, 0x4f, 0xc2, 0xc8, 0x36 } };

	/// OLECMDID_SHOWSCRIPTERROR in the CGID_DocHostCommandHandler group.
	static constexpr DWORD kCmdShowScriptError = 40;

	struct WebBrowserSite : IOleClientSite, IDocHostUIHandler, IServiceProvider, IOleCommandTarget {
		LONG m_refCount;
		std::shared_ptr<Context> m_ctx; ///< Fix #11: keeps the Context alive while MSHTML holds this site.
		int m_configId; ///< Fix #11: control looked up by id, never a raw Control* that may dangle.
		IUnknown *m_outer;

		WebBrowserSite( int configId, std::shared_ptr<Context> ctx, IUnknown *outer ) :
			m_refCount( 1 ),
			m_ctx( std::move( ctx ) ),
			m_configId( configId ),
			m_outer( outer ) {
		}

		/// Returns the live Control or nullptr if it (or the whole context) is gone.
		Control *GetControl() {
			if ( !m_ctx || !m_ctx->alive )
				return nullptr;
			auto it = m_ctx->controls.find( m_configId );
			return it != m_ctx->controls.end() ? it->second.get() : nullptr;
		}

		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) {
			if ( ppv == nullptr )
				return E_POINTER;

			if ( riid == IID_IUnknown || riid == IID_IOleClientSite )
				*ppv = static_cast<IOleClientSite *>( this );
			else if ( riid == IID_IDocHostUIHandler )
				*ppv = static_cast<IDocHostUIHandler *>( this );
			else if ( riid == IID_IServiceProvider )
				*ppv = static_cast<IServiceProvider *>( this );
			else if ( riid == IID_IOleCommandTarget )
				*ppv = static_cast<IOleCommandTarget *>( this );
			else if ( m_outer )
				return m_outer->QueryInterface( riid, ppv );
			else
				return E_NOINTERFACE;

			AddRef();
			return S_OK;
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

		// IOleCommandTarget - item 29: script error dialogs.
		// MSHTML asks the host (CGID_DocHostCommandHandler / OLECMDID_SHOWSCRIPTERROR) before showing the
		// "Error on page" dialog. With silent = true the answer is "handled, keep running scripts".
		STDMETHODIMP QueryStatus( const GUID *, ULONG, OLECMD[], OLECMDTEXT * ) override {
			return OLECMDERR_E_NOTSUPPORTED;
		}
		STDMETHODIMP Exec( const GUID *pguidCmdGroup, DWORD nCmdID, DWORD, VARIANTARG *, VARIANTARG *pvaOut ) override {
			if ( pguidCmdGroup && IsEqualGUID( *pguidCmdGroup, kCgidDocHostCommandHandler ) && nCmdID == kCmdShowScriptError ) {
				Control *ctrl = GetControl();
				if ( ctrl && ctrl->silent ) {
					if ( pvaOut ) {
						VariantInit( pvaOut );
						pvaOut->vt = VT_BOOL;
						pvaOut->boolVal = VARIANT_TRUE; // continue running scripts, no dialog
					}
					return S_OK;
				}
			}
			return OLECMDERR_E_NOTSUPPORTED;
		}

		// IServiceProvider - hands the SkinSecurityManager to MSHTML.
		STDMETHODIMP QueryService( REFGUID guidService, REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			Control *ctrl = GetControl();
			if ( guidService == SID_SInternetSecurityManager && riid == IID_IInternetSecurityManager && ctrl && ctrl->rain ) {

				// Item 4: the manager is ALWAYS handed out (the deny list applies to every URL);
				// the extra skin privileges are only granted when local files are allowed.
				bool allowLocal = ctrl->sanitizeFlags == SANITIZE_NONE || ( ctrl->sanitizeFlags & ALLOW_LOCAL ) != 0;
				std::wstring skinPath = allowLocal ? ctrl->rain->absPath( L"#CURRENTPATH#" ) : std::wstring();
				*ppv = CreateSkinSecurityManager( skinPath, allowLocal, ctrl->allowActiveX, ctrl->allowCredentials, ctrl->allowCookies );
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		STDMETHODIMP SaveObject() {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetMoniker( DWORD, DWORD, IMoniker ** ) {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetContainer( IOleContainer **pp ) {
			*pp = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP ShowObject() {
			return S_OK;
		}
		STDMETHODIMP OnShowWindow( BOOL ) {
			return S_OK;
		}
		STDMETHODIMP RequestNewObjectLayout() {
			return E_NOTIMPL;
		}

		STDMETHODIMP ShowContextMenu( DWORD dwID, POINT *ppt, IUnknown *pcmdtReserved, IDispatch *pdispReserved ) {
			Control *ctrl = GetControl();
			if ( !ctrl || ctrl->contextMenu )
				return S_FALSE; // let the browser show its default context menu

			// Forward right-click to the skin window so it behaves as if
			// the user clicked through the browser onto the skin itself.
			if ( ctrl->hwndParent && ppt && IsWindow( ctrl->hwndParent ) ) {
				POINT pt = *ppt; // ppt is in screen coordinates
				ScreenToClient( ctrl->hwndParent, &pt );
				LPARAM lParam = MAKELPARAM( pt.x, pt.y );
				PostMessage( ctrl->hwndParent, WM_RBUTTONDOWN, MK_RBUTTON, lParam );
				PostMessage( ctrl->hwndParent, WM_RBUTTONUP,   0,          lParam );
			}

			return S_OK; // suppress browser context menu
		}

		STDMETHODIMP GetHostInfo( DOCHOSTUIINFO *pInfo ) {
			pInfo->cbSize = sizeof( DOCHOSTUIINFO );
			pInfo->dwFlags = ComputeDocHostFlags( GetControl() );
			pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
			return S_OK;
		}

		STDMETHODIMP ShowUI( DWORD, IOleInPlaceActiveObject *, IOleCommandTarget *, IOleInPlaceFrame *, IOleInPlaceUIWindow * ) {
			return S_OK;
		}
		STDMETHODIMP HideUI() {
			return S_OK;
		}
		STDMETHODIMP UpdateUI() {
			return S_OK;
		}
		STDMETHODIMP EnableModeless( BOOL ) {
			return S_OK;
		}
		STDMETHODIMP OnDocWindowActivate( BOOL ) {
			return S_OK;
		}
		STDMETHODIMP OnFrameWindowActivate( BOOL ) {
			return S_OK;
		}
		STDMETHODIMP ResizeBorder( LPCRECT, IOleInPlaceUIWindow *, BOOL ) {
			return S_OK;
		}
		STDMETHODIMP TranslateAccelerator( LPMSG, const GUID *, DWORD ) {
			return S_FALSE;
		}

		STDMETHODIMP GetOptionKeyPath( LPOLESTR *pchKey, DWORD ) {
			*pchKey = nullptr;
			return S_FALSE;
		}

		/// Item 7: wraps MSHTML's default drop target and refuses files/URLs (see FilteringDropTarget).
		STDMETHODIMP GetDropTarget( IDropTarget *pDropTarget, IDropTarget **ppDropTarget ) {
			if ( !ppDropTarget )
				return E_POINTER;
			*ppDropTarget = CreateFilteringDropTarget( pDropTarget );
			return S_OK;
		}

		/**
		 * FIX #1: uses m_ctx directly - no g_contexts lookup needed.
		 * FIX (mutex): g_contextsMutex protects only externalDispatch creation,
		 * not a g_contexts traversal, so std::mutex is safe here with no risk of
		 * deadlock (lock is never held by the caller at this point).
		 */
		STDMETHODIMP GetExternal( IDispatch **ppDispatch ) override {
			if ( !ppDispatch )
				return E_POINTER;
			*ppDispatch = nullptr;

			// Fix #10: no g_contextsMutex here. Everything runs on the STA thread, and this
			// method can be re-entered from Cleanup() (which used to hold the same mutex) -> deadlock.
			// Fix #8: only trusted origins get window.external.
			Control *ctrl = GetControl();
			if ( !ctrl || !IsOriginTrusted( ctrl ) )
				return E_NOTIMPL;

			if ( !ctrl->externalDispatch )
				ctrl->externalDispatch = ExternalCreate( m_ctx, ctrl->configId );

			ExternalGetDispatch( ctrl->externalDispatch, ppDispatch );
			return S_OK;
		}

		STDMETHODIMP TranslateUrl( DWORD, OLECHAR *, OLECHAR **ppchURLOut ) {
			*ppchURLOut = nullptr;
			return E_NOTIMPL;
		}

		STDMETHODIMP FilterDataObject( IDataObject *, IDataObject **ppDORet ) {
			*ppDORet = nullptr;
			return E_NOTIMPL;
		}
	};

	/**
	 * @brief Installs our IDocHostUIHandler (window.external, context menu, drop filter) on the document.
	 *
	 * Item 4/7: also called from NavigateComplete2, i.e. as soon as the new document object
	 * exists and BEFORE it is parsed, not only at DocumentComplete (when inline scripts,
	 * onload handlers and drops had already run without the handler).
	 */
	void InstallUiHandler( IDispatch *docDisp, int configId, const std::shared_ptr<Context> &ctx ) {
		if ( !docDisp )
			return;
		CComQIPtr<ICustomDoc> customDoc( docDisp );
		if ( !customDoc )
			return;
		WebBrowserSite *site = new WebBrowserSite( configId, ctx, nullptr );
		customDoc->SetUIHandler( site );
		site->Release();
	}


} // namespace trident
