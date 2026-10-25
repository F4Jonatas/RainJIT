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
 * @file trident_external.cpp
 * @brief The window.external bridge (IDispatchEx) exposed to the page.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	// -------------------------------------------------------------------------
	// ExternalDispatch - window.external for JavaScript
	// -------------------------------------------------------------------------

	/**
	 * @brief Fills an EXCEPINFO so the page sees a real error message instead of "Unspecified error" (item 24).
	 *
	 * @param pei   Caller supplied structure (may be null, e.g. for plain IDispatch::Invoke without one).
	 * @param text  Message; truncated to kMaxExceptionText characters.
	 * @return DISP_E_EXCEPTION.
	 */
	static HRESULT FillException( EXCEPINFO *pei, const std::wstring &text ) {
		if ( pei ) {
			ZeroMemory( pei, sizeof( *pei ) );
			pei->scode = E_FAIL;
			pei->bstrSource = SysAllocString( L"RainJIT.Trident" );
			pei->bstrDescription = SysAllocStringLen( text.c_str(), (UINT)( text.size() < kMaxExceptionText ? text.size() : kMaxExceptionText ) );
		}
		return DISP_E_EXCEPTION;
	}

	/**
	 * @brief Callable object returned when a method of `window.external` is READ without being called (item 24).
	 *
	 * `if ( window.external && window.external.call )` is a common feature test; reading a member that
	 * only supports invocation used to fail with DISP_E_BADPARAMCOUNT. This object forwards its default
	 * member (DISPID_VALUE) to the parent, keeping the caller's IServiceProvider so the per-frame trust
	 * check of ExternalDispatch::InvokeEx still applies to every call made through the reference.
	 */
	struct ExternalMethodRef : IDispatchEx {
		LONG m_refCount;
		IDispatchEx *m_parent; ///< Owning ExternalDispatch (AddRef'd).
		DISPID m_id; ///< Member this reference stands for.

		ExternalMethodRef( IDispatchEx *parent, DISPID id ) :
			m_refCount( 1 ),
			m_parent( parent ),
			m_id( id ) {
			m_parent->AddRef();
		}
		~ExternalMethodRef() {
			m_parent->Release();
		}

		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( !ppv )
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
			if ( ref == 0 )
				delete this;
			return ref;
		}

		STDMETHODIMP GetTypeInfoCount( UINT *pctinfo ) override {
			if ( pctinfo )
				*pctinfo = 0;
			return S_OK;
		}
		STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo ** ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *, UINT, LCID, DISPID *rgDispId ) override {
			if ( rgDispId )
				*rgDispId = DISPID_UNKNOWN;
			return DISP_E_UNKNOWNNAME;
		}
		STDMETHODIMP Invoke( DISPID id, REFIID, LCID lcid, WORD wFlags, DISPPARAMS *pdp, VARIANT *pvarRes, EXCEPINFO *pei, UINT * ) override {
			return InvokeEx( id, lcid, wFlags, pdp, pvarRes, pei, nullptr );
		}
		STDMETHODIMP GetDispID( BSTR, DWORD, DISPID *pid ) override {
			if ( pid )
				*pid = DISPID_UNKNOWN;
			return DISP_E_UNKNOWNNAME;
		}
		STDMETHODIMP InvokeEx( DISPID id, LCID lcid, WORD wFlags, DISPPARAMS *pdp, VARIANT *pvarRes, EXCEPINFO *pei, IServiceProvider *pspCaller ) override {
			if ( id != DISPID_VALUE )
				return DISP_E_MEMBERNOTFOUND;
			return m_parent->InvokeEx( m_id, lcid, (WORD)( wFlags | DISPATCH_METHOD ), pdp, pvarRes, pei, pspCaller );
		}
		STDMETHODIMP DeleteMemberByName( BSTR, DWORD ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP DeleteMemberByDispID( DISPID ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetMemberProperties( DISPID, DWORD, DWORD * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetMemberName( DISPID, BSTR * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetNextDispID( DWORD, DISPID, DISPID * ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetNameSpaceParent( IUnknown **ppunk ) override {
			if ( ppunk )
				*ppunk = nullptr;
			return E_NOTIMPL;
		}
	};

	/**
	 * @brief IDispatch exposed to JavaScript as `window.external`.
	 *
	 * Allows JavaScript to send named events with data back to Lua via:
	 *   window.external.call("eventName", "data")
	 */
	struct ExternalDispatch : IDispatchEx {
		LONG m_refCount;
		std::shared_ptr<Context> m_ctx; ///< Fix #11: keeps the Context alive while MSHTML holds this object.
		int m_configId;
		bool m_cleanedUp;

		std::unordered_map<DISPID, std::string> m_boundFuncs;
		std::unordered_map<std::string, DISPID> m_nameToDispId;
		DISPID m_nextDispId;

		ExternalDispatch( std::shared_ptr<Context> ctx, int configId ) :
			m_refCount( 1 ),
			m_ctx( std::move( ctx ) ),
			m_configId( configId ),
			m_cleanedUp( false ),
			m_nextDispId( 1000 ) {
		}

		~ExternalDispatch() {
			if ( !m_cleanedUp )
				CleanupBoundFunctions();
		}

		Control *GetControl() {
			if ( !m_ctx || !m_ctx->alive )
				return nullptr;
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
		STDMETHODIMP Invoke( DISPID dispIdMember, REFIID, LCID, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT * ) override {
			return InvokeEx( dispIdMember, LOCALE_USER_DEFAULT, wFlags, pDispParams, pVarResult, pExcepInfo, nullptr );
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
				// Item 9: a page could otherwise create unlimited names (memory DoS).
				if ( name.size() > kMaxExternalName || m_nameToDispId.size() >= kMaxExternalNames )
					return E_OUTOFMEMORY;
				*pid = m_nextDispId++;
				m_nameToDispId[name] = *pid;
				return S_OK;
			}
			return DISP_E_UNKNOWNNAME;
		}

		/**
		 * @brief Invokes `external.call(name, data)` or a function registered with browser:bind().
		 *
		 * Item 24 fixes: the Lua stack is always restored (the result used to stay on it when the
		 * caller passed no result pointer), `pdp` may be null, the EXCEPINFO is filled, `call` is a
		 * reserved name (a bound "call" was shadowed and never ran), a single argument of `call` is the
		 * event NAME (it used to become `data`), and a bare read of a method returns a callable reference
		 * instead of an error.
		 */
		STDMETHODIMP InvokeEx( DISPID id, LCID lcid, WORD wFlags, DISPPARAMS *pdp, VARIANT *pvarRes, EXCEPINFO *pei, IServiceProvider *pspCaller ) override {
			if ( pvarRes )
				VariantInit( pvarRes );

			// Fix #8 / item 5: re-check on every call, now also against the CALLING frame
			// (a third-party iframe inside a trusted page must not reach the bridge).
			if ( m_cleanedUp || !IsOriginTrusted( GetControl(), pspCaller ) )
				return E_ACCESSDENIED;

			const bool isCall = ( wFlags & DISPATCH_METHOD ) != 0;
			const bool isGet = ( wFlags & DISPATCH_PROPERTYGET ) != 0;
			const bool known = id == 1 || m_boundFuncs.find( id ) != m_boundFuncs.end();
			if ( !known )
				return DISP_E_MEMBERNOTFOUND;

			// Reading the member without calling it: hand out a callable reference.
			if ( !isCall ) {
				if ( !isGet || !pvarRes )
					return DISP_E_MEMBERNOTFOUND;
				ExternalMethodRef *ref = new ExternalMethodRef( static_cast<IDispatchEx *>( this ), id );
				pvarRes->vt = VT_DISPATCH;
				pvarRes->pdispVal = static_cast<IDispatchEx *>( ref );
				return S_OK;
			}

			const UINT argc = pdp ? pdp->cArgs : 0;
			if ( argc > kMaxExternalArgs )
				return DISP_E_BADPARAMCOUNT;

			if ( id == 1 ) {
				if ( argc < 1 )
					return DISP_E_BADPARAMCOUNT;
				// Arguments arrive reversed: rgvarg[argc - 1] is the first one.
				std::wstring name = luaVariant::ToWString( pdp->rgvarg[argc - 1] );
				std::wstring data;
				if ( argc >= 2 )
					data = luaVariant::ToWString( pdp->rgvarg[argc - 2] );

				// Hard limits on page-controlled input.
				if ( name.size() > kMaxExternalName || data.size() > kMaxExternalData )
					return S_OK;

				MshtmlEvent ev;
				ev.type = EVENT_EXTERNAL;
				ev.name = name;
				ev.data = data;
				ev.configId = m_configId;
				ev.timestamp = GetTickCount64();
				EnqueueEvent( *m_ctx, std::move( ev ) ); // flood protection: count + byte budget, drops when full
				return S_OK;
			}

			lua_State *L = GetLuaState();
			if ( !L )
				return FillException( pei, L"RainJIT: the skin is no longer running." );

			// Item 36: the bound Lua function runs inside the page's script. quit() from it must not destroy
			// the browser that is executing this very call; the close is deferred until the guard is gone.
			ComEventGuard guard;

			const int top = lua_gettop( L );
			if ( !lua_checkstack( L, (int)argc + 4 ) )
				return FillException( pei, L"RainJIT: Lua stack overflow." );

			lua_getfield( L, LUA_REGISTRYINDEX, m_boundFuncs[id].c_str() );
			if ( !lua_isfunction( L, -1 ) ) {
				lua_settop( L, top );
				return DISP_E_MEMBERNOTFOUND;
			}

			for ( int i = (int)argc - 1; i >= 0; --i )
				luaVariant::Push( L, pdp->rgvarg[i] );

			if ( lua_pcall( L, (int)argc, 1, 0 ) != LUA_OK ) {
				std::wstring text = utf8_to_wstring( luaVariant::ToDisplayString( L, -1 ) ); // item 27: never NULL
				lua_settop( L, top );
				// FIX #2: use m_ctx->rain directly - ctrl may be null if already removed from map
				Rain *logRain = m_ctx ? m_ctx->rain : nullptr;
				if ( logRain )
					RN_LOG( logRain, LOG_ERROR, ( L"[RainJIT:Trident] bound function error: " + text ).c_str() );
				return FillException( pei, text );
			}

			if ( pvarRes )
				luaVariant::From( L, -1, pvarRes );
			lua_settop( L, top ); // always drop the result, with or without a result pointer
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

		/**
		 * @brief Registers a Lua function under `window.external.<name>`.
		 * @return E_INVALIDARG for an empty or reserved name (`call` is the built-in event channel).
		 */
		HRESULT Bind( const std::string &name, lua_State *L, int funcIndex ) {
			if ( name.empty() || name == "call" )
				return E_INVALIDARG;
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
	// Interface used by the other modules (ExternalDispatch itself stays private to this file)
	// -------------------------------------------------------------------------

	void *ExternalCreate( std::shared_ptr<Context> ctx, int configId ) {
		return static_cast<ExternalDispatch *>( new ExternalDispatch( std::move( ctx ), configId ) );
	}

	void ExternalGetDispatch( void *ext, IDispatch **out ) {
		IDispatchEx *dispEx = static_cast<IDispatchEx *>( static_cast<ExternalDispatch *>( ext ) );
		*out = dispEx;
		dispEx->AddRef();
	}

	HRESULT ExternalBind( void *ext, const char *name, lua_State *L, int idx ) {
		return static_cast<ExternalDispatch *>( ext )->Bind( name, L, idx );
	}

	void ExternalShutdown( void *ext ) {
		ExternalDispatch *e = static_cast<ExternalDispatch *>( ext );
		e->CleanupBoundFunctions(); // needs ctx->alive and the control still in the map
		e->Release();
	}


} // namespace trident
