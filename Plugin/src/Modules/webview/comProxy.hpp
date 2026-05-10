/**
 * @file comProxy.hpp
 * @brief Generic COM IDispatch proxy for LuaJIT.
 * @license GPL v2.0 License
 *
 * @details
 * Wraps any IDispatch* in a Lua full userdata so that property access,
 * assignment, and method calls are transparently dispatched via COM
 * automation — exactly as AHK and VBScript do natively.
 *
 * @par __index strategy:
 *   1. GetIDsOfNames(key) → DISPID.
 *   2. Try DISPATCH_PROPERTYGET eagerly.
 *      a. Returns primitive (string, number, bool) → push directly.
 *      b. Returns VT_DISPATCH → wrap in new ComProxy (chaining).
 *      c. Fails → return a closure for DISPATCH_METHOD.
 *
 * @par __tostring strategy:
 *   Uses IDispatch::GetTypeInfo → ITypeInfo::GetDocumentation to obtain
 *   the real COM type name (e.g. HTMLDivElement, HTMLCollection).
 *   Falls back to "IDispatch(0x...)" if type info is unavailable.
 *
 * @par Usage from Lua:
 * @code{.lua}
 * local doc   = browser:document()
 * local el    = doc.getElementById('app')        -- method  → ComProxy
 * local title = doc.title                        -- property → string directly
 * local vol   = el.settings.volume               -- chained properties
 * doc.title   = "New title"                      -- PROPERTYPUT via __newindex
 * print(doc)  --> ComProxy(HTMLDocument)
 * print(el)   --> ComProxy(HTMLDivElement)
 * @endcode
 */

#pragma once

#include "luaVariant.hpp"
#include <lua.hpp>
#include <atlbase.h>
#include <dispex.h>
#include <mshtml.h>
#include <oaidl.h>
#include <string>
#include <vector>
#include <utils/strings.hpp>

namespace trident {

	/// Lua metatable name for ComProxy userdata objects.
	static constexpr const char *COM_PROXY_MT = "trident.ComProxy";

	/// Internal userdata layout.
	struct ComProxyData {
		IDispatch *disp; ///< Owning reference (AddRef on Push, Release on __gc).
	};

	// Forward declaration so __index can create child proxies.
	namespace ComProxy {
		void Push( lua_State *L, IDispatch *disp );
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	/// Returns the IDispatch* from a ComProxy userdata, or nullptr.
	static inline IDispatch *CheckProxy( lua_State *L, int idx ) {
		void *ud = lua_touserdata( L, idx );
		if ( !ud ) return nullptr;
		if ( !lua_getmetatable( L, idx ) ) return nullptr;
		lua_getfield( L, LUA_REGISTRYINDEX, COM_PROXY_MT );
		bool ok = lua_rawequal( L, -1, -2 );
		lua_pop( L, 2 );
		return ok ? static_cast<ComProxyData *>( ud )->disp : nullptr;
	}

	/// Resolves a named DISPID. Returns DISPID_UNKNOWN on failure.
	static inline DISPID GetDispID( IDispatch *disp, const wchar_t *name ) {
		DISPID   id = DISPID_UNKNOWN;
		LPOLESTR n  = const_cast<LPOLESTR>( name );
		disp->GetIDsOfNames( IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id );
		return id;
	}

	/**
	 * @brief Resolves a human-readable type name for an IDispatch*.
	 *
	 * Priority:
	 *   1. IHTMLElement::tagName  → "HTMLElement(DIV)", "HTMLElement(OBJECT)", etc.
	 *   2. ITypeInfo::GetDocumentation → COM type name (HTMLDocument, HTMLCollection, etc.)
	 *   3. Fallback → "IDispatch:0x..."
	 */
	static inline std::string GetTypeName( IDispatch *disp ) {
		// 1. Try IHTMLElement — gives us the real tag name
		CComQIPtr<IHTMLElement> htmlEl( disp );
		if ( htmlEl ) {
			BSTR bstrTag = nullptr;
			if ( SUCCEEDED( htmlEl->get_tagName( &bstrTag ) ) && bstrTag ) {
				std::string tag = wstring_to_utf8( bstrTag );
				SysFreeString( bstrTag );
				return "HTMLElement(" + tag + ")";
			}
		}

		// 2. Try ITypeInfo — works for HTMLDocument, HTMLCollection, etc.
		CComPtr<ITypeInfo> typeInfo;
		if ( SUCCEEDED( disp->GetTypeInfo( 0, LOCALE_USER_DEFAULT, &typeInfo ) ) && typeInfo ) {
			BSTR bstrName = nullptr;
			if ( SUCCEEDED( typeInfo->GetDocumentation( MEMBERID_NIL, &bstrName, nullptr, nullptr, nullptr ) ) && bstrName ) {
				std::string name = wstring_to_utf8( bstrName );
				SysFreeString( bstrName );
				// Skip JScriptTypeInfo — it's the script engine wrapper, not useful
				if ( name != "JScriptTypeInfo" )
					return name;
			}
		}

		return {};
	}

