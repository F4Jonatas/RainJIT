/**
 * @file trident.cpp
 * @brief Implementation of the Trident WebBrowser control module.
 * @license GPL v2.0 License
 *
 * @details
 * This module creates a layered child window hosting an Internet Explorer
 * WebBrowser control. It handles:
 * - Constraining the control within the parent skin (`insideSkin`).
 * - Applying padding to adjust position and size.
 * - Color-key transparency via `WS_EX_LAYERED` and CSS injection.
 * - Rounded corners using `SetWindowRgn`.
 * - Lua bindings for navigation, script execution, and content writing.
 */

#include "trident.hpp"
#include "sanitize.hpp"
#include <Includes/rain.hpp>
#include <Includes/define.hpp>
#include <utils/strings.hpp>
#include <algorithm>
#include <atlbase.h>
#include <atlhost.h>
#include <commctrl.h>
#include <docobj.h>
#include <exdisp.h>
#include <mshtmdid.h>
#include <mshtml.h>
#include <mshtmhst.h>


#pragma comment( lib, "comctl32.lib" )

/// Message posted by EventSink::Invoke to the hidden window to trigger
/// immediate callback dispatch without waiting for Rainmeter's Update tick.
#define WM_TRIDENT_EVENT ( WM_APP + 2 )




/**
 * @brief Helper to invoke a method on the document's IDispatch.
 *
 * @param docDisp  IDispatch pointer to the document.
 * @param dispid   DISPID of the method to call.
 * @param text     UTF-8 string to pass as the first argument.
 * @return true on success.
 */
static bool InvokeDocumentWrite( IDispatch *docDisp, DISPID dispid, const char *text ) {
	if ( !docDisp || !text )
		return false;

	std::wstring wtext = utf8_to_wstring( text );
	CComBSTR bstrText( wtext.c_str() );
	DISPPARAMS params = {};
	VARIANTARG varg;
	VariantInit( &varg );
	varg.vt = VT_BSTR;
	varg.bstrVal = bstrText;
	params.rgvarg = &varg;
	params.cArgs = 1;

	// clang-format off
	HRESULT hr = docDisp->Invoke(
		dispid,
		IID_NULL,
		LOCALE_USER_DEFAULT,
		DISPATCH_METHOD,
		&params,
		nullptr,
		nullptr,
		nullptr
	);
	// clang-format on

	VariantClear( &varg );
	return SUCCEEDED( hr );
}




namespace mshtml {

	/// Parent window subclass data
	struct ParentSubclassData {
		Control *ctrl; // Owning control.
		HWND hWndParent; // Handle to the parent window.
	};



