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
#include <Utils/strings.hpp>
#include <Utils/filesystem.hpp>
#include <algorithm>
#include <atlbase.h>
#include <atlhost.h>
#include <commctrl.h>
#include <docobj.h>
#include <exdisp.h>
#include <mshtmdid.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <dispex.h>
#include <urlmon.h>

#pragma comment( lib, "urlmon.lib" )
#pragma comment( lib, "comctl32.lib" )

/// Message posted by EventSink::Invoke to the hidden window to trigger
/// immediate callback dispatch without waiting for Rainmeter's Update tick.
#define WM_TRIDENT_EVENT ( WM_APP + 2 )







namespace trident {

	/// Parent window subclass data
	struct ParentSubclassData {
		Control *ctrl; ///< Owning control.
		HWND hWndParent; ///< Handle to the parent window.
	};



	static LRESULT CALLBACK ParentSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );




	/**
	 * @brief HELPER - Applies a rounded rectangle region to the specified window.
	 */
	static void ApplyRoundedCorners( HWND hWnd, int width, int height, int radius ) {
		if ( radius <= 0 || width <= 0 || height <= 0 )
			return;

		HRGN hRgn = CreateRoundRectRgn( 0, 0, width, height, radius, radius );
		SetWindowRgn( hWnd, hRgn, TRUE );
	}



	/**
	 * @brief Resolve um URL ou path para uso no Navigate2.
	 *
	 * - URLs com scheme (http://, https://, about:, file://, etc.) → passam direto.
	 * - Paths relativos, com "./" ou com variáveis Rainmeter (#VAR#) →
	 *   resolvidos via Rain::absPath() e convertidos para file:// URL.
	 *
	 * @param rain     Instância Rain (para expansão de variáveis e resolução de path).
	 * @param url      URL ou path em UTF-8.
	 * @param isLocal  Saída: true se o resultado final é um file:// URL.
	 * @return URL resolvido em wide string, ou vazio em falha.
	 */
	static std::wstring ResolveUrl( Rain *rain, const std::string &url, bool &isLocal ) {
		isLocal = false;
		if ( url.empty() || !rain )
			return {};

		// Detecta se já tem um scheme (http://, about:, file://, data:, etc.)
		// Scheme = apenas letras antes do primeiro ':'
		auto hasScheme = [&]() -> bool {
			size_t colon = url.find( ':' );
			if ( colon == std::string::npos || colon == 0 )
				return false;
			for ( size_t i = 0; i < colon; ++i )
				if ( !isalpha( (unsigned char)url[i] ) )
					return false;
			return true;
		};

		if ( hasScheme() ) {
			isLocal = url.rfind( "file://", 0 ) == 0;
			return utf8_to_wstring( url );
		}

		// Path relativo, variável Rainmeter, ou "./" — resolver via absPath
		std::wstring wurl = utf8_to_wstring( url );

		// "./" ou ".\": RmPathToAbsolute não garante tratamento,
		// então substituímos pelo diretório da skin explicitamente.
		if ( wurl.size() >= 2 && wurl[0] == L'.' && ( wurl[1] == L'/' || wurl[1] == L'\\' ) )
			wurl = L"#CURRENTPATH#" + wurl.substr( 2 );

		std::wstring resolved = rain->absPath( wurl );
		if ( resolved.empty() )
			return {};

		// Converte path Windows → file:// URL
		// "C:\foo\bar.html" → "file:///C:/foo/bar.html"
		std::replace( resolved.begin(), resolved.end(), L'\\', L'/' );

		isLocal = true;
		return L"file:///" + resolved;
	}




	/**
	 * @brief HELPER - Calculates the final screen rectangle for the browser popup.
	 */
	static void GetConstrainedScreenRect( Control *ctrl, const RECT &parentRect, int &outX, int &outY, int &outW, int &outH ) {
		int desiredX = parentRect.left + ctrl->left;
		int desiredY = parentRect.top + ctrl->top;
		int desiredW = ctrl->width;
		int desiredH = ctrl->height;

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

		// SetWindowPos( ctrl->hwndControl, HWND_TOP, screenX, screenY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW );
		SetWindowPos( ctrl->hwndControl, HWND_TOP, screenX, screenY, width, height, SWP_NOACTIVATE );

		if ( ctrl->cornerRadius > 0 )
			ApplyRoundedCorners( ctrl->hwndControl, width, height, ctrl->cornerRadius );
	}



	/// Globals
	static std::unordered_map<Rain *, Context *> g_contexts;
	static std::mutex g_contextsMutex;
	static bool g_comInitialized = false;
	static bool g_comNeedsUninitialize = false;


	/**
	 * @brief Generates a unique Lua registry key for a control's callback.
	 */
	static std::string GetCallbackKey( Rain *rain, int configId ) {
		if ( !rain )
			return "";

		std::wstring skinName = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
		std::string name = wstring_to_utf8( skinName );
		std::replace( name.begin(), name.end(), '\\', '_' );
		std::replace( name.begin(), name.end(), '/', '_' );
		std::replace( name.begin(), name.end(), '.', '_' );
		return "trident_callback_" + name + "_" + std::to_string( configId );
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
						case EVENT_EXTERNAL:
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

							// Tenta decodificar como tabela Lua (JSON é subset válido para casos comuns)
							bool pushedTable = false;
							std::string attempt = "return " + data;
							if ( luaL_loadstring( L, attempt.c_str() ) == LUA_OK ) {
								if ( lua_pcall( L, 0, 1, 0 ) == LUA_OK && lua_istable( L, -1 ) ) {
									pushedTable = true; // table já está no topo
								} else {
									lua_pop( L, 1 ); // resultado não é table
								}
							} else {
								lua_pop( L, 1 ); // erro de compilação
							}

							if ( !pushedTable )
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
							// commandstatechange usa "command", navigate error usa "statusCode"
							lua_setfield( L, -2, ev.type == EVENT_COMMAND_STATE_CHANGE ? "command" : "statusCode" );
						}

						if ( ev.type == EVENT_COMMAND_STATE_CHANGE ) {
							lua_pushboolean( L, ev.cancel ? 1 : 0 ); // "enabled" no commandstatechange
							lua_setfield( L, -2, "enabled" );
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
	// Hidden window
	// -------------------------------------------------------------------------

	static LRESULT CALLBACK TridentWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
		if ( uMsg == WM_TRIDENT_EVENT ) {
			Context *ctx = (Context *)GetWindowLongPtrW( hWnd, GWLP_USERDATA );
			if ( ctx && ctx->rain )
				DrainEventQueue( ctx, ctx->rain );
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

	static bool RegisterHiddenWindowClass() {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof( WNDCLASSEXW );
		wc.lpfnWndProc = TridentWndProc;
		wc.hInstance = GetModuleHandleW( nullptr );
		wc.lpszClassName = L"RainJIT_Trident_Hidden";
		return RegisterClassExW( &wc ) != 0;
	}

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
			ctx
		);
		// clang-format on
	}


	// -------------------------------------------------------------------------
	// ExternalDispatch — window.external for JavaScript
	// -------------------------------------------------------------------------

	/**
	 * @brief IDispatch exposed to JavaScript as `window.external`.
	 *
	 * Allows JavaScript to send named events with data back to Lua via:
	 *   window.external.call("eventName", "data")
	 */
	struct ExternalDispatch : IDispatchEx {
		LONG m_refCount;
		Context *m_ctx;
		int m_configId;
		bool m_cleanedUp;

		std::unordered_map<DISPID, std::string> m_boundFuncs;
		std::unordered_map<std::string, DISPID> m_nameToDispId;
		DISPID m_nextDispId;

		ExternalDispatch( Context *ctx, int configId ) :
			m_refCount( 1 ),
			m_ctx( ctx ),
			m_configId( configId ),
			m_nextDispId( 1000 ),
			m_cleanedUp( false ) {
		}

		~ExternalDispatch() {
			if ( !m_cleanedUp )
				CleanupBoundFunctions();
		}

		Control *GetControl() {
			auto it = m_ctx->controls.find( m_configId );
			if ( it != m_ctx->controls.end() )
				return it->second.get();
			return nullptr;
		}

		lua_State *GetLuaState() {
			Control *ctrl = GetControl();
			if ( ctrl && ctrl->rain )
				return ctrl->rain->L;
			return nullptr;
		}

		// IUnknown
		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( ppv == nullptr )
				return E_POINTER;
			if ( riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IDispatchEx ) {
				*ppv = static_cast<IDispatchEx *>( this );
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_( ULONG ) AddRef() override {
			return InterlockedIncrement( &m_refCount );
		}
		STDMETHODIMP_( ULONG ) Release() override {
			LONG ref = InterlockedDecrement( &m_refCount );
			if ( ref == 0 ) {
				CleanupBoundFunctions();
				delete this;
			}
			return ref;
		}

		// IDispatch
		STDMETHODIMP GetTypeInfoCount( UINT * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo ** ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *rgszNames, UINT cNames, LCID, DISPID *rgDispId ) override {
			for ( UINT i = 0; i < cNames; ++i ) {
				if ( _wcsicmp( rgszNames[i], L"call" ) == 0 ) {
					rgDispId[i] = 1;
				} else {
					std::string name = wstring_to_utf8( rgszNames[i] );
					auto it = m_nameToDispId.find( name );
					if ( it != m_nameToDispId.end() )
						rgDispId[i] = it->second;
					else
						rgDispId[i] = DISPID_UNKNOWN;
				}
			}
			return S_OK;
		}
		STDMETHODIMP Invoke( DISPID dispIdMember, REFIID, LCID, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *, UINT * ) override {
			return InvokeEx( dispIdMember, LOCALE_USER_DEFAULT, wFlags, pDispParams, pVarResult, nullptr, nullptr );
		}

		// IDispatchEx
		STDMETHODIMP GetDispID( BSTR bstrName, DWORD grfdex, DISPID *pid ) override {
			if ( !bstrName || !pid )
				return E_POINTER;
			std::string name = wstring_to_utf8( bstrName );
			if ( name == "call" ) {
				*pid = 1;
				return S_OK;
			}
			auto it = m_nameToDispId.find( name );
			if ( it != m_nameToDispId.end() ) {
				*pid = it->second;
				return S_OK;
			}
			if ( grfdex & fdexNameEnsure ) {
				*pid = m_nextDispId++;
				m_nameToDispId[name] = *pid;
				return S_OK;
			}
			return DISP_E_UNKNOWNNAME;
		}

		STDMETHODIMP InvokeEx( DISPID id, LCID, WORD wFlags, DISPPARAMS *pdp, VARIANT *pvarRes, EXCEPINFO *, IServiceProvider * ) override {
			if ( id == 1 ) {
				if ( !pdp || pdp->cArgs < 1 )
					return DISP_E_BADPARAMCOUNT;
				std::wstring name, data;
				if ( pdp->cArgs >= 2 )
					name = luaVariant::ToWString( pdp->rgvarg[1] );
				if ( pdp->cArgs >= 1 )
					data = luaVariant::ToWString( pdp->rgvarg[0] );

				MshtmlEvent ev;
				ev.type = EVENT_EXTERNAL;
				ev.name = name;
				ev.data = data;
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

			auto it = m_boundFuncs.find( id );
			if ( it == m_boundFuncs.end() )
				return DISP_E_MEMBERNOTFOUND;

			lua_State *L = GetLuaState();
			if ( !L )
				return DISP_E_EXCEPTION;

			lua_getfield( L, LUA_REGISTRYINDEX, it->second.c_str() );
			if ( !lua_isfunction( L, -1 ) ) {
				lua_pop( L, 1 );
				return DISP_E_MEMBERNOTFOUND;
			}

			int argCount = pdp->cArgs;
			for ( int i = argCount - 1; i >= 0; --i )
				luaVariant::Push( L, pdp->rgvarg[i] );

			int status = lua_pcall( L, argCount, 1, 0 );
			if ( status != LUA_OK ) {
				const char *err = lua_tostring( L, -1 );
				// FIX #2: use m_ctx->rain directly — ctrl may be null if already removed from map
				Rain *logRain = m_ctx ? m_ctx->rain : nullptr;
				if ( logRain )
					RN_LOG( logRain, LOG_ERROR, ( L"Trident bound function error: " + utf8_to_wstring( err ) ).c_str() );
				lua_pop( L, 1 );
				return DISP_E_EXCEPTION;
			}

			if ( pvarRes ) {
				luaVariant::From( L, -1, pvarRes );
				lua_pop( L, 1 );
			}

			return S_OK;
		}

		STDMETHODIMP GetMemberName( DISPID, BSTR * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetMemberProperties( DISPID, DWORD, DWORD * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetNextDispID( DWORD, DISPID, DISPID * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetNameSpaceParent( IUnknown ** ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP DeleteMemberByName( BSTR, DWORD ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP DeleteMemberByDispID( DISPID ) override {
			return E_NOTIMPL;
		}

		HRESULT Bind( const std::string &name, lua_State *L, int funcIndex ) {
			DISPID id;
			CComBSTR bstrName( utf8_to_wstring( name ).c_str() );
			HRESULT hr = GetDispID( bstrName, fdexNameEnsure, &id );
			if ( FAILED( hr ) )
				return hr;

			auto oldIt = m_boundFuncs.find( id );
			if ( oldIt != m_boundFuncs.end() ) {
				lua_pushnil( L );
				lua_setfield( L, LUA_REGISTRYINDEX, oldIt->second.c_str() );
			}

			lua_pushvalue( L, funcIndex );
			std::string key = "trident_bound_" + std::to_string( reinterpret_cast<uintptr_t>( this ) ) + "_" + name;
			lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
			m_boundFuncs[id] = key;
			return S_OK;
		}

		void CleanupBoundFunctions() {
			if ( m_cleanedUp )
				return;
			m_cleanedUp = true;

			lua_State *L = GetLuaState();
			if ( L ) {
				for ( auto &pair : m_boundFuncs ) {
					lua_pushnil( L );
					lua_setfield( L, LUA_REGISTRYINDEX, pair.second.c_str() );
				}
			}
			m_boundFuncs.clear();
			m_nameToDispId.clear();
		}
	};



	// -------------------------------------------------------------------------
	// SkinSecurityManager — IInternetSecurityManager
	//
	// Permite que o MSHTML carregue recursos locais referenciados com paths
	// relativos dentro de documentos file:// da skin.
	// -------------------------------------------------------------------------
	struct SkinSecurityManager : IInternetSecurityManager {
		LONG m_refCount;
		std::wstring m_skinPath; ///< Diretório da skin normalizado para comparação.

		SkinSecurityManager( const std::wstring &skinPath ) :
			m_refCount( 1 ) {
			m_skinPath = skinPath;
			std::transform( m_skinPath.begin(), m_skinPath.end(), m_skinPath.begin(), ::towlower );
			std::replace( m_skinPath.begin(), m_skinPath.end(), L'\\', L'/' );
		}

		// IUnknown
		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			if ( riid == IID_IUnknown || riid == IID_IInternetSecurityManager ) {
				*ppv = static_cast<IInternetSecurityManager *>( this );
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_( ULONG ) AddRef() override {
			return InterlockedIncrement( &m_refCount );
		}
		STDMETHODIMP_( ULONG ) Release() override {
			LONG ref = InterlockedDecrement( &m_refCount );
			if ( ref == 0 )
				delete this;
			return ref;
		}

		/// Verifica se a URL é um file:// dentro do diretório da skin.
		bool isSkinUrl( LPCWSTR url ) {
			if ( !url )
				return false;
			std::wstring w = url;
			std::transform( w.begin(), w.end(), w.begin(), ::towlower );
			std::replace( w.begin(), w.end(), L'\\', L'/' );
			// file:///C:/path/skin/ → normaliza para comparação
			if ( w.rfind( L"file:///", 0 ) != 0 )
				return false;
			std::wstring path = w.substr( 8 ); // remove "file:///"
			return path.rfind( m_skinPath, 0 ) == 0;
		}

		// Retorna URLZONE_LOCAL_MACHINE para recursos da skin — permite carregamento.
		STDMETHODIMP MapUrlToZone( LPCWSTR pwszUrl, DWORD *pdwZone, DWORD ) override {
			if ( !pdwZone )
				return E_POINTER;
			if ( isSkinUrl( pwszUrl ) ) {
				*pdwZone = URLZONE_LOCAL_MACHINE;
				return S_OK;
			}
			return INET_E_DEFAULT_ACTION;
		}

		// Permite explicitamente as ações de carregamento para URLs da skin.
		STDMETHODIMP ProcessUrlAction( LPCWSTR pwszUrl, DWORD dwAction, BYTE *pPolicy, DWORD cbPolicy, BYTE *, DWORD, DWORD, DWORD ) override {
			if ( isSkinUrl( pwszUrl ) && cbPolicy >= sizeof( DWORD ) ) {
				*reinterpret_cast<DWORD *>( pPolicy ) = URLPOLICY_ALLOW;
				return S_OK;
			}
			return INET_E_DEFAULT_ACTION;
		}

		// Resto das interfaces — delega ao comportamento padrão.
		STDMETHODIMP SetSecuritySite( IInternetSecurityMgrSite * ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetSecuritySite( IInternetSecurityMgrSite ** ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetSecurityId( LPCWSTR, BYTE *, DWORD *, DWORD_PTR ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP QueryCustomPolicy( LPCWSTR, REFGUID, BYTE **, DWORD *, BYTE *, DWORD, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP SetZoneMapping( DWORD, LPCWSTR, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetZoneMappings( DWORD, IEnumString **, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
	};





	// -------------------------------------------------------------------------
	// WebBrowserSite — IOleClientSite + IDocHostUIHandler
	// FIX #1: added m_ctx member and updated constructor signature
	// -------------------------------------------------------------------------

	/**
	 * @brief Implements IOleClientSite and IDocHostUIHandler for the browser.
	 *
	 * Suppresses context menus, 3D borders, scrollbars, and optionally script errors.
	 * Exposes window.external via GetExternal().
	 */
	struct WebBrowserSite : IOleClientSite, IDocHostUIHandler, IServiceProvider {
		LONG m_refCount;
		Control *m_control;
		Context *m_ctx; ///< Needed by GetExternal to create ExternalDispatch.
		IUnknown *m_outer;

		WebBrowserSite( Control *ctrl, Context *ctx, IUnknown *outer ) :
			m_refCount( 1 ),
			m_control( ctrl ),
			m_ctx( ctx ),
			m_outer( outer ) {
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

		// IServiceProvider — entrega o SkinSecurityManager ao MSHTML.
		STDMETHODIMP QueryService( REFGUID guidService, REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			if ( guidService == SID_SInternetSecurityManager && riid == IID_IInternetSecurityManager && m_control && m_control->rain ) {

				// Só entrega se local files estiver permitido
				bool allowLocal = ( m_control->sanitizeFlags & SANITIZE_NONE ) == SANITIZE_NONE || ( m_control->sanitizeFlags & ALLOW_LOCAL ) != 0;

				if ( allowLocal ) {
					std::wstring skinPath = m_control->rain->absPath( L"#CURRENTPATH#" );
					if ( !skinPath.empty() ) {
						*ppv = new SkinSecurityManager( skinPath );
						return S_OK;
					}
				}
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

		STDMETHODIMP ShowContextMenu( DWORD, POINT *, IUnknown *, IDispatch * ) {
			return S_FALSE;
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

		/**
		 * FIX #1: uses m_ctx directly — no g_contexts lookup needed.
		 * FIX (mutex): g_contextsMutex protects only externalDispatch creation,
		 * not a g_contexts traversal, so std::mutex is safe here with no risk of
		 * deadlock (lock is never held by the caller at this point).
		 */
		STDMETHODIMP GetExternal( IDispatch **ppDispatch ) override {
			if ( !ppDispatch )
				return E_POINTER;
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			if ( !m_control->externalDispatch ) {
				m_control->externalDispatch = new ExternalDispatch( m_ctx, m_control->configId );
			}
			*ppDispatch = static_cast<IDispatchEx *>( m_control->externalDispatch );
			( *ppDispatch )->AddRef();
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



	// -------------------------------------------------------------------------
	// EventSink — DWebBrowserEvents2
	// -------------------------------------------------------------------------

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
				if ( m_ctx->hiddenWindow )
					PostMessage( m_ctx->hiddenWindow, WM_TRIDENT_EVENT, 0, 0 );
			};

			if ( dispIdMember == DISPID_DOCUMENTCOMPLETE ) {
				pushEvent( EVENT_DOCUMENT_COMPLETE );
				if ( m_ctx && m_ctx->rain )
					RN_LOG( m_ctx->rain, LOG_NOTICE, L"Trident: DocumentComplete" );

				auto it = m_ctx->controls.find( m_configId );
				if ( it != m_ctx->controls.end() ) {
					Control &ctrl = *it->second;
					if ( ctrl.webBrowser ) {
						CComPtr<IDispatch> docDisp;
						if ( SUCCEEDED( ctrl.webBrowser->get_Document( &docDisp ) ) && docDisp ) {

							// FIX #1: pass m_ctx so GetExternal can use it directly
							CComQIPtr<ICustomDoc> customDoc( docDisp );
							if ( customDoc ) {
								WebBrowserSite *site = new WebBrowserSite( &ctrl, m_ctx, nullptr );
								customDoc->SetUIHandler( site );
								site->Release();
							}

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
				VARIANT *pUrl = &pDispParams->rgvarg[5];
				if ( pUrl->vt == ( VT_BYREF | VT_VARIANT ) && pUrl->pvarVal )
					pUrl = pUrl->pvarVal;
				if ( pUrl->vt == VT_BSTR && pUrl->bstrVal ) {
					std::wstring url = pUrl->bstrVal;
					if ( url != L"about:blank" ) {
						bool cancel = false;
						auto ctrlIt = m_ctx->controls.find( m_configId );
						if ( ctrlIt != m_ctx->controls.end() ) {
							const Control &ctrl = *ctrlIt->second;
							if ( ctrl.enabled && !ctrl.callbackKey.empty() && m_ctx->rain->L ) {
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
										const char *err = lua_tostring( L, -1 );
										if ( m_ctx && m_ctx->rain )
											RN_LOG( m_ctx->rain, LOG_ERROR, ( L"Trident navigate callback error: " + utf8_to_wstring( err ) ).c_str() );
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
				std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
				m_ctx->eventBuffer.push( ev );
				m_ctx->hasPendingEvents = true;
			}

			else if ( dispIdMember == DISPID_PROGRESSCHANGE && pDispParams && pDispParams->cArgs >= 2 ) {
				MshtmlEvent ev;
				ev.type = EVENT_PROGRESS_CHANGE;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				ev.progress = ( pDispParams->rgvarg[1].vt == VT_I4 ) ? pDispParams->rgvarg[1].lVal : 0;
				ev.progressMax = ( pDispParams->rgvarg[0].vt == VT_I4 ) ? pDispParams->rgvarg[0].lVal : 0;
				std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
				m_ctx->eventBuffer.push( ev );
				m_ctx->hasPendingEvents = true;
			}

			else if ( dispIdMember == DISPID_DOWNLOADBEGIN )
				pushEvent( EVENT_DOWNLOAD_BEGIN );

			else if ( dispIdMember == DISPID_DOWNLOADCOMPLETE )
				pushEvent( EVENT_DOWNLOAD_COMPLETE );

			else if ( dispIdMember == DISPID_NAVIGATEERROR && pDispParams && pDispParams->cArgs >= 5 ) {
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
				std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
				m_ctx->eventBuffer.push( ev );
				m_ctx->hasPendingEvents = true;
			}

			else if ( dispIdMember == DISPID_NEWWINDOW2 && pDispParams && pDispParams->cArgs >= 2 ) {
				// Cancel via return false no callback Lua
				MshtmlEvent ev;
				ev.type = EVENT_NEW_WINDOW;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				// Chama callback direto aqui pois precisamos setar o cancel
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
							lua_pushstring( LS, "newwindow" );
							lua_setfield( LS, -2, "type" );
							if ( lua_pcall( LS, 2, 1, 0 ) == LUA_OK ) {
								bool cancel = ( lua_isboolean( LS, -1 ) && !lua_toboolean( LS, -1 ) );
								lua_pop( LS, 1 );
								VARIANT &vCancel = pDispParams->rgvarg[1];
								if ( ( vCancel.vt & VT_BYREF ) && vCancel.pboolVal )
									*vCancel.pboolVal = cancel ? VARIANT_TRUE : VARIANT_FALSE;
							} else {
								lua_pop( LS, 1 );
							}
						} else {
							lua_pop( LS, 1 );
						}
					}
				}
			}

			else if ( dispIdMember == DISPID_WINDOWCLOSING ) {
				// Mesmo padrão — cancelável direto
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
				std::lock_guard<std::mutex> lock( m_ctx->eventMutex );
				m_ctx->eventBuffer.push( ev );
				m_ctx->hasPendingEvents = true;
			}

			return S_OK;
		}
	};



	// -------------------------------------------------------------------------
	// Parent window subclass
	// -------------------------------------------------------------------------

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
				if ( wParam ) {
					if ( ctrl->hidden )
						ShowWindow( ctrl->hwndControl, SW_HIDE );
					else {
						UpdateControlPosition( ctrl );
						ShowWindow( ctrl->hwndControl, SW_SHOW );
					}
				} else {
					ShowWindow( ctrl->hwndControl, SW_HIDE );
				}
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



	// -------------------------------------------------------------------------
	// Event sink connection management
	// -------------------------------------------------------------------------

	static void ConnectEventSink( Control &ctrl, Context *ctx ) {
		if ( !ctrl.webBrowser )
			return;

		CComPtr<IConnectionPointContainer> cpc;
		if ( FAILED( ctrl.webBrowser->QueryInterface( IID_IConnectionPointContainer, (void **)&cpc ) ) )
			return;

		CComPtr<IConnectionPoint> cp;
		if ( FAILED( cpc->FindConnectionPoint( DIID_DWebBrowserEvents2, &cp ) ) )
			return;

		EventSink *sink = new EventSink( ctx, ctrl.configId );

		DWORD cookie = 0;
		if ( SUCCEEDED( cp->Advise( sink, &cookie ) ) ) {
			ctrl.eventCookie = cookie;
			ctrl.eventCP = cp.Detach();
			ctrl.eventSink = sink;
		} else {
			sink->Release();
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



	// -------------------------------------------------------------------------
	// CreateWebBrowserControl
	// -------------------------------------------------------------------------

	/**
	 * @brief Creates the popup window hosting the Shell.Explorer control.
	 */
	static bool CreateWebBrowserControl( Control &ctrl, Rain *rain, std::string &outError ) {
		if ( !IsWindow( ctrl.hwndParent ) ) {
			outError = "Invalid parent window handle";
			RN_LOG( rain, LOG_ERROR, L"Trident: Invalid hwndParent." );
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
			screenX, screenY, finalW, finalH,
			nullptr, nullptr,
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

		if ( ctrl.cornerRadius > 0 )
			ApplyRoundedCorners( hwndPopup, finalW, finalH, ctrl.cornerRadius );

		CComPtr<IUnknown> unk;
		HRESULT hr = AtlAxGetControl( hwndPopup, &unk );
		if ( FAILED( hr ) || !unk ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "AtlAxGetControl failed";
			RN_LOG( rain, LOG_ERROR, L"Trident: AtlAxGetControl failed." );
			return false;
		}

		hr = unk->QueryInterface( IID_IWebBrowser2, (void **)&ctrl.webBrowser );
		if ( FAILED( hr ) || !ctrl.webBrowser ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "QueryInterface(IWebBrowser2) failed";
			RN_LOG( rain, LOG_ERROR, L"Trident: QueryInterface(IWebBrowser2) failed." );
			return false;
		}

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

		ParentSubclassData *subData = new ParentSubclassData{};
		subData->ctrl = &ctrl;
		subData->hWndParent = ctrl.hwndParent;

		UINT_PTR subclassId = (UINT_PTR)ctrl.configId;

		if ( !SetWindowSubclass( ctrl.hwndParent, ParentSubclassProc, subclassId, (DWORD_PTR)subData ) ) {
			delete subData;
			ctrl.subclassData = nullptr;
			RN_LOG( rain, LOG_WARNING, L"Trident: SetWindowSubclass failed." );
		} else {
			ctrl.subclassData = subData;
			RN_LOG( rain, LOG_NOTICE, L"Trident: Parent subclass installed." );
		}

		{
			RECT rc;
			GetWindowRect( hwndPopup, &rc );
			// clang-format off
			wchar_t buf[256];
			swprintf_s(
				buf,
				L"Trident: Popup created. Rect=(%d,%d,%d,%d) insideSkin=%s padding=(%d,%d,%d,%d) radius=%d",
				rc.left, rc.top, rc.right, rc.bottom,
				ctrl.insideSkin ? L"YES" : L"NO",
				ctrl.padLeft, ctrl.padTop, ctrl.padWidth, ctrl.padHeight,
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

	/**
	 * @brief Stable indirection between Lua closures and a live Control.
	 *
	 * All Lua closures hold a ControlHandle* instead of a raw Control*.
	 * DoQuit nulls handle->ctrl BEFORE freeing the Control, so any closure
	 * called after quit — including l_gc fired by the GC after Cleanup() —
	 * sees a null ctrl and returns immediately.
	 *
	 * Lifetime: allocated in l_create, freed by l_gc after DoQuit has nulled ctrl.
	 */
	struct ControlHandle {
		Control *ctrl; ///< Non-null while the control is alive; nullptr after DoQuit.
	};

	/**
	 * FIX #4: g_contextsMutex is NOT held while ProcessMessages calls
	 * DrainEventQueue. Callbacks fired during drain may call browser:quit()
	 * which acquires the mutex in DoQuit — holding it here would deadlock.
	 */
	int ProcessMessages( Rain *rain ) {
		Context *ctx = nullptr;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = g_contexts.find( rain );
			if ( it == g_contexts.end() )
				return 0;
			ctx = it->second;
		}
		return DrainEventQueue( ctx, rain );
	}

	void Cleanup( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_contextsMutex );
		auto it = g_contexts.find( rain );
		if ( it == g_contexts.end() )
			return;
		Context *ctx = it->second;

		if ( ctx->hiddenWindow ) {
			DestroyWindow( ctx->hiddenWindow );
			ctx->hiddenWindow = nullptr;
		}

		for ( auto &pair : ctx->controls ) {
			Control &ctrl = *pair.second;

			if ( ctrl.handle ) {
				static_cast<ControlHandle *>( ctrl.handle )->ctrl = nullptr;
				ctrl.handle = nullptr;
			}

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
			if ( ctrl.externalDispatch ) {
				ExternalDispatch *ext = static_cast<ExternalDispatch *>( ctrl.externalDispatch );
				ctrl.externalDispatch = nullptr;
				ext->CleanupBoundFunctions();
				ext->Release();
			}
		}

		ctx->controls.clear();
		delete ctx;
		g_contexts.erase( it );

		if ( g_contexts.empty() && g_comInitialized && g_comNeedsUninitialize ) {
			OleUninitialize();
			g_comInitialized = false;
			g_comNeedsUninitialize = false;
		}
	}

	// -------------------------------------------------------------------------
	// Lua bindings
	// -------------------------------------------------------------------------

	/**
	 * @brief Converts a control's SanitizeFlags bitmask into sanitize::Options.
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
	 * @brief Core cleanup logic shared by l_quit and l_gc.
	 *
	 * Nulls h->ctrl first so reentrant or late calls are safe no-ops.
	 * Saves browserKey locally before erasing from the map so the Lua registry
	 * entry can be cleared even after the Control has been freed.
	 */
	static void DoQuit( ControlHandle *h, lua_State *L ) {
		if ( !h || !h->ctrl )
			return;

		Control *ctrl = h->ctrl;
		h->ctrl = nullptr; // disarm all closures

		Rain *rain = ctrl->rain;
		std::string callbackKey = ctrl->callbackKey;
		std::string browserKey = ctrl->browserKey;
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
		if ( ctrl->externalDispatch ) {
			ExternalDispatch *ext = static_cast<ExternalDispatch *>( ctrl->externalDispatch );
			ctrl->externalDispatch = nullptr;
			ext->CleanupBoundFunctions();
			ext->Release();
		}

		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );
			auto it = g_contexts.find( rain );
			if ( it != g_contexts.end() )
				it->second->controls.erase( configId );
		}

		if ( rain && rain->L && !callbackKey.empty() ) {
			lua_pushnil( rain->L );
			lua_setfield( rain->L, LUA_REGISTRYINDEX, callbackKey.c_str() );
		}
		if ( rain && rain->L && !browserKey.empty() ) {
			lua_pushnil( rain->L );
			lua_setfield( rain->L, LUA_REGISTRYINDEX, browserKey.c_str() );
		}
	}



	static int l_navigate( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;

		const char *url = luaL_checkstring( L, 2 );

		bool isLocal = false;
		std::wstring wurl = ResolveUrl( ctrl->rain, url, isLocal );

		if ( wurl.empty() ) {
			RN_LOG( ctrl->rain, LOG_WARNING, ( L"Trident: navigate() failed to resolve: " + utf8_to_wstring( url ) ).c_str() );
			return 0;
		}

		if ( ctrl->sanitizeFlags & VALIDATE_URLS ) {
			// Paths resolvidos via absPath são intencionais — herdam ALLOW_LOCAL
			bool allowLocal = isLocal || ( ctrl->sanitizeFlags & ALLOW_LOCAL ) != 0;
			if ( !sanitize::IsUrlSafe( wstring_to_utf8( wurl ), allowLocal ) ) {
				RN_LOG( ctrl->rain, LOG_WARNING, ( L"Trident: [SECURITY] navigate() blocked: " + wurl ).c_str() );
				return 0;
			}
		}

		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( 0x04 );
		ctrl->webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );
		RN_LOG( ctrl->rain, LOG_NOTICE, ( L"Trident: navigate -> " + wurl ).c_str() );
		return 0;
	}



	static int l_back( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->GoBack();
		return 0;
	}



	static int l_forward( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->GoForward();
		return 0;
	}



	static int l_refresh( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->Refresh();
		return 0;
	}



	static int l_stop( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;
		ctrl->webBrowser->Stop();
		return 0;
	}

	static int l_write( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
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
		if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp )
			return 0;

		CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
		if ( !htmlDoc )
			return 0;

		// document.open() — resets the document, clears existing content.
		// Invoked via IDispatch because IHTMLDocument2::open() requires
		// several optional parameters that are inconvenient to supply via COM.
		DISPID dispOpen = 0;
		OLECHAR *openName = const_cast<OLECHAR *>( L"open" );
		if ( SUCCEEDED( docDisp->GetIDsOfNames( IID_NULL, &openName, 1, LOCALE_USER_DEFAULT, &dispOpen ) ) ) {
			DISPPARAMS noArgs = {};
			docDisp->Invoke( dispOpen, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &noArgs, nullptr, nullptr, nullptr );
		}

		// document.write(html) — inject content into the open stream.
		std::wstring wtext = utf8_to_wstring( content );
		CComBSTR bstrText( wtext.c_str() );
		DISPPARAMS params = {};
		VARIANTARG varg;
		VariantInit( &varg );
		varg.vt = VT_BSTR;
		varg.bstrVal = bstrText;
		params.rgvarg = &varg;
		params.cArgs = 1;
		docDisp->Invoke( DISPID_IHTMLDOCUMENT2_WRITE, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, nullptr, nullptr, nullptr );
		VariantClear( &varg );

		// document.close() — finalises the parse and fires documentcomplete.
		htmlDoc->close();

		return 0;
	}

	static int l_setTransparent( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
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

	static int l_execScript( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->webBrowser )
			return 0;

		const char *arg = luaL_checkstring( L, 2 );
		std::string script = arg;

		std::wstring warg = utf8_to_wstring( arg );

		// "./" ou ".\" → diretório da skin, igual ao ResolveUrl
		if ( warg.size() >= 2 && warg[0] == L'.' && ( warg[1] == L'/' || warg[1] == L'\\' ) )
			warg = L"#CURRENTPATH#" + warg.substr( 2 );

		// Tenta resolver como path — aceita variáveis Rainmeter e "./"
		std::wstring resolved = ctrl->rain->absPath( warg );
		if ( !resolved.empty() && fs::fileExists( resolved ) ) {
			if ( !fs::ReadTextFile( resolved, script ) ) {
				lua_pushnil( L );
				lua_pushstring( L, "Failed to read script file" );
				return 2;
			}
			RN_LOG( ctrl->rain, LOG_NOTICE, ( L"Trident: execScript loading file: " + resolved ).c_str() );
		}

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

		CComPtr<IHTMLElement> body;
		if ( FAILED( htmlDoc->get_body( &body ) ) || !body ) {
			lua_pushnil( L );
			lua_pushstring( L, "No body" );
			return 2;
		}

		std::string wrapped = "(function(){\n"
													"    'use strict';\n"
													"    try {\n"
													"        var r = (function(){\n";
		wrapped += script;
		wrapped += "\n})();\n"
							 "        document.body.setAttribute('data-trident-ret', String(r));\n"
							 "    } catch(e) {\n"
							 "        document.body.setAttribute('data-trident-ret', 'ERROR:' + e.message);\n"
							 "    }\n"
							 "})();";

		std::wstring wscript = utf8_to_wstring( wrapped );
		CComBSTR bstrScript( wscript.c_str() );
		CComBSTR bstrLang( L"JavaScript" );

		CComVariant resultDummy;
		HRESULT hrExec = window->execScript( bstrScript, bstrLang, &resultDummy );
		if ( FAILED( hrExec ) ) {
			lua_pushnil( L );
			lua_pushstring( L, "execScript failed" );
			lua_pushinteger( L, hrExec );
			return 3;
		}

		CComBSTR attrName( L"data-trident-ret" );
		CComVariant attrValue;
		HRESULT hrGet = body->getAttribute( attrName, 2, &attrValue );

		VARIANT_BOOL vbSuccess = VARIANT_FALSE;
		body->removeAttribute( attrName, 2, &vbSuccess );

		if ( FAILED( hrGet ) || attrValue.vt == VT_EMPTY || attrValue.vt == VT_NULL ) {
			lua_pushnil( L );
			return 1;
		}

		if ( attrValue.vt != VT_BSTR ) {
			if ( FAILED( VariantChangeType( &attrValue, &attrValue, 0, VT_BSTR ) ) ) {
				lua_pushnil( L );
				lua_pushstring( L, "Variant conversion failed" );
				return 2;
			}
		}

		std::string resultStr = wstring_to_utf8( attrValue.bstrVal );

		if ( resultStr.compare( 0, 6, "ERROR:" ) == 0 ) {
			lua_pushnil( L );
			lua_pushstring( L, resultStr.c_str() + 6 );
			return 2;
		}

		if ( resultStr == "true" ) {
			lua_pushboolean( L, 1 );
			return 1;
		}
		if ( resultStr == "false" ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		// nil — JavaScript pode retornar "null", "undefined" ou vazio
		if ( resultStr == "null" || resultStr == "undefined" || resultStr.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		// Inteiro
		char *endptr;
		errno = 0;
		long longVal = strtol( resultStr.c_str(), &endptr, 10 );
		if ( *endptr == '\0' && errno == 0 ) {
			lua_pushinteger( L, longVal );
			return 1;
		}

		// Double
		double dblVal = strtod( resultStr.c_str(), &endptr );
		if ( *endptr == '\0' && errno == 0 ) {
			lua_pushnumber( L, dblVal );
			return 1;
		}

		// String
		lua_pushlstring( L, resultStr.c_str(), resultStr.size() );
		return 1;
	}

	static int l_getURL( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
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

	static int l_reposition( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl || !ctrl->hwndParent )
			return 0;
		UpdateControlPosition( ctrl );
		return 0;
	}

	static int l_show( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = false;
		ShowWindow( ctrl->hwndControl, SW_SHOWNOACTIVATE );
		UpdateControlPosition( ctrl );
		return 0;
	}

	static int l_hide( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		Control *ctrl = h ? h->ctrl : nullptr;
		if ( !ctrl || !ctrl->hwndControl )
			return 0;
		ctrl->hidden = true;
		ShowWindow( ctrl->hwndControl, SW_HIDE );
		return 0;
	}

	static int l_quit( lua_State *L ) {
		ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		DoQuit( h, L );
		return 0;
	}

	static int l_gc( lua_State *L ) {
		ControlHandle **pp = (ControlHandle **)lua_touserdata( L, 1 );
		if ( !pp || !*pp )
			return 0;
		ControlHandle *h = *pp;
		*pp = nullptr;
		DoQuit( h, L );
		delete h;
		return 0;
	}

	// -------------------------------------------------------------------------
	// l_create
	// -------------------------------------------------------------------------

	/**
	 * @brief Creates a new browser control.
	 *
	 * Lua signature: trident.create(table)
	 *
	 * Lock discipline — three distinct phases:
	 *   PHASE 1 (locked)   — COM init, Context creation.
	 *   PHASE 2 (unlocked) — Lua option parsing, CreateWebBrowserControl, Navigate2.
	 *                         Trident may call GetExternal here; lock must NOT be held.
	 *   PHASE 3 (locked)   — Insert Control into map, create ExternalDispatch/ControlHandle.
	 *
	 * FIX #5: AtlAxWinInit() moved outside the mutex (idempotent, no shared state to protect).
	 */
	static int l_create( lua_State *L ) {
		Rain *rain = (Rain *)lua_touserdata( L, lua_upvalueindex( 1 ) );
		if ( !rain || !rain->hwnd ) {
			lua_pushnil( L );
			lua_pushstring( L, "Invalid Rain instance" );
			return 2;
		}

		// FIX #5: outside the lock — idempotent Win32 class registration.
		AtlAxWinInit();

		// -------------------------------------------------------------------
		// PHASE 1 — shared state only, safe to hold the lock.
		// -------------------------------------------------------------------
		Context *ctx = nullptr;
		int configId = 0;
		{
			std::lock_guard<std::mutex> lock( g_contextsMutex );

			if ( !g_comInitialized ) {
				HRESULT hr = OleInitialize( nullptr ); // ← não CoInitializeEx
				if ( hr == S_OK || hr == S_FALSE ) {
					g_comInitialized = true;
					g_comNeedsUninitialize = true;
				} else if ( hr == RPC_E_CHANGED_MODE ) {
					g_comInitialized = true;
					g_comNeedsUninitialize = false;
					RN_LOG( rain, LOG_WARNING, L"Trident: COM já inicializado com threading incompatível." );
				} else {
					lua_pushnil( L );
					lua_pushstring( L, "OLE initialization failed" );
					return 2;
				}
			}

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

			configId = ctx->nextId++;
		}
		// g_contextsMutex is now FREE — Trident can call GetExternal safely.

		// -------------------------------------------------------------------
		// PHASE 2 — Lua parsing + COM operations. No lock held.
		// -------------------------------------------------------------------
		luaL_checktype( L, 1, LUA_TTABLE );
		Control ctrl = {};
		ctrl.configId = configId;
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
		ctrl.sanitizeFlags = SANITIZE_ALL;
		ctrl.hidden = false;

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
				}
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		{
			const wchar_t *lvl = ctrl.sanitizeFlags == SANITIZE_ALL ? L"ALL" : ctrl.sanitizeFlags == SANITIZE_NONE ? L"NONE" : L"CUSTOM";
			wchar_t buf[128];
			swprintf_s( buf, L"Trident: sanitize=%s (flags=0x%X)", lvl, ctrl.sanitizeFlags );
			RN_LOG( rain, LOG_NOTICE, buf );
		}

		lua_getfield( L, 1, "callback" );
		if ( lua_isfunction( L, -1 ) ) {
			ctrl.callbackKey = GetCallbackKey( rain, ctrl.configId );
			lua_pushvalue( L, -1 );
			lua_setfield( L, LUA_REGISTRYINDEX, ctrl.callbackKey.c_str() );
		} else {
			ctrl.callbackKey.clear();
		}
		lua_pop( L, 1 );

		std::wstring wurl = L"about:blank";
		lua_getfield( L, 1, "url" );
		if ( lua_isstring( L, -1 ) && lua_objlen( L, -1 ) > 0 ) {
			std::string candidate = lua_tostring( L, -1 );
			bool isLocal = false;
			std::wstring resolved = ResolveUrl( rain, candidate, isLocal );

			if ( !resolved.empty() ) {
				if ( ctrl.sanitizeFlags & VALIDATE_URLS ) {
					bool allowLocal = isLocal || ( ctrl.sanitizeFlags & ALLOW_LOCAL ) != 0;
					if ( sanitize::IsUrlSafe( wstring_to_utf8( resolved ), allowLocal ) )
						wurl = resolved;
					else
						RN_LOG( rain, LOG_WARNING, ( L"Trident: [SECURITY] create() URL blocked — about:blank. Blocked: " + resolved ).c_str() );
				} else {
					wurl = resolved;
				}
			}
		}
		lua_pop( L, 1 );

		std::string errorMsg;
		if ( !CreateWebBrowserControl( ctrl, rain, errorMsg ) ) {
			lua_pushnil( L );
			lua_pushstring( L, errorMsg.c_str() );
			return 2;
		}

		ConnectEventSink( ctrl, ctx );

		CComVariant vUrl( wurl.c_str() );
		CComVariant vFlags( 0x04 );
		ctrl.webBrowser->Navigate2( &vUrl, &vFlags, nullptr, nullptr, nullptr );

		// -------------------------------------------------------------------
		// PHASE 3 — insert into map, build Lua object. Lock re-acquired.
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

			ExternalDispatch *extDisp = new ExternalDispatch( ctx, storedCtrl->configId );
			storedCtrl->externalDispatch = extDisp;

			handle = new ControlHandle{ storedCtrl };
			storedCtrl->handle = handle;
		}

		// Build Lua browser table (no lock needed — unique_ptr keeps address stable).
		lua_newtable( L );

		auto pushMethod = [&]( const char *name, lua_CFunction fn ) {
			lua_pushlightuserdata( L, handle );
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
		pushMethod( "show", l_show );
		pushMethod( "hide", l_hide );

		// browser:bind(name, func)
		lua_pushlightuserdata( L, handle );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					Control *ctrl = h ? h->ctrl : nullptr;
					if ( !ctrl || !ctrl->externalDispatch ) {
						luaL_error( L, "Browser not ready or externalDispatch missing" );
						return 0;
					}
					const char *name = luaL_checkstring( L, 2 );
					luaL_checktype( L, 3, LUA_TFUNCTION );
					ExternalDispatch *ext = (ExternalDispatch *)ctrl->externalDispatch;
					if ( FAILED( ext->Bind( name, L, 3 ) ) )
						luaL_error( L, "Failed to bind method '%s'", name );
					return 0;
				},
				1 );
		lua_setfield( L, -2, "bind" );

		lua_pushlightuserdata( L, (void *)storedCtrl->hwndControl );
		lua_setfield( L, -2, "hwnd" );

		// browser:document() — returns a ComProxy wrapping the live IHTMLDocument2.
		// All properties and methods are accessible directly via __index/__newindex.
		pushMethod( "document", []( lua_State *L ) -> int {
			ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
			Control *ctrl = h ? h->ctrl : nullptr;
			if ( !ctrl || !ctrl->webBrowser ) {
				lua_pushnil( L );
				return 1;
			}

			CComPtr<IDispatch> docDisp;
			if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
				lua_pushnil( L );
				return 1;
			}

			ComProxy::Push( L, docDisp );
			return 1;
		} );

		// browser:window() — returns a ComProxy wrapping the live IHTMLWindow2.
		pushMethod( "window", []( lua_State *L ) -> int {
			ControlHandle *h = (ControlHandle *)lua_touserdata( L, lua_upvalueindex( 1 ) );
			Control *ctrl = h ? h->ctrl : nullptr;
			if ( !ctrl || !ctrl->webBrowser ) {
				lua_pushnil( L );
				return 1;
			}

			CComPtr<IDispatch> docDisp;
			if ( FAILED( ctrl->webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
				lua_pushnil( L );
				return 1;
			}

			CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
			if ( !htmlDoc ) {
				lua_pushnil( L );
				return 1;
			}

			CComPtr<IHTMLWindow2> window;
			if ( FAILED( htmlDoc->get_parentWindow( &window ) ) || !window ) {
				lua_pushnil( L );
				return 1;
			}

			CComQIPtr<IDispatch> winDisp( window );
			ComProxy::Push( L, winDisp );
			return 1;
		} );

		storedCtrl->browserKey = "trident_browser_" + storedCtrl->callbackKey;
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, storedCtrl->browserKey.c_str() );

		// Callback API is explicitly (browser, event) — no environment injection needed.
		// All call sites (DrainEventQueue, BeforeNavigate2) already pass browser as arg1.

		ControlHandle **sentinelData = static_cast<ControlHandle **>( lua_newuserdata( L, sizeof( ControlHandle * ) ) );
		*sentinelData = handle;
		lua_newtable( L );
		lua_pushcfunction( L, l_gc );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );
		lua_setfield( L, -2, "__sentinel" );

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
		ComProxy::Register( L );
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_trident, 1 );
		lua_setfield( L, -2, "webview.trident" );
		lua_pop( L, 2 );
	}

} // namespace trident