	/**
	 * @brief Pushes a VARIANT result onto the Lua stack.
	 *
	 * VT_DISPATCH values are wrapped in a new ComProxy for transparent chaining.
	 * All other types are pushed via luaVariant::Push.
	 */
	static inline void PushVariantResult( lua_State *L, const VARIANT &v ) {
		if ( v.vt == VT_DISPATCH && v.pdispVal )
			ComProxy::Push( L, v.pdispVal );
		else
			luaVariant::Push( L, v );
	}

	// -------------------------------------------------------------------------
	// Metamethods
	// -------------------------------------------------------------------------

	/// __gc — releases the IDispatch reference.
	static int proxy_gc( lua_State *L ) {
		ComProxyData *d = static_cast<ComProxyData *>( lua_touserdata( L, 1 ) );
		if ( d && d->disp ) {
			d->disp->Release();
			d->disp = nullptr;
		}
		return 0;
	}

	/**
	 * @brief __tostring — shows the real COM type name for easy debugging.
	 *
	 * Examples:
	 *   ComProxy(HTMLDocument)
	 *   ComProxy(HTMLDivElement)
	 *   ComProxy(HTMLCollection)
	 *   ComProxy(IDispatch:0x00a1b2c3)  ← fallback when TypeInfo unavailable
	 */
	static int proxy_tostring( lua_State *L ) {
		ComProxyData *d = static_cast<ComProxyData *>( lua_touserdata( L, 1 ) );
		if ( !d || !d->disp ) {
			lua_pushstring( L, "ComProxy(null)" );
			return 1;
		}

		std::string typeName = GetTypeName( d->disp );
		if ( !typeName.empty() ) {
			lua_pushfstring( L, "ComProxy(%s)", typeName.c_str() );
		} else {
			char buf[64];
			snprintf( buf, sizeof( buf ), "ComProxy(IDispatch:%p)", (void *)d->disp );
			lua_pushstring( L, buf );
		}
		return 1;
	}