	static LRESULT CALLBACK ParentSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );




	/**
	 * @brief HELPER - Applies a rounded rectangle region to the specified window.
	 *
	 * @param hWnd   Window handle.
	 * @param width  Width of the window.
	 * @param height Height of the window.
	 * @param radius Corner radius; if <= 0, does nothing.
	 */
	static void ApplyRoundedCorners( HWND hWnd, int width, int height, int radius ) {
		if ( radius <= 0 || width <= 0 || height <= 0 )
			return;

		HRGN hRgn = CreateRoundRectRgn( 0, 0, width, height, radius, radius );
		SetWindowRgn( hWnd, hRgn, TRUE );
		// The system owns the region after SetWindowRgn.
	}



	/**
	 * @brief HELPER - Calculates the final screen rectangle for the browser popup.
	 *
	 * Starts with the desired position (parent.left + ctrl->left, parent.top + ctrl->top)
	 * and the desired size (ctrl->width, ctrl->height). If insideSkin is true, the rectangle
	 * is clipped to the parent window's bounds. Then the padding values are applied.
	 *
	 * @param ctrl        Pointer to the control.
	 * @param parentRect  Parent window rectangle in screen coordinates.
	 * @param outX        Output screen X coordinate.
	 * @param outY        Output screen Y coordinate.
	 * @param outW        Output width.
	 * @param outH        Output height.
	 */
	static void GetConstrainedScreenRect( Control *ctrl, const RECT &parentRect, int &outX, int &outY, int &outW, int &outH ) {
		// Desired rectangle before constraint/padding
		int desiredX = parentRect.left + ctrl->left;
		int desiredY = parentRect.top + ctrl->top;
		int desiredW = ctrl->width;
		int desiredH = ctrl->height;

		// 1. Constrain inside parent skin if requested
		if ( ctrl->insideSkin ) {
			int skinLeft = parentRect.left;
			int skinTop = parentRect.top;
			int skinRight = parentRect.right;
			int skinBottom = parentRect.bottom;

			if ( desiredX < skinLeft )
				desiredX = skinLeft;

			if ( desiredY < skinTop )
				desiredY = skinTop;

			int maxW = skinRight - desiredX;
			int maxH = skinBottom - desiredY;

			if ( desiredW > maxW )
				desiredW = ( maxW > 0 ) ? maxW : 1;

			if ( desiredH > maxH )
				desiredH = ( maxH > 0 ) ? maxH : 1;
		}

		// 2. Apply padding (reduces position and size)
		int paddedX = desiredX + ctrl->padLeft;
		int paddedY = desiredY + ctrl->padTop;
		int paddedW = desiredW - ctrl->padWidth;
		int paddedH = desiredH - ctrl->padHeight;

		if ( paddedW < 1 )
			paddedW = 1;

		if ( paddedH < 1 )
			paddedH = 1;

		outX = paddedX;
		outY = paddedY;
		outW = paddedW;
		outH = paddedH;
	}



	/**
	 * @brief Moves and resizes the popup window based on current parent position.
	 *
	 * Called when the parent window moves or is resized.
	 *
	 * @param ctrl Pointer to the control.
	 */
	static void UpdateControlPosition( Control *ctrl ) {
		if ( !ctrl || !ctrl->hwndControl || !IsWindow( ctrl->hwndControl ) )
			return;

		if ( !ctrl->hwndParent || !IsWindow( ctrl->hwndParent ) )
			return;

		RECT parentRect;
		GetWindowRect( ctrl->hwndParent, &parentRect );

		int screenX, screenY, width, height;
		GetConstrainedScreenRect( ctrl, parentRect, screenX, screenY, width, height );

		SetWindowPos( ctrl->hwndControl, HWND_TOP, screenX, screenY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW );

		if ( ctrl->cornerRadius > 0 )
			ApplyRoundedCorners( ctrl->hwndControl, width, height, ctrl->cornerRadius );
	}



	/// Globals
	static std::unordered_map<Rain *, Context *> g_contexts;
	static std::recursive_mutex g_contextsMutex;
	static bool g_comInitialized = false;
	static bool g_comNeedsUninitialize = false;


	/**
	 * @brief Generates a unique Lua registry key for a control's callback.
	 *
	 * @param rain     Rain instance.
	 * @param configId Control identifier.
	 * @return String key suitable for LUA_REGISTRYINDEX.
	 */
	static std::string GetCallbackKey( Rain *rain, int configId ) {
		if ( !rain )
			return "";

		std::wstring skinName = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
		std::string name = wstring_to_utf8( skinName );
		std::replace( name.begin(), name.end(), '\\', '_' );
		std::replace( name.begin(), name.end(), '/', '_' );
		std::replace( name.begin(), name.end(), '.', '_' );
		return "mshtml_callback_" + name + "_" + std::to_string( configId );
	}



	// -------------------------------------------------------------------------
	// Event drain — shared between WM_TRIDENT_EVENT handler and ProcessMessages
	// -------------------------------------------------------------------------

	/**
	 * @brief Drains the event queue for a context and invokes Lua callbacks.
	 *
	 * Called both from the hidden window's WndProc (immediate, triggered by
	 * PostMessage from EventSink) and from ProcessMessages (fallback, triggered
	 * by Rainmeter's Update tick).
	 *
	 * The callback already has `self` injected into its environment via setfenv
	 * in l_create, so this function simply fires the callback with the event
	 * table — no environment manipulation needed here.
	 *
	 * @param ctx  Context whose queue should be drained.
	 * @param rain Owning Rain instance (carries the Lua state).
	 * @return Number of events processed.
	 */
	static int DrainEventQueue( Context *ctx, Rain *rain ) {
		if ( !ctx || !ctx->hasPendingEvents || !rain )
			return 0;

		std::queue<MshtmlEvent> events;
		{
			std::lock_guard<std::mutex> lock( ctx->eventMutex );
			events.swap( ctx->eventBuffer );
			ctx->hasPendingEvents = false;
		}

		int processed = 0;
		while ( !events.empty() ) {
			const MshtmlEvent &ev = events.front();
			auto ctrlIt = ctx->controls.find( ev.configId );

			if ( ctrlIt != ctx->controls.end() && ctrlIt->second.enabled && rain->L ) {
				const Control &ctrl = ctrlIt->second;

				if ( !ctrl.callbackKey.empty() ) {
					lua_State *L = rain->L;

					lua_getfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
					if ( lua_isfunction( L, -1 ) ) {

						// Push browser table as first argument: callback(browser, event)
						if ( !ctrl.browserKey.empty() )
							lua_getfield( L, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
						else
							lua_pushnil( L );

						// Build event table
						lua_newtable( L );

						const char *typeStr = "unknown";
						std::string externalName;
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
						case EVENT_EXTERNAL:
							// Use the event name supplied by JS as the type:
							// window.external.notify("meuEvento", data) → event.type == "meuEvento"
							externalName = wstring_to_utf8( ev.name );
							typeStr = externalName.empty() ? "external" : externalName.c_str();
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
							std::string data = wstring_to_utf8( ev.data );
							lua_pushlstring( L, data.c_str(), data.size() );
							lua_setfield( L, -2, "data" );
						}

						lua_pushnumber( L, (lua_Number)ev.timestamp );
						lua_setfield( L, -2, "timestamp" );

						if ( lua_pcall( L, 2, 0, 0 ) != LUA_OK ) {
							const char *err = lua_tostring( L, -1 );
							RN_LOG( rain, LOG_ERROR, ( L"Trident callback error: " + utf8_to_wstring( err ) ).c_str() );
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
	// Hidden window — WndProc, class registration, creation
	// -------------------------------------------------------------------------

	/**
	 * @brief WndProc for the per-skin hidden message-only window.
	 *
	 * Handles WM_TRIDENT_EVENT posted by EventSink::Invoke. The Context*
	 * is stored in GWLP_USERDATA at window creation time.
	 */
	static LRESULT CALLBACK TridentWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
		if ( uMsg == WM_TRIDENT_EVENT ) {
			Context *ctx = (Context *)GetWindowLongPtrW( hWnd, GWLP_USERDATA );
			if ( ctx && ctx->rain )
				DrainEventQueue( ctx, ctx->rain );
			return 0;
		}

		if ( uMsg == WM_CREATE ) {
			// Store the Context* passed as lpCreateParams
			CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
			if ( cs )
				SetWindowLongPtrW( hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
			return 0;
		}

		return DefWindowProcW( hWnd, uMsg, wParam, lParam );
	}

	static bool RegisterHiddenWindowClass() {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof( WNDCLASSEXW );
		wc.lpfnWndProc = TridentWndProc;
		wc.hInstance = GetModuleHandleW( nullptr );
		wc.lpszClassName = L"RainJIT_Trident_Hidden";
		return RegisterClassExW( &wc ) != 0;
	}

	/**
	 * @brief Creates a hidden message-only window for the given context.
	 *
	 * Passes ctx as lpCreateParams so TridentWndProc can store it in
	 * GWLP_USERDATA and access it on every WM_TRIDENT_EVENT.
	 */
	static HWND CreateHiddenWindow( Context *ctx ) {
		static bool registered = RegisterHiddenWindowClass();
		// clang-format off
		return CreateWindowExW(
			0,
			L"RainJIT_Trident_Hidden",
			nullptr,
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			nullptr,
			GetModuleHandleW( nullptr ),
			ctx  // passed as lpCreateParams → stored in GWLP_USERDATA by WM_CREATE
		);
		// clang-format on
	}



	static void ApplyParentTransparency( HWND parentWnd, bool enable, COLORREF colorKey ) {
		if ( !parentWnd )
			return;

		LONG_PTR exStyle = GetWindowLongPtrW( parentWnd, GWL_EXSTYLE );
		if ( enable ) {
			SetWindowLongPtrW( parentWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED );
			SetLayeredWindowAttributes( parentWnd, colorKey, 0, LWA_COLORKEY );
		} else
			SetWindowLongPtrW( parentWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED );
	}



	/**
	 * @brief IDispatch exposed to JavaScript as `window.external`.
	 *
	 * Allows JavaScript to send named events with data back to Lua via:
	 *   window.external.notify("eventName", "data")
	 *
	 * The call is dispatched as EVENT_EXTERNAL through the normal event queue,
	 * delivering { type="external", name="eventName", data="data" } to the callback.
	 */
	struct ExternalDispatch : IDispatch {
		LONG m_refCount;
		Context *m_ctx;
		int m_configId;

		static const DISPID DISPID_NOTIFY = 1;

		ExternalDispatch( Context *ctx, int configId ) :
			m_refCount( 1 ),
			m_ctx( ctx ),
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

		STDMETHODIMP GetTypeInfoCount( UINT *pCount ) {
			*pCount = 0;
			return S_OK;
		}
		STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo ** ) {
			return E_NOTIMPL;
		}

		STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *rgNames, UINT cNames, LCID, DISPID *rgDispId ) {
			for ( UINT i = 0; i < cNames; ++i ) {
				if ( rgNames[i] && _wcsicmp( rgNames[i], L"notify" ) == 0 )
					rgDispId[i] = DISPID_NOTIFY;
				else
					rgDispId[i] = DISPID_UNKNOWN;
			}
			return S_OK;
		}

		STDMETHODIMP Invoke( DISPID dispId, REFIID, LCID, WORD, DISPPARAMS *pParams, VARIANT *, EXCEPINFO *, UINT * ) {
			if ( dispId != DISPID_NOTIFY )
				return DISP_E_MEMBERNOTFOUND;
			if ( !pParams || pParams->cArgs < 1 )
				return DISP_E_BADPARAMCOUNT;

			// Helper: coerce any VARIANT to wstring (numbers, bools, strings all work)
			auto varToWStr = []( VARIANT &v ) -> std::wstring {
				CComVariant coerced;
				if ( SUCCEEDED( VariantChangeType( &coerced, &v, 0, VT_BSTR ) ) && coerced.bstrVal )
					return coerced.bstrVal;
				return {};
			};

			// COM passes arguments in reverse order:
			//   notify("name", "data")  →  rgvarg[0]="data", rgvarg[1]="name"
			std::wstring name, data;
			if ( pParams->cArgs >= 2 )
				name = varToWStr( pParams->rgvarg[1] );
			if ( pParams->cArgs >= 1 )
				data = varToWStr( pParams->rgvarg[0] );

			MshtmlEvent ev;
			ev.type = EVENT_EXTERNAL;
			ev.name = std::move( name );
			ev.data = std::move( data );
			ev.configId = m_configId;
			ev.timestamp = GetTickCount64();
			{
				std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
				m_ctx->eventBuffer.push( ev );
				m_ctx->hasPendingEvents = true;
			}
			if ( m_ctx->hiddenWindow )
				PostMessage( m_ctx->hiddenWindow, WM_TRIDENT_EVENT, 0, 0 );
			return S_OK;
		}
	};



	/**
	 * @brief Implements IOleClientSite and IDocHostUIHandler for the browser.
	 *
	 * Suppresses context menus, 3D borders, scrollbars, and optionally script errors.
	 */
	struct WebBrowserSite : IOleClientSite, IDocHostUIHandler {
		LONG m_refCount;
		Control *m_control;
		IUnknown *m_outer;

		WebBrowserSite( Control *ctrl, IUnknown *outer ) :
			m_refCount( 1 ),
			m_control( ctrl ),
			m_outer( outer ) {
		}

		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) {
			if ( ppv == nullptr )
				return E_POINTER;

			if ( riid == IID_IUnknown || riid == IID_IOleClientSite )
				*ppv = static_cast<IOleClientSite *>( this );

			else if ( riid == IID_IDocHostUIHandler )
				*ppv = static_cast<IDocHostUIHandler *>( this );

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

		STDMETHODIMP SaveObject() {
			return E_NOTIMPL;
		}


		STDMETHODIMP GetMoniker( DWORD, DWORD, IMoniker ** ) {
			return E_NOTIMPL;
		}


		STDMETHODIMP GetContainer( IOleContainer **ppContainer ) {
			*ppContainer = nullptr;
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

		STDMETHODIMP ShowContextMenu( DWORD, POINT *, IUnknown *, IDispatch * ) {
			return S_FALSE; // S_FALSE = let MSHTML show the default context menu
		}


		STDMETHODIMP GetHostInfo( DOCHOSTUIINFO *pInfo ) {
			pInfo->cbSize = sizeof( DOCHOSTUIINFO );
			pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER;

			if ( m_control && m_control->silent )
				pInfo->dwFlags |= DOCHOSTUIFLAG_SILENT;

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


		STDMETHODIMP GetDropTarget( IDropTarget *, IDropTarget **ppDropTarget ) {
			*ppDropTarget = nullptr;
			return E_NOTIMPL;
		}


		STDMETHODIMP GetExternal( IDispatch **ppDispatch ) {
			if ( !ppDispatch )
				return E_POINTER;
			// Find context via global map — called on the main thread, no lock needed
			auto it = g_contexts.find( m_control->rain );
			if ( it != g_contexts.end() ) {
				*ppDispatch = new ExternalDispatch( it->second, m_control->configId );
				return S_OK;
			}
			*ppDispatch = nullptr;
			return S_FALSE;
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
	 * @brief Implements IDispatch to receive DWebBrowserEvents2.
	 *
	 * Buffers events for later processing on the main thread. Also injects
	 * `background-color:transparent` into the document when transparency is enabled.
	 */
	struct EventSink : IDispatch {
		LONG m_refCount;
		Context *m_ctx;
		int m_configId;

		EventSink( Context *ctx, int configId ) :
			m_refCount( 1 ),
			m_ctx( ctx ),
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

		STDMETHODIMP Invoke( DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *pDispParams, VARIANT *, EXCEPINFO *, UINT * ) {
			auto pushEvent = [&]( EventType type, std::wstring title = {} ) {
				MshtmlEvent ev;
				ev.type = type;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.title = std::move( title );
				{
					std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
					m_ctx->eventBuffer.push( ev );
					m_ctx->hasPendingEvents = true;
				}
				// Notify the hidden window immediately so callbacks fire without
				// waiting for Rainmeter's Update tick. PostMessage is thread-safe
				// and non-blocking — safe to call from the COM STA thread.
				if ( m_ctx->hiddenWindow )
					PostMessage( m_ctx->hiddenWindow, WM_TRIDENT_EVENT, 0, 0 );
			};

			if ( dispIdMember == DISPID_DOCUMENTCOMPLETE ) {
				pushEvent( EVENT_DOCUMENT_COMPLETE );
				RN_LOG( m_ctx->rain, LOG_NOTICE, L"MSHTML: DocumentComplete" );

				auto it = m_ctx->controls.find( m_configId );
				if ( it != m_ctx->controls.end() ) {
					Control &ctrl = it->second;
					if ( ctrl.webBrowser ) {
						CComPtr<IDispatch> docDisp;
						if ( SUCCEEDED( ctrl.webBrowser->get_Document( &docDisp ) ) && docDisp ) {

							// Register WebBrowserSite via ICustomDoc::SetUIHandler so that
							// MSHTML calls GetExternal() and window.external becomes available
							// to JavaScript. This does NOT replace ATL's OLE client site.
							CComQIPtr<ICustomDoc> customDoc( docDisp );
							if ( customDoc ) {
								WebBrowserSite *site = new WebBrowserSite( &ctrl, nullptr );
								customDoc->SetUIHandler( site );
								site->Release();
							}

							// Inject transparency into the document body if enabled
							if ( ctrl.transparent ) {
								CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
								if ( htmlDoc ) {
									CComPtr<IHTMLElement> body;
									if ( SUCCEEDED( htmlDoc->get_body( &body ) ) && body ) {
										CComQIPtr<IHTMLStyle> style;
										body->get_style( &style );
										if ( style ) {
											CComVariant value( L"transparent" );
											style->put_backgroundColor( value );
										}
									}
								}
							}
						}
					}
				}
			}

			else if ( dispIdMember == DISPID_NAVIGATECOMPLETE2 )
				pushEvent( EVENT_NAVIGATE_COMPLETE );

			else if ( dispIdMember == DISPID_TITLECHANGE && pDispParams && pDispParams->cArgs >= 1 && pDispParams->rgvarg[0].vt == VT_BSTR )
				pushEvent( EVENT_TITLE_CHANGE, pDispParams->rgvarg[0].bstrVal );

			else if ( dispIdMember == DISPID_BEFORENAVIGATE2 && pDispParams && pDispParams->cArgs >= 7 ) {
				// BeforeNavigate2 args in reverse order (COM convention, 7 total):
				// rgvarg[6]=pDisp  [5]=URL  [4]=Flags  [3]=TargetFrame
				// rgvarg[2]=PostData  [1]=Headers  [0]=Cancel (VT_BYREF|VT_BOOL)
				//
				// The callback is called synchronously here (not via the event queue)
				// so we can read its return value before MSHTML reads the Cancel flag.
				//
				// Callback signature: function(browser, event) ... return false end
				//   return false  → Cancel = true  → browser does NOT navigate
				//   return true / nil / nothing → Cancel = false → browser navigates normally
				//
				// URL may be wrapped in VT_BYREF|VT_VARIANT — dereference if needed.
				VARIANT *pUrl = &pDispParams->rgvarg[5];
				if ( pUrl->vt == ( VT_BYREF | VT_VARIANT ) && pUrl->pvarVal )
					pUrl = pUrl->pvarVal;
				if ( pUrl->vt == VT_BSTR && pUrl->bstrVal ) {
					std::wstring url = pUrl->bstrVal;
					if ( url != L"about:blank" ) {
						bool cancel = false;
						auto ctrlIt = m_ctx->controls.find( m_configId );
						if ( ctrlIt != m_ctx->controls.end() ) {
							const Control &ctrl = ctrlIt->second;
							if ( ctrl.enabled && !ctrl.callbackKey.empty() && m_ctx->rain->L ) {
								lua_State *L = m_ctx->rain->L;
								lua_getfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
								if ( lua_isfunction( L, -1 ) ) {
									// arg1: browser table
									if ( !ctrl.browserKey.empty() )
										lua_getfield( L, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
									else
										lua_pushnil( L );
									// arg2: event table
									lua_newtable( L );
									lua_pushstring( L, "navigate" );
									lua_setfield( L, -2, "type" );
									std::string urlUtf8 = wstring_to_utf8( url );
									lua_pushlstring( L, urlUtf8.c_str(), urlUtf8.size() );
									lua_setfield( L, -2, "data" );
									// call callback(browser, event) — 2 args, 1 return value
									if ( lua_pcall( L, 2, 1, 0 ) == LUA_OK ) {
										// cancel only if Lua explicitly returned false
										cancel = ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) );
										lua_pop( L, 1 );
									} else {
										const char *err = lua_tostring( L, -1 );
										RN_LOG( m_ctx->rain, LOG_ERROR, ( L"Trident navigate callback error: " + utf8_to_wstring( err ) ).c_str() );
										lua_pop( L, 1 );
									}
								} else {
									lua_pop( L, 1 );
								}
							}
						}
						// Set the Cancel flag based on Lua return value.
						// Cancel is passed as VT_BYREF|VT_BOOL — must dereference pboolVal.
						VARIANT &vCancel = pDispParams->rgvarg[0];
						if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
							*vCancel.pboolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
						else
							vCancel.boolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
					}
				}
			}

			return S_OK;
		}
	};



	// Parent window subclass procedure
	static LRESULT CALLBACK ParentSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData ) {
		ParentSubclassData *data = (ParentSubclassData *)dwRefData;
		if ( !data || !data->ctrl )
			return DefSubclassProc( hWnd, uMsg, wParam, lParam );

		Control *ctrl = data->ctrl;

		switch ( uMsg ) {
		case WM_WINDOWPOSCHANGED:
		case WM_MOVE:
		case WM_SIZE:
			if ( !ctrl->hidden )
				UpdateControlPosition( ctrl );
			break;

		case WM_SHOWWINDOW:
			if ( ctrl->hwndControl && IsWindow( ctrl->hwndControl ) ) {
				if ( wParam )
					UpdateControlPosition( ctrl );
				else
					ShowWindow( ctrl->hwndControl, SW_HIDE );
			}
			break;

		case WM_ACTIVATE:
			if ( LOWORD( wParam ) != WA_INACTIVE ) {
				if ( ctrl->hwndControl && IsWindow( ctrl->hwndControl ) )
					SetWindowPos( ctrl->hwndControl, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
			}
			break;

		case WM_NCDESTROY:
			RemoveWindowSubclass( hWnd, ParentSubclassProc, uIdSubclass );
			delete data;
			break;
		}

		return DefSubclassProc( hWnd, uMsg, wParam, lParam );
	}



	// Event sink connection management
	static void ConnectEventSink( Control &ctrl, Context *ctx ) {
		if ( !ctrl.webBrowser )
			return;

		CComPtr<IConnectionPointContainer> cpc;
		if ( FAILED( ctrl.webBrowser->QueryInterface( IID_IConnectionPointContainer, (void **)&cpc ) ) )
			return;

		CComPtr<IConnectionPoint> cp;
		if ( FAILED( cpc->FindConnectionPoint( DIID_DWebBrowserEvents2, &cp ) ) )
			return;

		EventSink *sink = new EventSink( ctx, ctrl.configId ); // refcount = 1

		DWORD cookie = 0;
		if ( SUCCEEDED( cp->Advise( sink, &cookie ) ) ) {
			ctrl.eventCookie = cookie;
			ctrl.eventCP = cp.Detach();
			ctrl.eventSink = sink; // nossa referência continua com refcount = 1; Advise adiciona a dele
		} else {
			sink->Release(); // refcount volta a 0 → deleta o objeto
		}
	}



	static void DisconnectEventSink( Control &ctrl ) {
		if ( ctrl.eventCP && ctrl.eventSink ) {
			ctrl.eventCP->Unadvise( ctrl.eventCookie );
			ctrl.eventCP->Release();
			ctrl.eventCP = nullptr;
			ctrl.eventSink->Release();
			ctrl.eventSink = nullptr;
			ctrl.eventCookie = 0;
		}
	}



	/**
	 * @brief Creates the popup window hosting the Shell.Explorer control.
	 *
	 * Initializes ATL, creates the window, obtains IWebBrowser2, sets up transparency
	 * and rounded corners, and installs parent subclassing.
	 *
	 * @param ctrl      Control to populate with COM pointers and window handles.
	 * @param rain      Owning Rain instance.
	 * @param outError  Receives error description on failure.
	 * @return true on success, false otherwise.
	 */
	static bool CreateWebBrowserControl( Control &ctrl, Rain *rain, std::string &outError ) {
		if ( !IsWindow( ctrl.hwndParent ) ) {
			outError = "Invalid parent window handle";
			RN_LOG( rain, LOG_ERROR, L"MSHTML: Invalid hwndParent." );
			return false;
		}

		static bool atlInitTried = false;
		if ( !atlInitTried ) {
			HMODULE hAtl = LoadLibraryW( L"atl.dll" );

			if ( hAtl ) {
				typedef BOOL( WINAPI * AtlAxWinInitProc )();
				auto pInit = (AtlAxWinInitProc)GetProcAddress( hAtl, "AtlAxWinInit" );
				if ( pInit )
					pInit();
			} else {
				AtlAxWinInit();
			}

			atlInitTried = true;
		}


		// Calculate final position/size after constraint and padding
		RECT parentRect;
		GetWindowRect( ctrl.hwndParent, &parentRect );
		int screenX, screenY, finalW, finalH;
		GetConstrainedScreenRect( &ctrl, parentRect, screenX, screenY, finalW, finalH );

		DWORD dwStyle = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
		DWORD dwExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;


		// clang-format off
		HWND hwndPopup = CreateWindowExW(
			dwExStyle,
			L"AtlAxWin",
			L"Shell.Explorer",
			dwStyle,
			screenX,
			screenY,
			finalW,
			finalH,
			nullptr,
			nullptr,
			GetModuleHandleW( nullptr ),
			nullptr
		);
		// clang-format on

		if ( !hwndPopup ) {
			DWORD err = GetLastError();
			wchar_t buf[128];
			swprintf_s( buf, L"CreateWindowEx failed. GetLastError=%lu", err );
			outError = wstring_to_utf8( buf );
			RN_LOG( rain, LOG_ERROR, buf );
			return false;
		}

		ctrl.hwndControl = hwndPopup;

		// Apply rounded corners if requested
		if ( ctrl.cornerRadius > 0 )
			ApplyRoundedCorners( hwndPopup, finalW, finalH, ctrl.cornerRadius );

		CComPtr<IUnknown> unk;
		HRESULT hr = AtlAxGetControl( hwndPopup, &unk );
		if ( FAILED( hr ) || !unk ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "AtlAxGetControl failed";
			RN_LOG( rain, LOG_ERROR, L"MSHTML: AtlAxGetControl failed." );
			return false;
		}

		hr = unk->QueryInterface( IID_IWebBrowser2, (void **)&ctrl.webBrowser );
		if ( FAILED( hr ) || !ctrl.webBrowser ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "QueryInterface(IWebBrowser2) failed";
			RN_LOG( rain, LOG_ERROR, L"MSHTML: QueryInterface(IWebBrowser2) failed." );
			return false;
		}

		// Enable transparency via layered window and color key.
		// Note: put_AllowTransparency is not available on IWebBrowser2;
		// actual transparency is achieved by injecting background:transparent
		// into the document on DocumentComplete.
		if ( ctrl.transparent ) {
			LONG_PTR exStyle = GetWindowLongPtr( hwndPopup, GWL_EXSTYLE );
			SetWindowLongPtr( hwndPopup, GWL_EXSTYLE, exStyle | WS_EX_LAYERED );
			SetLayeredWindowAttributes( hwndPopup, ctrl.colorKey, 0, LWA_COLORKEY );
			SetWindowPos( hwndPopup, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
		}

		if ( ctrl.silent ) {
			CComVariant silent( true );
			ctrl.webBrowser->put_Silent( silent.boolVal );
		}

		ShowWindow( hwndPopup, ctrl.hidden ? SW_HIDE : SW_SHOW );
		UpdateWindow( hwndPopup );

		// Install parent subclassing
		ParentSubclassData *subData = new ParentSubclassData{};
		subData->ctrl = &ctrl;
		subData->hWndParent = ctrl.hwndParent;

		UINT_PTR subclassId = (UINT_PTR)ctrl.configId;

		if ( !SetWindowSubclass( ctrl.hwndParent, ParentSubclassProc, subclassId, (DWORD_PTR)subData ) ) {
			delete subData;
			ctrl.subclassData = nullptr;
			RN_LOG( rain, LOG_WARNING, L"MSHTML: SetWindowSubclass failed." );
		} else {
			ctrl.subclassData = subData;
			RN_LOG( rain, LOG_NOTICE, L"MSHTML: Parent subclass installed." );
		}

		{
			RECT rc;
			GetWindowRect( hwndPopup, &rc );
			// clang-format off
			wchar_t buf[256];
			swprintf_s(
				buf,
				L"MSHTML: Popup created. Rect=(%d,%d,%d,%d) insideSkin=%s padding=(%d,%d,%d,%d) radius=%d",
				rc.left,
				rc.top,
				rc.right,
				rc.bottom,
				ctrl.insideSkin ? L"YES" : L"NO",
				ctrl.padLeft,
				ctrl.padTop,
				ctrl.padWidth,
				ctrl.padHeight,
				ctrl.cornerRadius
			);
			// clang-format on

			RN_LOG( rain, LOG_NOTICE, buf );
		}

		return true;
	}

	// -------------------------------------------------------------------------
	// Public API
	// -------------------------------------------------------------------------
	int ProcessMessages( Rain *rain ) {
		std::lock_guard<std::recursive_mutex> lock( g_contextsMutex );
		auto it = g_contexts.find( rain );
		if ( it == g_contexts.end() )
			return 0;
		// Fallback path — WM_TRIDENT_EVENT from the hidden window already drains
		// the queue immediately on each browser event. This is called from
		// Rainmeter's Update cycle as a safety net in case a message is missed.
		return DrainEventQueue( it->second, rain );
	}

	void Cleanup( Rain *rain ) {
		std::lock_guard<std::recursive_mutex> lock( g_contextsMutex );
		auto it = g_contexts.find( rain );
		if ( it == g_contexts.end() )
			return;
		Context *ctx = it->second;

		for ( auto &pair : ctx->controls ) {
			Control &ctrl = pair.second;
			DisconnectEventSink( ctrl );

			if ( ctrl.subclassData ) {
				ParentSubclassData *data = (ParentSubclassData *)ctrl.subclassData;
				RemoveWindowSubclass( data->hWndParent, ParentSubclassProc, (UINT_PTR)ctrl.configId );
				delete data;
				ctrl.subclassData = nullptr;
			}
			if ( ctrl.webBrowser ) {
				ctrl.webBrowser->Stop();
				ctrl.webBrowser->Release();
			}
			if ( ctrl.oleObject ) {
				ctrl.oleObject->Close( OLECLOSE_NOSAVE );
				ctrl.oleObject->Release();
			}
			if ( ctrl.hwndControl )
				DestroyWindow( ctrl.hwndControl );

			if ( rain->L && !ctrl.callbackKey.empty() ) {
				lua_pushnil( rain->L );
				lua_setfield( rain->L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
			}
			if ( rain->L && !ctrl.browserKey.empty() ) {
				lua_pushnil( rain->L );
				lua_setfield( rain->L, LUA_REGISTRYINDEX, ctrl.browserKey.c_str() );
			}
		}
		ctx->controls.clear();
		if ( ctx->hiddenWindow )
			DestroyWindow( ctx->hiddenWindow );
		delete ctx;
		g_contexts.erase( it );

		if ( g_contexts.empty() && g_comInitialized && g_comNeedsUninitialize ) {
			CoUninitialize();
			g_comInitialized = false;
			g_comNeedsUninitialize = false;
		}
	}

	// -------------------------------------------------------------------------
	// Lua bindings (methods attached to the browser object)
	// -------------------------------------------------------------------------

	/**
	 * @brief Converts a control's SanitizeFlags bitmask into a sanitize::Options struct.
	 *
	 * This is the single point of translation between the trident flag model
	 * (default=block-all, allow_* to loosen) and the sanitize.hpp Options struct
	 * (each field independently controllable).
	 */
	static sanitize::Options FlagsToOptions( uint32_t flags ) {
		sanitize::Options opts;
		opts.blockScripts = ( flags & BLOCK_SCRIPTS ) != 0;
		opts.blockEvents = ( flags & BLOCK_EVENTS ) != 0;
		opts.blockStyle = ( flags & BLOCK_STYLE ) != 0;
		opts.filterCss = ( flags & FILTER_CSS ) != 0;
		opts.validateUrls = ( flags & VALIDATE_URLS ) != 0;
		opts.allowLocal = ( flags & ALLOW_LOCAL ) != 0;
		return opts;
	}

	/**
	 * @brief Navigates the browser to a URL.
	 *
	 * Lua signature: browser:navigate(url)
	 */
	static int l_navigate( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		const char *url = luaL_checkstring( L, 2 );

		if ( ctrl->sanitizeFlags & VALIDATE_URLS ) {
			bool allowLocal = ( ctrl->sanitizeFlags & ALLOW_LOCAL ) != 0;
			if ( !sanitize::IsUrlSafe( url, allowLocal ) ) {
				RN_LOG( ctrl->rain, LOG_WARNING, ( L"Trident: [SECURITY] navigate() blocked unsafe URL: " + utf8_to_wstring( url ) ).c_str() );
				return 0;
			}
		}

		std::wstring wurl = utf8_to_wstring( url );
		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( 0x04 );
		ctrl->webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );
		RN_LOG( ctrl->rain, LOG_NOTICE, ( L"Trident: navigate -> " + wurl ).c_str() );
		return 0;
	}

	/**
	 * @brief Navigates back in the history.
	 *
	 * Lua signature: browser:back()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_back( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->GoBack();
		return 0;
	}

	/**
	 * @brief Navigates forward in the history.
	 *
	 * Lua signature: browser:forward()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_forward( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->GoForward();
		return 0;
	}

	/**
	 * @brief Refreshes the current page.
	 *
	 * Lua signature: browser:refresh()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_refresh( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->Refresh();
		return 0;
	}

	/**
	 * @brief Stops any ongoing navigation or download.
	 *
	 * Lua signature: browser:stop()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_stop( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->Stop();
		return 0;
	}

	/**
	 * @brief Writes HTML content into the document.
	 *
	 * Note: The document must be open. It is recommended to call this from
	 * the "documentcomplete" callback after navigating to "about:blank".
	 *
	 * Lua signature: browser:write(html)
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_write( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		const char *html = luaL_checkstring( L, 2 );

		std::string content = html;

		if ( ctrl->sanitizeFlags & ( BLOCK_SCRIPTS | BLOCK_EVENTS | FILTER_CSS | BLOCK_STYLE | VALIDATE_URLS ) ) {
			sanitize::Options opts = FlagsToOptions( ctrl->sanitizeFlags );
			content = sanitize::HtmlFragment( html, opts );
			if ( content != html )
				RN_LOG( ctrl->rain, LOG_WARNING,
								L"Trident: [SECURITY] write() content was modified — "
								L"dangerous tags, event attributes, or blocked CSS were removed." );
		}

		CComPtr<IDispatch> docDisp;
		if ( SUCCEEDED( ctrl->webBrowser->get_Document( &docDisp ) ) && docDisp ) {
			InvokeDocumentWrite( docDisp, DISPID_IHTMLDOCUMENT2_WRITE, content.c_str() );
		}
		return 0;
	}

	/**
	 * @brief Writes a line of HTML content (appends a newline).
	 *
	 * Lua signature: browser:writeline(html)
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_writeline( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		const char *html = luaL_checkstring( L, 2 );

		std::string content = html;

		if ( ctrl->sanitizeFlags & ( BLOCK_SCRIPTS | BLOCK_EVENTS | FILTER_CSS | BLOCK_STYLE | VALIDATE_URLS ) ) {
			sanitize::Options opts = FlagsToOptions( ctrl->sanitizeFlags );
			content = sanitize::HtmlFragment( html, opts );
			if ( content != html )
				RN_LOG( ctrl->rain, LOG_WARNING,
								L"Trident: [SECURITY] writeline() content was modified — "
								L"dangerous tags, event attributes, or blocked CSS were removed." );
		}

		CComPtr<IDispatch> docDisp;
		if ( SUCCEEDED( ctrl->webBrowser->get_Document( &docDisp ) ) && docDisp ) {
			InvokeDocumentWrite( docDisp, DISPID_IHTMLDOCUMENT2_WRITELN, content.c_str() );
		}
		return 0;
	}

	/**
	 * @brief Enables or disables color-key transparency.
	 *
	 * Lua signature: browser:setTransparent(enable [, colorKey])
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_setTransparent( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl )
			return 0;
		bool enable = lua_toboolean( L, 2 );
		COLORREF color = ctrl->colorKey;
		if ( lua_isnumber( L, 3 ) ) {
			int rgb = (int)lua_tointeger( L, 3 );
			color = RGB( ( rgb >> 16 ) & 0xFF, ( rgb >> 8 ) & 0xFF, rgb & 0xFF );
		}
		ctrl->transparent = enable;
		ctrl->colorKey = color;
		ApplyParentTransparency( ctrl->hwndParent, enable, color );
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
	 * @brief Executes a JavaScript string in the context of the current document.
	 *
	 * Lua signature: browser:execScript(script)
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_execScript( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		const char *script = luaL_checkstring( L, 2 );
		CComPtr<IDispatch> docDisp;
		if ( SUCCEEDED( ctrl->webBrowser->get_Document( &docDisp ) ) && docDisp ) {
			CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
			if ( htmlDoc ) {
				CComPtr<IHTMLWindow2> window;
				if ( SUCCEEDED( htmlDoc->get_parentWindow( &window ) ) && window ) {
					// Wrap in a strict IIFE to isolate scope and avoid global leaks.
					// The leading ';' prevents errors if prior code lacks a semicolon.
					std::string wrapped = ";(function() { 'use strict';\n";
					wrapped += script;
					wrapped += "\n})();";
					std::wstring wscript = utf8_to_wstring( wrapped );
					CComBSTR bstrScript( wscript.c_str() );
					CComVariant ret;
					window->execScript( bstrScript, CComBSTR( L"JavaScript" ), &ret );
				}
			}
		}
		return 0;
	}

	/**
	 * @brief Returns the current location URL.
	 *
	 * Lua signature: url = browser:getURL()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 1 (string or nil)
	 */
	static int l_getURL( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
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

	/**
	 * @brief Forces the control to recalculate and apply its position/size.
	 *
	 * Useful after changing the skin's position or dimensions programmatically.
	 *
	 * Lua signature: browser:reposition()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_reposition( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->hwndControl || !ctrl->hwndParent )
			return 0;
		UpdateControlPosition( ctrl );
		return 0;
	}

	/**
	 * @brief Shows the browser window (reverses hide = true or browser:hide()).
	 * Lua signature: browser:show()
	 */
	static int l_show( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = false;
		UpdateControlPosition( ctrl );
		return 0;
	}

	/**
	 * @brief Hides the browser window without destroying it.
	 * Lua signature: browser:hide()
	 */
	static int l_hide( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = true;
		ShowWindow( ctrl->hwndControl, SW_HIDE );
		return 0;
	}

	/**
	 * @brief Destroys the browser control and releases all resources.
	 *
	 * Lua signature: browser:quit()
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_quit( lua_State *L ) {
		Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !ctrl )
			return 0;
		Rain *rain = ctrl->rain;
		std::string callbackKey = ctrl->callbackKey;
		int configId = ctrl->configId;

		DisconnectEventSink( *ctrl );

		if ( ctrl->subclassData ) {
			ParentSubclassData *data = (ParentSubclassData *)ctrl->subclassData;
			RemoveWindowSubclass( data->hWndParent, ParentSubclassProc, (UINT_PTR)configId );
			delete data;
			ctrl->subclassData = nullptr;
		}
		if ( ctrl->webBrowser ) {
			ctrl->webBrowser->Stop();
			ctrl->webBrowser->Release();
			ctrl->webBrowser = nullptr;
		}
		if ( ctrl->oleObject ) {
			ctrl->oleObject->Close( OLECLOSE_NOSAVE );
			ctrl->oleObject->Release();
			ctrl->oleObject = nullptr;
		}
		if ( ctrl->hwndControl ) {
			DestroyWindow( ctrl->hwndControl );
			ctrl->hwndControl = nullptr;
		}

		{
			std::lock_guard<std::recursive_mutex> lock( g_contextsMutex );
			auto it = g_contexts.find( rain );
			if ( it != g_contexts.end() )
				it->second->controls.erase( configId );
		}
		if ( rain && rain->L && !callbackKey.empty() ) {
			lua_pushnil( rain->L );
			lua_setfield( rain->L, LUA_REGISTRYINDEX, callbackKey.c_str() );
		}
		if ( rain && rain->L && !ctrl->browserKey.empty() ) {
			lua_pushnil( rain->L );
			lua_setfield( rain->L, LUA_REGISTRYINDEX, ctrl->browserKey.c_str() );
		}
		return 0;
	}

	/**
	 * @brief Lua __gc metamethod for automatic cleanup.
	 *
	 * @param L Lua state (upvalue 1: Control*)
	 * @return 0
	 */
	static int l_gc( lua_State *L ) {
		return l_quit( L );
	}

	// -------------------------------------------------------------------------
	// l_create — constructs a browser control from a Lua table
	// -------------------------------------------------------------------------
	/**
	 * @brief Creates a new browser control.
	 *
	 * Lua signature: trident.create(table)
	 *
	 * Expected table fields:
	 * - url (string): Initial URL (optional)
	 * - width, height, left, top (numbers): Position and size
	 * - transparent (boolean): Enable color-key transparency
	 * - colorKey (number): RGB color for transparency key
	 * - silent (boolean): Suppress script errors
	 * - insideSkin (boolean): Constrain to parent bounds (default true)
	 * - padding (table): {left, top, width, height} reductions
	 * - cornerRadius (number): Radius for rounded corners
	 * - callback (function): Event handler
	 *
	 * @param L Lua state (upvalue 1: Rain*)
	 * @return 1 (browser object) or nil, error
	 */
	static int l_create( lua_State *L ) {
		Rain *rain = (Rain *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !rain || !rain->hwnd ) {
			lua_pushnil( L );
			lua_pushstring( L, "Invalid Rain instance" );
			return 2;
		}

		if ( !g_comInitialized ) {
			HRESULT hr = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
			if ( hr == S_OK || hr == S_FALSE ) {
				g_comInitialized = true;
				g_comNeedsUninitialize = true;
			} else if ( hr == RPC_E_CHANGED_MODE ) {
				g_comInitialized = true;
				g_comNeedsUninitialize = false;
				RN_LOG( rain, LOG_WARNING, L"MSHTML: COM already initialized with incompatible threading model. Stability not guaranteed." );
			} else {
				lua_pushnil( L );
				lua_pushstring( L, "COM initialization failed" );
				return 2;
			}
		}

		AtlAxWinInit();

		std::lock_guard<std::recursive_mutex> lock( g_contextsMutex );
		Context *ctx = nullptr;
		auto it = g_contexts.find( rain );
		if ( it == g_contexts.end() ) {
			ctx = new Context{};
			ctx->rain = rain;
			ctx->nextId = 1;
			ctx->hasPendingEvents = false;
			ctx->hiddenWindow = CreateHiddenWindow( ctx );
			g_contexts[rain] = ctx;
		} else {
			ctx = it->second;
		}

		luaL_checktype( L, 1, LUA_TTABLE );
		Control ctrl = {};
		ctrl.configId = ctx->nextId++;
		ctrl.rain = rain;
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
		ctrl.sanitizeFlags = SANITIZE_ALL; // default: sanitize everything
		ctrl.hidden = false; // default: visible

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
			ctrl.colorKey = (COLORREF)lua_tointeger( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 1, "silent" );
		ctrl.silent = lua_toboolean( L, -1 );
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

		// --- Parse `hide` option ---
		lua_getfield( L, 1, "hide" );
		if ( !lua_isnil( L, -1 ) )
			ctrl.hidden = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// --- Parse `sanitize` option ---
		// true (default) → SANITIZE_ALL  (block everything)
		// false          → SANITIZE_NONE (block nothing)
		// table          → start from SANITIZE_ALL, clear flags via allow_* tokens:
		//   "allow_scripts" → clear BLOCK_SCRIPTS
		//   "allow_events"  → clear BLOCK_EVENTS
		//   "allow_style"   → clear FILTER_CSS + BLOCK_STYLE (pass style through untouched)
		//   "allow_css"     → clear FILTER_CSS only (style present but unfiltered)
		//   "allow_urls"    → clear VALIDATE_URLS
		//   "allow_local"   → set   ALLOW_LOCAL  (file:// permitted)
		lua_getfield( L, 1, "sanitize" );
		if ( lua_isboolean( L, -1 ) ) {
			ctrl.sanitizeFlags = lua_toboolean( L, -1 ) ? SANITIZE_ALL : SANITIZE_NONE;
		} else if ( lua_istable( L, -1 ) ) {
			ctrl.sanitizeFlags = SANITIZE_ALL; // start strict, loosen via allow_*
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
				}
				lua_pop( L, 1 );
			}
		}
		// nil → keep default SANITIZE_ALL
		lua_pop( L, 1 );

		{
			const wchar_t *lvl = ctrl.sanitizeFlags == SANITIZE_ALL ? L"ALL" : ctrl.sanitizeFlags == SANITIZE_NONE ? L"NONE" : L"CUSTOM";
			wchar_t buf[128];
			swprintf_s( buf, L"Trident: sanitize=%s (flags=0x%X)", lvl, ctrl.sanitizeFlags );
			RN_LOG( rain, LOG_NOTICE, buf );
		}

		lua_getfield( L, 1, "callback" );
		if ( lua_isfunction( L, -1 ) ) {
			// Callback fornecido: armazenar no registro
			ctrl.callbackKey = GetCallbackKey( rain, ctrl.configId );
			lua_pushvalue( L, -1 );
			lua_setfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
		} else {
			// Nenhum callback: deixar callbackKey vazio
			ctrl.callbackKey.clear();
		}
		lua_pop( L, 1 ); // remove o valor do campo "callback" da pilha

		std::string errorMsg;
		if ( !CreateWebBrowserControl( ctrl, rain, errorMsg ) ) {
			lua_pushnil( L );
			lua_pushstring( L, errorMsg.c_str() );
			return 2;
		}

		ConnectEventSink( ctrl, ctx );
		if ( ctrl.transparent )
			ApplyParentTransparency( ctrl.hwndParent, true, ctrl.colorKey );


		lua_getfield( L, 1, "url" );
		// Default value
		std::wstring wurl = L"about:blank";

		if ( lua_isstring( L, -1 ) && lua_objlen( L, -1 ) > 0 ) {
			std::string candidate = lua_tostring( L, -1 );
			// Validate URL when VALIDATE_URLS is active.
			// "about:blank<script>..." is caught here — IsUrlSafe rejects
			// anything with a protocol other than the known safe ones.
			if ( ctrl.sanitizeFlags & VALIDATE_URLS ) {
				bool allowLocal = ( ctrl.sanitizeFlags & ALLOW_LOCAL ) != 0;
				if ( sanitize::IsUrlSafe( candidate, allowLocal ) ) {
					wurl = utf8_to_wstring( candidate );
				} else {
					RN_LOG( rain, LOG_WARNING,
									( L"Trident: [SECURITY] create() URL blocked — falling back to about:blank. "
										L"Blocked: " +
										utf8_to_wstring( candidate ) )
											.c_str() );
				}
			} else {
				wurl = utf8_to_wstring( candidate );
			}
		}

		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( 0x04 );
		ctrl.webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );
		lua_pop( L, 1 );


		ctx->controls[ctrl.configId] = ctrl;
		Control *storedCtrl = &ctx->controls[ctrl.configId];

		// Update subclass data pointer to the stored control
		if ( storedCtrl->subclassData ) {
			ParentSubclassData *sd = (ParentSubclassData *)storedCtrl->subclassData;
			sd->ctrl = storedCtrl;
		}

		// Build Lua object with methods
		lua_newtable( L );

		auto pushMethod = [&]( const char *name, lua_CFunction fn ) {
			lua_pushlightuserdata( L, storedCtrl );
			lua_pushcclosure( L, fn, 1 );
			lua_setfield( L, -2, name );
		};

		pushMethod( "navigate", l_navigate );
		pushMethod( "back", l_back );
		pushMethod( "forward", l_forward );
		pushMethod( "refresh", l_refresh );
		pushMethod( "stop", l_stop );
		pushMethod( "write", l_write );
		pushMethod( "writeline", l_writeline );
		pushMethod( "setTransparent", l_setTransparent );
		pushMethod( "execScript", l_execScript );
		pushMethod( "getURL", l_getURL );
		pushMethod( "quit", l_quit );
		pushMethod( "reposition", l_reposition );
		pushMethod( "show", l_show );
		pushMethod( "hide", l_hide );

		// browser.hwnd
		lua_pushlightuserdata( L, (void *)storedCtrl->hwndControl );
		lua_setfield( L, -2, "hwnd" );

		// browser:document() — returns a fresh table with document-level shortcuts.
		// Returns a new table every call so callers always get current COM state.
		// Usage:
		//   local doc = browser:document()
		//   doc:write("<p>hello</p>")
		//   print(doc:getTitle())
		pushMethod( "document", []( lua_State *L ) -> int {
			Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
			if ( !ctrl ) {
				lua_pushnil( L );
				return 1;
			}

			lua_newtable( L );

			auto pushDoc = [&]( const char *name, lua_CFunction fn ) {
				lua_pushlightuserdata( L, ctrl );
				lua_pushcclosure( L, fn, 1 );
				lua_setfield( L, -2, name );
			};

			pushDoc( "write", l_write );
			pushDoc( "writeln", l_writeline );

			// document:getTitle()
			lua_pushlightuserdata( L, ctrl );
			lua_pushcclosure(
					L,
					[]( lua_State *L ) -> int {
						Control *c = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
						if ( !c || !c->webBrowser ) {
							lua_pushnil( L );
							return 1;
						}
						CComPtr<IDispatch> docDisp;
						if ( SUCCEEDED( c->webBrowser->get_Document( &docDisp ) ) && docDisp ) {
							CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
							if ( htmlDoc ) {
								BSTR bTitle = nullptr;
								if ( SUCCEEDED( htmlDoc->get_title( &bTitle ) ) && bTitle ) {
									std::string title = wstring_to_utf8( bTitle );
									SysFreeString( bTitle );
									lua_pushlstring( L, title.c_str(), title.size() );
									return 1;
								}
							}
						}
						lua_pushnil( L );
						return 1;
					},
					1 );
			lua_setfield( L, -2, "getTitle" );

			return 1;
		} );

		// browser:window() — returns a fresh table with window-level shortcuts.
		// JS can call back to Lua via window.external.notify("name", value).
		// Usage:
		//   browser:window():eval("document.title = 'hello'")
		pushMethod( "window", []( lua_State *L ) -> int {
			Control *ctrl = (Control *)lua_touserdata( L, lua_upvalueindex( 1 ) );
			if ( !ctrl ) {
				lua_pushnil( L );
				return 1;
			}

			lua_newtable( L );

			// window:eval(script) — execScript with strict IIFE
			lua_pushlightuserdata( L, ctrl );
			lua_pushcclosure( L, l_execScript, 1 );
			lua_setfield( L, -2, "eval" );

			return 1;
		} );

		// Store the browser table in the Lua registry to keep it alive (prevent GC)
		// and so that l_create can read it back to inject `self` into the callback env.
		// Stack: [..., browser_table]
		storedCtrl->browserKey = "trident_browser_" + storedCtrl->callbackKey;
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, storedCtrl->browserKey.c_str() );

		// -------------------------------------------------------------------------
		// Inject `self` into the callback's environment — done exactly once here.
		//
		// Goal: the callback can reference `self` as if it were a global variable,
		// without receiving it as an argument:
		//   callback = function(event)
		//       print(self, event)   -- `self` is the browser table returned by create()
		//   end
		//
		// Strategy: replace the callback function's environment with a new table that
		// has `self = browser_table` and falls back to the original env via __index,
		// so all other globals (print, pairs, Rainmeter API, etc.) remain accessible.
		//
		// lua_setfenv modifies the function object in place (functions are reference
		// types in Lua 5.1/LuaJIT), so the registry entry already points to the
		// modified function — no need to re-store it.
		// -------------------------------------------------------------------------
		if ( !storedCtrl->callbackKey.empty() ) {
			lua_getfield( L, LUA_REGISTRYINDEX, storedCtrl->callbackKey.c_str() );
			// Stack: [..., browser_table, callback_fn]

			if ( lua_isfunction( L, -1 ) ) {
				lua_getfenv( L, -1 );
				// Stack: [..., browser_table, callback_fn, original_env]

				lua_newtable( L );
				// Stack: [..., browser_table, callback_fn, original_env, new_env]

				// new_env.self = browser_table
				lua_pushvalue( L, -4 );
				lua_setfield( L, -2, "self" );

				// setmetatable(new_env, { __index = original_env })
				lua_newtable( L );
				lua_pushvalue( L, -3 ); // original_env
				lua_setfield( L, -2, "__index" );
				lua_setmetatable( L, -2 );

				lua_remove( L, -2 ); // remove original_env
				// Stack: [..., browser_table, callback_fn, new_env]

				lua_setfenv( L, -2 ); // pops new_env, sets it on callback_fn
				// Stack: [..., browser_table, callback_fn]
			}

			lua_pop( L, 1 ); // pop callback_fn (or non-function value)
			// Stack: [..., browser_table]
		}

		// Set __gc metamethod
		lua_newtable( L );
		lua_pushlightuserdata( L, storedCtrl );
		lua_pushcclosure( L, l_gc, 1 );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );

		return 1;
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
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_trident, 1 );
		lua_setfield( L, -2, "webview.trident" );
		lua_pop( L, 2 );
	}

} // namespace mshtml
