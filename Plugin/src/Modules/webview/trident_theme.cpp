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
 * @file trident_theme.cpp
 * @brief Theme (light/dark) and custom `data-*` attributes on the document's <html> element.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * The page styles itself with attribute selectors, which MSHTML supports:
 * @code{.css}
 * [data-theme="dark"] body { background: #111; }
 * [data-accent="red"] a    { color: red; }
 * @endcode
 * The module only writes the attributes (`data-theme` plus the ones of create{ data = {...} }); it never
 * touches the page's CSS. ApplyThemeData() is idempotent and runs at NavigateComplete2, DocumentComplete,
 * after write() and on redraw(). Part of the Trident module; see trident_internal.hpp.
 */

#include "trident_internal.hpp"

#pragma comment( lib, "advapi32.lib" )

namespace trident {

	namespace {

		/// Most attributes a control may carry, and the longest name / value accepted.
		const size_t kMaxDataEntries = 64;
		const size_t kMaxDataName = 64;
		const size_t kMaxDataValue = 1024;

		/// Lowercases `name`, drops an optional "data-" prefix and validates it. Empty result = invalid.
		std::string NormalizeDataName( const std::string &name ) {
			std::string n = name;
			std::transform( n.begin(), n.end(), n.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
			if ( n.compare( 0, 5, "data-" ) == 0 )
				n.erase( 0, 5 );
			if ( n.empty() || n.size() > kMaxDataName || n[0] == '.' || n[0] == '-' )
				return std::string();
			for ( unsigned char c : n ) {
				if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '.' || c == '-' ) )
					return std::string();
			}
			return n;
		}

	} // namespace

	// -------------------------------------------------------------------------
	// Theme
	// -------------------------------------------------------------------------

	/**
	 * @brief The Windows app theme: "dark" when "Choose your default app mode" is dark, otherwise "light".
	 */
	std::string WindowsTheme() {
		DWORD value = 1; // light when the value is missing (Windows 7/8 and early 10)
		DWORD size = sizeof( value );
		if ( RegGetValueW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
											 RRF_RT_REG_DWORD, nullptr, &value, &size ) != ERROR_SUCCESS )
			value = 1;
		return value == 0 ? "dark" : "light";
	}

	/// The theme the page is rendered with: the `theme` option when set, otherwise the Windows theme.
	std::string EffectiveTheme( const Control &ctrl ) {
		return ctrl.themeOption.empty() ? WindowsTheme() : ctrl.themeOption;
	}

	/// Normalizes a theme name: "light" / "dark" (case-insensitive); empty for "" or "auto"; sets `ok` = false otherwise.
	std::string NormalizeThemeName( const std::string &name, bool &ok ) {
		std::string t = name;
		std::transform( t.begin(), t.end(), t.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
		ok = true;
		if ( t == "light" || t == "dark" )
			return t;
		if ( t.empty() || t == "auto" )
			return std::string();
		ok = false;
		return std::string();
	}

	// -------------------------------------------------------------------------
	// data table <-> Lua
	// -------------------------------------------------------------------------

	void ReadDataTable( lua_State *L, int idx, std::map<std::string, std::string> &out, std::vector<std::string> &problems ) {
		if ( idx < 0 && idx > LUA_REGISTRYINDEX )
			idx = lua_gettop( L ) + idx + 1; // absolute index (Lua 5.1 / LuaJIT has no lua_absindex)
		lua_pushnil( L );
		while ( lua_next( L, idx ) != 0 ) {
			// key at -2, value at -1
			if ( lua_type( L, -2 ) != LUA_TSTRING ) {
				problems.push_back( "keys must be strings" );
				lua_pop( L, 1 );
				continue;
			}
			const std::string rawName( lua_tostring( L, -2 ), lua_objlen( L, -2 ) );
			const std::string name = NormalizeDataName( rawName );
			std::string value;
			bool valueOk = true;

			switch ( lua_type( L, -1 ) ) {
			case LUA_TSTRING:
				value.assign( lua_tostring( L, -1 ), lua_objlen( L, -1 ) );
				break;
			case LUA_TNUMBER: {
				// lua_tostring on a number would convert the VALUE in place, which breaks lua_next on a numeric key;
				// the key is a string here, so converting the value is safe, but a copy keeps the stack untouched.
				lua_pushvalue( L, -1 );
				value = lua_tostring( L, -1 );
				lua_pop( L, 1 );
				break;
			}
			case LUA_TBOOLEAN:
				value = lua_toboolean( L, -1 ) ? "true" : "false";
				break;
			default:
				valueOk = false;
				break;
			}

			if ( name.empty() )
				problems.push_back( "invalid name '" + rawName + "' (use letters, digits, '_', '.' and '-')" );
			else if ( name == "theme" )
				problems.push_back( "'theme' is reserved (use the theme option)" );
			else if ( !valueOk )
				problems.push_back( "'" + rawName + "': the value must be a string, number or boolean" );
			else if ( value.size() > kMaxDataValue )
				problems.push_back( "'" + rawName + "': the value is too long" );
			else if ( out.size() >= kMaxDataEntries && out.find( name ) == out.end() )
				problems.push_back( "too many attributes (max 64)" );
			else
				out[name] = value;

			lua_pop( L, 1 );
		}
	}

	void PushDataTable( lua_State *L, const std::map<std::string, std::string> &data ) {
		lua_createtable( L, 0, (int)data.size() );
		for ( const auto &kv : data ) {
			lua_pushlstring( L, kv.second.c_str(), kv.second.size() );
			lua_setfield( L, -2, kv.first.c_str() );
		}
	}

	// -------------------------------------------------------------------------
	// Applying to the document
	// -------------------------------------------------------------------------

	/// Forgets which attributes were written on the previous document.
	void ResetThemeState( Control &ctrl ) {
		ctrl.dataApplied.clear();
	}

	/**
	 * @brief Writes `data-theme` and the `data-*` attributes on the document's <html> element.
	 *
	 * Attributes written earlier that are no longer in `ctrl.data` are removed. Does nothing when there is no
	 * document (yet); the same state is applied when it loads. Safe to call repeatedly. Never throws.
	 */
	void ApplyThemeData( Control &ctrl ) {
		try {
			if ( !ctrl.webBrowser )
				return;
			CComPtr<IDispatch> docDisp;
			if ( FAILED( ctrl.webBrowser->get_Document( &docDisp ) ) || !docDisp )
				return;
			CComQIPtr<IHTMLDocument3> doc3( docDisp );
			if ( !doc3 )
				return;
			CComPtr<IHTMLElement> root;
			if ( FAILED( doc3->get_documentElement( &root ) ) || !root )
				return; // too early: DocumentComplete applies it again

			ReentryGuard guard;

			auto setAttr = [&]( const std::string &name, const std::string &value ) {
				CComBSTR bname( utf8_to_wstring( name ).c_str() );
				CComVariant bvalue( utf8_to_wstring( value ).c_str() );
				root->setAttribute( bname, bvalue, 0 );
			};

			setAttr( "data-theme", EffectiveTheme( ctrl ) );

			// drop what an earlier redraw() wrote and the table no longer has
			for ( const std::string &old : ctrl.dataApplied ) {
				if ( ctrl.data.find( old ) != ctrl.data.end() )
					continue;
				CComBSTR bname( utf8_to_wstring( "data-" + old ).c_str() );
				VARIANT_BOOL removed = VARIANT_FALSE;
				root->removeAttribute( bname, 0, &removed );
			}

			ctrl.dataApplied.clear();
			for ( const auto &kv : ctrl.data ) {
				setAttr( "data-" + kv.first, kv.second );
				ctrl.dataApplied.push_back( kv.first );
			}
		} catch ( ... ) {
			RN_LOG( ctrl.rain, LOG_WARNING, L"[RainJIT:Trident] applying the theme failed." );
		}
	}

} // namespace trident