	/**
	 * @brief __index — intercepts property and method access.
	 *
	 * Uses IDispatchEx::GetMemberProperties to distinguish properties from
	 * methods without triggering COM Invoke — avoiding script errors on pure
	 * methods (e.g. getElementById) and ActiveX objects (e.g. WMP).
	 *
	 * Falls back to ITypeInfo::GetFuncDesc if IDispatchEx is unavailable.
	 * If neither is available, returns a closure (safe default).
	 *
	 * Strategy:
	 *   canGet && !canCall  → confirmed property → PROPERTYGET eagerly
	 *   canCall             → method             → return closure
	 *   unknown             → return closure (safe, no script errors)
	 */
	static int proxy_index( lua_State *L ) {
		IDispatch *disp = CheckProxy( L, 1 );
		if ( !disp ) { lua_pushnil( L ); return 1; }

		const char  *key  = luaL_checkstring( L, 2 );
		std::wstring wkey = utf8_to_wstring( key );

		DISPID id = GetDispID( disp, wkey.c_str() );
		if ( id == DISPID_UNKNOWN ) {
			lua_pushnil( L );
			return 1;
		}

		bool isConfirmedProperty = false;

		// Strategy 1: IDispatchEx::GetMemberProperties — no Invoke, no script errors.
		// Works on MSHTML elements and most ActiveX objects.
		CComQIPtr<IDispatchEx> dispEx( disp );
		if ( dispEx ) {
			DWORD props = 0;
			HRESULT hr  = dispEx->GetMemberProperties(
				id, fdexPropCanGet | fdexPropCanCall, &props );
			if ( SUCCEEDED( hr ) ) {
				bool canGet  = ( props & fdexPropCanGet  ) != 0;
				bool canCall = ( props & fdexPropCanCall ) != 0;
				isConfirmedProperty = canGet && !canCall;
			}
		} else {
			// Strategy 2: ITypeInfo::GetFuncDesc — fallback for non-IDispatchEx objects.
			CComPtr<ITypeInfo> typeInfo;
			if ( SUCCEEDED( disp->GetTypeInfo( 0, LOCALE_USER_DEFAULT, &typeInfo ) ) && typeInfo ) {
				TYPEATTR *typeAttr = nullptr;
				if ( SUCCEEDED( typeInfo->GetTypeAttr( &typeAttr ) ) && typeAttr ) {
					for ( UINT i = 0; i < typeAttr->cFuncs; ++i ) {
						FUNCDESC *funcDesc = nullptr;
						if ( SUCCEEDED( typeInfo->GetFuncDesc( i, &funcDesc ) ) && funcDesc ) {
							if ( funcDesc->memid == id ) {
								isConfirmedProperty = ( funcDesc->invkind == INVOKE_PROPERTYGET );
								typeInfo->ReleaseFuncDesc( funcDesc );
								break;
							}
							typeInfo->ReleaseFuncDesc( funcDesc );
						}
					}
					typeInfo->ReleaseTypeAttr( typeAttr );
				}
			}
		}

		if ( isConfirmedProperty ) {
			DISPPARAMS noArgs = {};
			CComVariant result;
			HRESULT hr = disp->Invoke( id, IID_NULL, LOCALE_USER_DEFAULT,
			                           DISPATCH_PROPERTYGET, &noArgs,
			                           &result, nullptr, nullptr );
			if ( SUCCEEDED( hr ) ) {
				PushVariantResult( L, result );
				return 1;
			}
		}

		// Not a confirmed property — return a callable closure.
		// ComProxy as upvalue ensures Release() on GC.
		// When called with 0 args, tries PROPERTYGET first (handles ActiveX
		// objects like WMP that lack IDispatchEx/ITypeInfo metadata).
		// When called with N args, goes directly to DISPATCH_METHOD.
		ComProxy::Push( L, disp );
		lua_pushinteger( L, id );
		lua_pushcclosure( L, []( lua_State *L ) -> int {
			IDispatch *d  = CheckProxy( L, lua_upvalueindex( 1 ) );
			DISPID    did = (DISPID)lua_tointeger( L, lua_upvalueindex( 2 ) );
			if ( !d ) { lua_pushnil( L ); return 1; }

			int nargs = lua_gettop( L );

			// 0 args — try PROPERTYGET first (e.g. WMP .settings, .controls).
			if ( nargs == 0 ) {
				DISPPARAMS noArgs = {};
				CComVariant result;
				if ( SUCCEEDED( d->Invoke( did, IID_NULL, LOCALE_USER_DEFAULT,
				                           DISPATCH_PROPERTYGET, &noArgs,
				                           &result, nullptr, nullptr ) ) ) {
					PushVariantResult( L, result );
					return 1;
				}
			}

			// N args or PROPERTYGET failed — method call.
			std::vector<VARIANT> vargs( nargs );
			for ( int i = 0; i < nargs; ++i )
				luaVariant::From( L, i + 1, &vargs[nargs - 1 - i] );

			DISPPARAMS dp = {};
			dp.rgvarg     = vargs.empty() ? nullptr : vargs.data();
			dp.cArgs      = (UINT)nargs;

			CComVariant result;
			HRESULT hr = d->Invoke( did, IID_NULL, LOCALE_USER_DEFAULT,
			                        DISPATCH_METHOD | DISPATCH_PROPERTYGET,
			                        &dp, &result, nullptr, nullptr );

			for ( auto &v : vargs ) VariantClear( &v );

			if ( FAILED( hr ) ) { lua_pushnil( L ); return 1; }

			PushVariantResult( L, result );
			return 1;
		}, 2 );

		return 1;
	}

