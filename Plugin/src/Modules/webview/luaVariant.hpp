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
 * @file luaVariant.hpp
 * @brief Utilities for converting between Lua types and COM VARIANTs.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * @details
 * Conversion helpers with no dependency on Rainmeter or trident. The only coupling to the rest of the
 * module is through two optional hooks (see PushDispatchHook / GetDispatchHook) that comProxy.hpp
 * installs, so IDispatch values can travel in both directions without a circular include.
 *
 * @par Lua -> VARIANT (From)
 * - number: VT_I4 when it is an integer in the 32-bit range, otherwise VT_R8 (NaN and +-Inf included).
 * - string: VT_BSTR (UTF-8 -> UTF-16, embedded NULs preserved).
 * - boolean: VT_BOOL.
 * - ComProxy userdata: VT_DISPATCH (the very same COM object, AddRef'd).
 * - function: VT_DISPATCH, a callable object (usable as an event handler or callback).
 * - table: VT_DISPATCH, a READ-ONLY object: string keys are properties, `[i]` maps to Lua index i + 1
 *   and `length` is `#t`; function fields are callable.
 * - anything else: VT_NULL.
 *
 * @par VARIANT -> Lua (Push)
 * - VT_DISPATCH / VT_UNKNOWN with IDispatch: a ComProxy (AddRef'd); nil if no proxy support is installed.
 * - 64-bit integers become lua_Number (double), VT_BYREF values are dereferenced.
 *
 * @note Lua function/table objects handed to COM keep a registry reference. Call ReleaseAll() for the
 *       lua_State before it is closed; objects still referenced by COM become inert afterwards.
 */

#pragma once

#include <lua.hpp>
#include <atlbase.h>
#include <dispex.h>
#include <oaidl.h>
#include <climits>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utils/strings.hpp>

namespace luaVariant {

	/// Hook: pushes a ComProxy for an IDispatch (AddRef'd). Installed by ComProxy::Register.
	using PushDispatchFn = void ( * )( lua_State *, IDispatch * );
	/// Hook: returns the IDispatch wrapped by the ComProxy at a stack index, or nullptr. Not AddRef'd.
	using GetDispatchFn = IDispatch *( * )( lua_State *, int );

	/// @return Reference to the process-wide "push dispatch" hook.
	inline PushDispatchFn &PushDispatchHook() {
		static PushDispatchFn fn = nullptr;
		return fn;
	}

	/// @return Reference to the process-wide "get dispatch" hook.
	inline GetDispatchFn &GetDispatchHook() {
		static GetDispatchFn fn = nullptr;
		return fn;
	}

	/// Hook pair run before / after Lua code is executed on behalf of COM (LuaObject::InvokeEx).
	/// The host module uses it to mark re-entrant frames (see ReentryGuard in trident_internal.hpp).
	using CallScopeFn = void ( * )();

	/// @return Reference to the process-wide "Lua call begins" hook.
	inline CallScopeFn &CallEnterHook() {
		static CallScopeFn fn = nullptr;
		return fn;
	}

	/// @return Reference to the process-wide "Lua call ended" hook.
	inline CallScopeFn &CallLeaveHook() {
		static CallScopeFn fn = nullptr;
		return fn;
	}

	/// RAII wrapper that runs the call hooks (a no-op when none is installed).
	struct CallScope {
		CallScope() {
			if ( CallScopeFn f = CallEnterHook() )
				f();
		}
		~CallScope() {
			if ( CallScopeFn f = CallLeaveHook() )
				f();
		}
		CallScope( const CallScope & ) = delete;
		CallScope &operator=( const CallScope & ) = delete;
	};

	/// Registry key under which the main lua_State of a Lua universe is stored.
	inline const char *MainStateKey() {
		return "trident.mainstate";
	}

	/**
	 * @brief Records `L` as the main state, so objects created from a coroutine never keep a pointer to it.
	 * @param L  The main lua_State (the one given to the module at registration time).
	 */
	inline void RegisterMainState( lua_State *L ) {
		lua_pushlightuserdata( L, L );
		lua_setfield( L, LUA_REGISTRYINDEX, MainStateKey() );
	}

	/// @return The main state registered for the universe of `L`, or `L` itself when none was registered.
	inline lua_State *MainState( lua_State *L ) {
		lua_getfield( L, LUA_REGISTRYINDEX, MainStateKey() );
		lua_State *m = static_cast<lua_State *>( lua_touserdata( L, -1 ) );
		lua_pop( L, 1 );
		return m ? m : L;
	}

	/**
	 * @brief Text for logging a Lua value that may not be a string (e.g. `error({})` or `error(nil)`).
	 *
	 * lua_tostring() returns NULL for such values; passing that to a string conversion crashes (item 27).
	 *
	 * @param L    Lua state.
	 * @param idx  Stack index of the value.
	 * @return The string/number value, or "(<type> error object)".
	 */
	inline std::string ToDisplayString( lua_State *L, int idx ) {
		size_t len = 0;
		const char *s = lua_tolstring( L, idx, &len );
		if ( s )
			return std::string( s, len );
		return std::string( "(" ) + lua_typename( L, lua_type( L, idx ) ) + " error object)";
	}

	/**
	 * @brief Coerces a VARIANT to a wide string.
	 *
	 * Attempts VT_BSTR coercion via VariantChangeType. Returns an empty
	 * wstring if conversion fails (e.g. VT_EMPTY, VT_NULL, unsupported type).
	 *
	 * @param v  Source VARIANT (read-only; a temporary copy is used internally).
	 * @return   Wide string representation, or empty string on failure.
	 */
	inline std::wstring ToWString( const VARIANT &v ) {
		CComVariant coerced;
		if ( SUCCEEDED( VariantChangeType( &coerced, const_cast<VARIANT *>( &v ), 0, VT_BSTR ) ) && coerced.bstrVal )
			return coerced.bstrVal;
		return {};
	}

	/**
	 * @brief Pushes a VARIANT value onto the Lua stack.
	 *
	 * Type mapping:
	 *   VT_I1, VT_I2, VT_I4, VT_INT, VT_UI1, VT_UI2, VT_UI4, VT_UINT -> lua_Integer
	 *   VT_I8, VT_UI8, VT_R4, VT_R8       -> lua_Number
	 *   VT_BSTR                           -> string (UTF-8)
	 *   VT_BOOL                           -> boolean
	 *   VT_DISPATCH, VT_UNKNOWN           -> ComProxy (AddRef'd) through PushDispatchHook(); nil without it
	 *   VT_NULL, VT_EMPTY, VT_ERROR       -> nil
	 *   VT_BYREF | x                      -> the referenced value
	 *   anything else                     -> string via VariantChangeType, or nil
	 *
	 * @param L  Lua state.
	 * @param v  VARIANT to convert (not modified).
	 */
	inline void Push( lua_State *L, const VARIANT &v ) {
		// Dereference VT_BYREF | VT_VARIANT (how script engines pass most arguments).
		if ( v.vt == ( VT_BYREF | VT_VARIANT ) ) {
			if ( v.pvarVal && v.pvarVal != &v )
				Push( L, *v.pvarVal );
			else
				lua_pushnil( L );
			return;
		}

		switch ( v.vt ) {
		case VT_I1:
			lua_pushinteger( L, v.cVal );
			break;
		case VT_I2:
			lua_pushinteger( L, v.iVal );
			break;
		case VT_I4:
			lua_pushinteger( L, v.lVal );
			break;
		case VT_INT:
			lua_pushinteger( L, v.intVal );
			break;
		case VT_UI1:
			lua_pushinteger( L, v.bVal );
			break;
		case VT_UI2:
			lua_pushinteger( L, v.uiVal );
			break;
		case VT_UI4:
			lua_pushinteger( L, v.ulVal );
			break;
		case VT_UINT:
			lua_pushinteger( L, v.uintVal );
			break;
		case VT_I8:
			lua_pushnumber( L, (lua_Number)v.llVal );
			break;
		case VT_UI8:
			lua_pushnumber( L, (lua_Number)v.ullVal );
			break;
		case VT_R4:
			lua_pushnumber( L, v.fltVal );
			break;
		case VT_R8:
			lua_pushnumber( L, v.dblVal );
			break;
		case VT_BSTR: {
			if ( v.bstrVal ) {
				std::string str = wstring_to_utf8( v.bstrVal );
				lua_pushlstring( L, str.c_str(), str.size() );
			} else {
				lua_pushstring( L, "" );
			}
			break;
		}
		case VT_BOOL:
			lua_pushboolean( L, v.boolVal == VARIANT_TRUE );
			break;
		case VT_DISPATCH:
		case VT_UNKNOWN: {
			// Item 26: used to push a raw, un-AddRef'd IDispatch* as lightuserdata (dangling and unusable).
			CComPtr<IDispatch> disp;
			if ( v.vt == VT_DISPATCH )
				disp = v.pdispVal;
			else if ( v.punkVal )
				v.punkVal->QueryInterface( IID_IDispatch, (void **)&disp );
			if ( disp && PushDispatchHook() )
				PushDispatchHook()( L, disp ); // AddRefs
			else
				lua_pushnil( L );
			break;
		}
		case VT_NULL:
		case VT_EMPTY:
		case VT_ERROR:
			lua_pushnil( L );
			break;
		default: {
			CComVariant coerced;
			if ( SUCCEEDED( VariantChangeType( &coerced, const_cast<VARIANT *>( &v ), 0, VT_BSTR ) ) && coerced.bstrVal ) {
				std::string str = wstring_to_utf8( coerced.bstrVal );
				lua_pushlstring( L, str.c_str(), str.size() );
			} else {
				lua_pushnil( L );
			}
			break;
		}
		}
	}

	/**
	 * @brief Fills an EXCEPINFO with a message (the caller returns DISP_E_EXCEPTION).
	 *
	 * @param pei   Structure supplied by the caller; may be null.
	 * @param text  Message, truncated to 512 characters.
	 */
	inline void FillExcepInfo( EXCEPINFO *pei, const std::wstring &text ) {
		if ( !pei )
			return;
		ZeroMemory( pei, sizeof( *pei ) );
		pei->scode = E_FAIL;
		pei->bstrSource = SysAllocString( L"RainJIT.Trident" );
		const size_t n = text.size() < 512 ? text.size() : 512;
		pei->bstrDescription = SysAllocStringLen( text.c_str(), (UINT)n );
	}

	inline void From( lua_State *L, int idx, VARIANT *out ); // defined below; LuaObject needs it

	/**
	 * @brief COM object that exposes a Lua function or table to script (item 25).
	 *
	 * Function: invoking the default member calls the function, so it can be assigned as an event
	 * handler or passed as a callback. Table: read-only object (see the file comment).
	 * Everything runs on the thread that owns the Lua state (the STA thread of the skin).
	 */
	class LuaObject : public IDispatchEx {
	public:
		/**
		 * @brief Wraps the function or table at `idx` and stores it in `out` as VT_DISPATCH.
		 * @param L    Lua state (may be a coroutine).
		 * @param idx  Stack index of a function or table.
		 * @param out  Initialized VARIANT that receives the object (refcount 1).
		 */
		static void Create( lua_State *L, int idx, VARIANT *out ) {
			lua_pushvalue( L, idx );
			const int key = luaL_ref( L, LUA_REGISTRYINDEX ); // pops the copy
			LuaObject *o = new LuaObject( MainState( L ), key, lua_type( L, idx ) == LUA_TFUNCTION );
			out->vt = VT_DISPATCH;
			out->pdispVal = static_cast<IDispatchEx *>( o );
		}

		/**
		 * @brief Makes every live object of `L` inert and drops its registry reference.
		 *
		 * Call before the lua_State is closed. COM may still hold the objects; they then fail every
		 * call with an exception instead of touching a dead state.
		 */
		static void ReleaseAll( lua_State *L ) {
			for ( LuaObject *o : Live() ) {
				if ( o->m_L == L ) {
					luaL_unref( L, LUA_REGISTRYINDEX, o->m_key );
					o->m_L = nullptr;
				}
			}
		}

		// IUnknown
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
			return InterlockedIncrement( &m_ref );
		}
		STDMETHODIMP_( ULONG ) Release() override {
			LONG r = InterlockedDecrement( &m_ref );
			if ( r == 0 ) {
				if ( m_L )
					luaL_unref( m_L, LUA_REGISTRYINDEX, m_key );
				Live().erase( this );
				delete this;
			}
			return r;
		}

		// IDispatch
		STDMETHODIMP GetTypeInfoCount( UINT *pctinfo ) override {
			if ( pctinfo )
				*pctinfo = 0;
			return S_OK;
		}
		STDMETHODIMP GetTypeInfo( UINT, LCID, ITypeInfo ** ) override {
			return E_NOTIMPL;
		}
		STDMETHODIMP GetIDsOfNames( REFIID, LPOLESTR *rgszNames, UINT cNames, LCID, DISPID *rgDispId ) override {
			if ( !rgszNames || !rgDispId )
				return E_POINTER;
			HRESULT result = S_OK;
			for ( UINT i = 0; i < cNames; ++i ) {
				rgDispId[i] = DISPID_UNKNOWN;
				if ( FAILED( LookupName( rgszNames[i], &rgDispId[i] ) ) )
					result = DISP_E_UNKNOWNNAME;
			}
			return result;
		}
		STDMETHODIMP Invoke( DISPID id, REFIID, LCID lcid, WORD flags, DISPPARAMS *pdp, VARIANT *res, EXCEPINFO *pei, UINT * ) override {
			return InvokeEx( id, lcid, flags, pdp, res, pei, nullptr );
		}

		// IDispatchEx
		STDMETHODIMP GetDispID( BSTR bstrName, DWORD, DISPID *pid ) override {
			if ( !pid )
				return E_POINTER;
			*pid = DISPID_UNKNOWN;
			return LookupName( bstrName, pid );
		}

		STDMETHODIMP InvokeEx( DISPID id, LCID, WORD flags, DISPPARAMS *pdp, VARIANT *res, EXCEPINFO *pei, IServiceProvider * ) override {
			if ( res )
				VariantInit( res );
			if ( !m_L )
				return Fail( pei, L"RainJIT: the skin that owns this Lua object is no longer running." );

			CallScope scope; // item 36: Lua code runs inside a COM call; the host defers destructive work

			const UINT argc = pdp ? pdp->cArgs : 0;
			if ( argc > kMaxArgs )
				return DISP_E_BADPARAMCOUNT;

			// ----- function object: the default member is the call
			if ( m_isFunction ) {
				if ( id != DISPID_VALUE )
					return DISP_E_MEMBERNOTFOUND;
				if ( flags & DISPATCH_METHOD )
					return CallFunction( -1, pdp, argc, res, pei );
				return Describe( res, L"[Lua function]" );
			}

			// ----- table object (read-only)
			if ( id == DISPID_VALUE )
				return Describe( res, L"[Lua table]" );
			if ( flags & ( DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF ) )
				return E_ACCESSDENIED;

			lua_State *L = m_L;
			const int top = lua_gettop( L );
			if ( !lua_checkstack( L, 8 ) )
				return Fail( pei, L"RainJIT: Lua stack overflow." );

			lua_rawgeti( L, LUA_REGISTRYINDEX, m_key );
			if ( !PushMember( L, id ) ) {
				lua_settop( L, top );
				return DISP_E_MEMBERNOTFOUND;
			}

			HRESULT hr = S_OK;
			if ( ( flags & DISPATCH_METHOD ) && lua_isfunction( L, -1 ) ) {
				hr = CallFunction( -1, pdp, argc, res, pei );
			} else if ( flags & ( DISPATCH_PROPERTYGET | DISPATCH_METHOD ) ) {
				if ( res )
					From( L, -1, res );
			} else {
				hr = DISP_E_MEMBERNOTFOUND;
			}
			lua_settop( L, top );
			return hr;
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

	private:
		static constexpr UINT kMaxArgs = 32;
		static constexpr DISPID kLengthId = 2;
		static constexpr DISPID kNameBase = 1000;
		static constexpr size_t kMaxNames = 1024;
		static constexpr DISPID kIndexBase = 0x100000; ///< DISPID of element i is kIndexBase + i.
		static constexpr unsigned long kMaxIndex = 0xFFFFF;

		LONG m_ref;
		lua_State *m_L; ///< Main state; nullptr once ReleaseAll() ran.
		int m_key; ///< Registry reference to the function/table.
		bool m_isFunction;
		std::unordered_map<std::string, DISPID> m_nameToId;
		std::unordered_map<DISPID, std::string> m_idToName;

		LuaObject( lua_State *L, int key, bool isFunction ) :
			m_ref( 1 ),
			m_L( L ),
			m_key( key ),
			m_isFunction( isFunction ) {
			Live().insert( this );
		}

		/// All live objects (single-threaded: the STA thread owns every Lua object).
		static std::unordered_set<LuaObject *> &Live() {
			static std::unordered_set<LuaObject *> s;
			return s;
		}

		static HRESULT Fail( EXCEPINFO *pei, const std::wstring &text ) {
			FillExcepInfo( pei, text );
			return DISP_E_EXCEPTION;
		}

		static HRESULT Describe( VARIANT *res, const wchar_t *text ) {
			if ( res ) {
				res->vt = VT_BSTR;
				res->bstrVal = SysAllocString( text );
			}
			return S_OK;
		}

		/// Parses a canonical decimal array index ("0", "17"); false for anything else.
		static bool ParseIndex( const std::wstring &name, unsigned long &out ) {
			if ( name.empty() || name.size() > 7 || ( name.size() > 1 && name[0] == L'0' ) )
				return false;
			unsigned long v = 0;
			for ( wchar_t c : name ) {
				if ( c < L'0' || c > L'9' )
					return false;
				v = v * 10 + (unsigned long)( c - L'0' );
			}
			if ( v > kMaxIndex )
				return false;
			out = v;
			return true;
		}

		/// Name -> DISPID for a table (functions only have the default member). Fails if the member does not exist.
		HRESULT LookupName( const wchar_t *wname, DISPID *pid ) {
			if ( m_isFunction || !m_L || !wname )
				return DISP_E_UNKNOWNNAME;
			const std::wstring w = wname;
			lua_State *L = m_L;
			const int top = lua_gettop( L );
			if ( !lua_checkstack( L, 4 ) )
				return DISP_E_UNKNOWNNAME;

			HRESULT hr = DISP_E_UNKNOWNNAME;
			unsigned long index = 0;
			if ( w == L"length" ) {
				*pid = kLengthId;
				hr = S_OK;
			} else if ( ParseIndex( w, index ) ) {
				lua_rawgeti( L, LUA_REGISTRYINDEX, m_key );
				lua_rawgeti( L, -1, (int)index + 1 );
				if ( !lua_isnil( L, -1 ) ) {
					*pid = kIndexBase + (DISPID)index;
					hr = S_OK;
				}
			} else {
				const std::string name = wstring_to_utf8( w );
				auto it = m_nameToId.find( name );
				if ( it != m_nameToId.end() ) {
					*pid = it->second;
					hr = S_OK;
				} else if ( m_nameToId.size() < kMaxNames ) {
					lua_rawgeti( L, LUA_REGISTRYINDEX, m_key );
					lua_pushlstring( L, name.c_str(), name.size() );
					lua_rawget( L, -2 ); // raw access: no metamethod can run (and raise) here
					if ( !lua_isnil( L, -1 ) ) {
						DISPID id = kNameBase + (DISPID)m_nameToId.size();
						m_nameToId[name] = id;
						m_idToName[id] = name;
						*pid = id;
						hr = S_OK;
					}
				}
			}
			lua_settop( L, top );
			return hr;
		}

		/// Pushes the table member for `id` (table is on top of the stack). Returns false if absent.
		bool PushMember( lua_State *L, DISPID id ) {
			if ( id == kLengthId ) {
				lua_pushinteger( L, (lua_Integer)lua_objlen( L, -1 ) );
				return true;
			}
			if ( id >= kIndexBase ) {
				lua_rawgeti( L, -1, (int)( id - kIndexBase ) + 1 );
				return !lua_isnil( L, -1 );
			}
			auto it = m_idToName.find( id );
			if ( it == m_idToName.end() )
				return false;
			lua_pushlstring( L, it->second.c_str(), it->second.size() );
			lua_rawget( L, -2 );
			return !lua_isnil( L, -1 );
		}

		/**
		 * @brief Calls the Lua function at stack index `fnIdx` (or the wrapped function when -1 and this
		 *        is a function object) with the COM arguments, converting the result into `res`.
		 */
		HRESULT CallFunction( int fnIdx, DISPPARAMS *pdp, UINT argc, VARIANT *res, EXCEPINFO *pei ) {
			lua_State *L = m_L;
			const int top = lua_gettop( L );
			if ( !lua_checkstack( L, (int)argc + 4 ) )
				return Fail( pei, L"RainJIT: Lua stack overflow." );

			if ( m_isFunction )
				lua_rawgeti( L, LUA_REGISTRYINDEX, m_key );
			else
				lua_pushvalue( L, fnIdx < 0 ? lua_gettop( L ) + fnIdx + 1 : fnIdx ); // function member of the table
			// COM passes arguments right-to-left.
			for ( int i = (int)argc - 1; i >= 0; --i )
				Push( L, pdp->rgvarg[i] );

			if ( lua_pcall( L, (int)argc, 1, 0 ) != LUA_OK ) {
				const std::wstring text = utf8_to_wstring( ToDisplayString( L, -1 ) );
				lua_settop( L, top );
				return Fail( pei, text );
			}
			if ( res )
				From( L, -1, res );
			lua_settop( L, top );
			return S_OK;
		}
	};

	/**
	 * @brief Releases every Lua function/table object created for COM in `L`'s universe.
	 * @param L  The main lua_State, before it is closed.
	 */
	inline void ReleaseAll( lua_State *L ) {
		LuaObject::ReleaseAll( L );
	}

	/**
	 * @brief Converts a Lua stack value to a VARIANT.
	 *
	 * Type mapping: see the file comment. Numbers that are not 32-bit integers (including NaN, +-Inf and
	 * values above 2^31) are sent as VT_R8 instead of being truncated (item 25).
	 *
	 * Caller is responsible for calling VariantClear() on the result.
	 *
	 * @param L    Lua state.
	 * @param idx  Stack index (absolute or relative).
	 * @param out  Output VARIANT (must be uninitialized or already cleared).
	 */
	inline void From( lua_State *L, int idx, VARIANT *out ) {
		VariantInit( out );
		switch ( lua_type( L, idx ) ) {
		case LUA_TNUMBER: {
			const double n = (double)lua_tonumber( L, idx );
			if ( std::isfinite( n ) && n == std::floor( n ) && n >= (double)INT_MIN && n <= (double)INT_MAX ) {
				out->vt = VT_I4;
				out->lVal = (LONG)n;
			} else {
				out->vt = VT_R8;
				out->dblVal = n;
			}
			break;
		}
		case LUA_TSTRING: {
			size_t len = 0;
			const char *s = lua_tolstring( L, idx, &len );
			const std::wstring w = utf8_to_wstring( std::string( s ? s : "", s ? len : 0 ) );
			out->vt = VT_BSTR;
			out->bstrVal = SysAllocStringLen( w.c_str(), (UINT)w.size() );
			break;
		}
		case LUA_TBOOLEAN:
			out->vt = VT_BOOL;
			out->boolVal = lua_toboolean( L, idx ) ? VARIANT_TRUE : VARIANT_FALSE;
			break;
		case LUA_TFUNCTION:
		case LUA_TTABLE:
			LuaObject::Create( L, idx, out );
			break;
		case LUA_TUSERDATA: {
			IDispatch *d = GetDispatchHook() ? GetDispatchHook()( L, idx ) : nullptr;
			if ( d ) {
				d->AddRef();
				out->vt = VT_DISPATCH;
				out->pdispVal = d;
			} else {
				out->vt = VT_NULL;
			}
			break;
		}
		default:
			out->vt = VT_NULL;
			break;
		}
	}

} // namespace luaVariant
