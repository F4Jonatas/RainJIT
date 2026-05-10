/**
 * @file luaVariant.hpp
 * @brief Utilities for converting between Lua types and COM VARIANTs.
 * @license GPL v2.0 License
 *
 * @details
 * Pure conversion helpers with no dependency on Rainmeter, trident, or
 * any module-specific state. Safe to include from any translation unit
 * that links against lua and the COM/ATL headers.
 */

#pragma once

#include <lua.hpp>
#include <atlbase.h>
#include <oaidl.h>
#include <string>
#include <utils/strings.hpp>

namespace luaVariant {

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
	 *   VT_I1, VT_I2, VT_I4, VT_INT  → lua_Integer
	 *   VT_UI1, VT_UI2, VT_UI4, VT_UINT → lua_Integer
	 *   VT_R4, VT_R8                   → lua_Number
	 *   VT_BSTR                        → string (UTF-8)
	 *   VT_BOOL                        → boolean
	 *   VT_DISPATCH                    → lightuserdata (raw IDispatch*)
	 *                                    Caller is responsible for wrapping.
	 *   VT_NULL, VT_EMPTY              → nil
	 *   anything else                  → string via VariantChangeType, or nil
	 *
	 * @param L  Lua state.
	 * @param v  VARIANT to convert (not modified).
	 */
	inline void Push( lua_State *L, const VARIANT &v ) {
		switch ( v.vt ) {
		case VT_I1:   lua_pushinteger( L, v.cVal );    break;
		case VT_I2:   lua_pushinteger( L, v.iVal );    break;
		case VT_I4:   lua_pushinteger( L, v.lVal );    break;
		case VT_INT:  lua_pushinteger( L, v.intVal );  break;
		case VT_UI1:  lua_pushinteger( L, v.bVal );    break;
		case VT_UI2:  lua_pushinteger( L, v.uiVal );   break;
		case VT_UI4:  lua_pushinteger( L, v.ulVal );   break;
		case VT_UINT: lua_pushinteger( L, v.uintVal ); break;
		case VT_R4:   lua_pushnumber( L, v.fltVal );   break;
		case VT_R8:   lua_pushnumber( L, v.dblVal );   break;
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
			// Raw pointer — caller wraps in ComProxy if needed.
			lua_pushlightuserdata( L, v.pdispVal );
			break;
		case VT_NULL:
		case VT_EMPTY:
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
	 * @brief Converts a Lua stack value to a VARIANT.
	 *
	 * Type mapping:
	 *   LUA_TNUMBER  → VT_R8 (double) or VT_I4 if integer-valued
	 *   LUA_TSTRING  → VT_BSTR (UTF-8 → wide)
	 *   LUA_TBOOLEAN → VT_BOOL
	 *   LUA_TNIL     → VT_NULL
	 *   anything else → VT_NULL
	 *
	 * Caller is responsible for calling VariantClear() on the result.
	 *
	 * @param L    Lua state.
	 * @param idx  Stack index.
	 * @param out  Output VARIANT (must be uninitialized or already cleared).
	 */
	inline void From( lua_State *L, int idx, VARIANT *out ) {
		VariantInit( out );
		switch ( lua_type( L, idx ) ) {
		case LUA_TNUMBER: {
			lua_Number n = lua_tonumber( L, idx );
			lua_Integer i = (lua_Integer)n;
			if ( (lua_Number)i == n ) {
				out->vt   = VT_I4;
				out->lVal = (LONG)i;
			} else {
				out->vt     = VT_R8;
				out->dblVal = (double)n;
			}
			break;
		}
		case LUA_TSTRING:
			out->vt      = VT_BSTR;
			out->bstrVal = SysAllocString( utf8_to_wstring( lua_tostring( L, idx ) ).c_str() );
			break;
		case LUA_TBOOLEAN:
			out->vt      = VT_BOOL;
			out->boolVal = lua_toboolean( L, idx ) ? VARIANT_TRUE : VARIANT_FALSE;
			break;
		default:
			out->vt = VT_NULL;
			break;
		}
	}

} // namespace luaVariant