	/**
	 * @brief __newindex — intercepts property assignment (DISPATCH_PROPERTYPUT).
	 */
	static int proxy_newindex( lua_State *L ) {
		IDispatch *disp = CheckProxy( L, 1 );
		if ( !disp ) return 0;

		const char  *key  = luaL_checkstring( L, 2 );
		std::wstring wkey = utf8_to_wstring( key );

		DISPID id = GetDispID( disp, wkey.c_str() );
		if ( id == DISPID_UNKNOWN ) return 0;

		VARIANT varg;
		luaVariant::From( L, 3, &varg );

		DISPID      namedArg = DISPID_PROPERTYPUT;
		DISPPARAMS  dp       = {};
		dp.rgvarg            = &varg;
		dp.cArgs             = 1;
		dp.rgdispidNamedArgs = &namedArg;
		dp.cNamedArgs        = 1;

		disp->Invoke( id, IID_NULL, LOCALE_USER_DEFAULT,
		              DISPATCH_PROPERTYPUT, &dp, nullptr, nullptr, nullptr );

		VariantClear( &varg );
		return 0;
	}

	/**
	 * @brief __call — invokes the proxy itself as a callable object (DISPID_VALUE).
	 *
	 * First arg (self) is the proxy, remaining args are forwarded as COM args.
	 */
	static int proxy_call( lua_State *L ) {
		IDispatch *disp = CheckProxy( L, 1 );
		if ( !disp ) { lua_pushnil( L ); return 1; }

		int nargs = lua_gettop( L ) - 1; // skip self

		std::vector<VARIANT> vargs( nargs );
		for ( int i = 0; i < nargs; ++i )
			luaVariant::From( L, i + 2, &vargs[nargs - 1 - i] );

		DISPPARAMS dp = {};
		dp.rgvarg     = vargs.empty() ? nullptr : vargs.data();
		dp.cArgs      = (UINT)nargs;

		CComVariant result;
		HRESULT hr = disp->Invoke( DISPID_VALUE, IID_NULL, LOCALE_USER_DEFAULT,
		                           DISPATCH_METHOD | DISPATCH_PROPERTYGET,
		                           &dp, &result, nullptr, nullptr );

		for ( auto &v : vargs ) VariantClear( &v );

		if ( FAILED( hr ) ) { lua_pushnil( L ); return 1; }

		PushVariantResult( L, result );
		return 1;
	}

	// -------------------------------------------------------------------------
	// Public API
	// -------------------------------------------------------------------------

	namespace ComProxy {

		/**
		 * @brief Registers the ComProxy metatable in the Lua registry.
		 *
		 * Must be called once per lua_State before any Push() call.
		 * Idempotent — safe to call multiple times.
		 */
		inline void Register( lua_State *L ) {
			if ( luaL_newmetatable( L, COM_PROXY_MT ) ) {
				lua_pushcfunction( L, proxy_gc );        lua_setfield( L, -2, "__gc" );
				lua_pushcfunction( L, proxy_tostring );  lua_setfield( L, -2, "__tostring" );
				lua_pushcfunction( L, proxy_index );     lua_setfield( L, -2, "__index" );
				lua_pushcfunction( L, proxy_newindex );  lua_setfield( L, -2, "__newindex" );
				lua_pushcfunction( L, proxy_call );      lua_setfield( L, -2, "__call" );
			}
			lua_pop( L, 1 );
		}

		/**
		 * @brief Wraps an IDispatch* in a ComProxy and pushes it onto the stack.
		 *
		 * Calls AddRef() on disp. The proxy __gc calls Release().
		 * Pushes nil if disp is nullptr.
		 */
		inline void Push( lua_State *L, IDispatch *disp ) {
			if ( !disp ) { lua_pushnil( L ); return; }

			disp->AddRef();

			ComProxyData *d = static_cast<ComProxyData *>(
				lua_newuserdata( L, sizeof( ComProxyData ) ) );
			d->disp = disp;

			luaL_getmetatable( L, COM_PROXY_MT );
			lua_setmetatable( L, -2 );
		}

	} // namespace ComProxy

} // namespace trident
