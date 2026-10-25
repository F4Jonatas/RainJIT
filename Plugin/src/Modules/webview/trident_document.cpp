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
 * @file trident_document.cpp
 * @brief Document level operations: write() into about:blank and script evaluation.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/// True if the control's current location is about:blank (or empty, i.e. nothing loaded yet).
	bool IsBlankLocation( Control &ctrl ) {
		if ( !ctrl.webBrowser )
			return false;
		BSTR b = nullptr;
		if ( FAILED( ctrl.webBrowser->get_LocationURL( &b ) ) )
			return false;
		std::string u = b ? wstring_to_utf8( b ) : std::string();
		if ( b )
			SysFreeString( b );
		std::transform( u.begin(), u.end(), u.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
		return u.empty() || IsAboutBlank( u );
	}

	/**
	 * @brief Writes already-sanitized HTML into the control's (about:blank) document (item 31).
	 *
	 * document.open() on a document that came from a remote URL keeps that origin, so write() only calls this
	 * for about:blank; otherwise the write is deferred until an about:blank document has been loaded.
	 *
	 * @param ctrl     Control to write into.
	 * @param content  Sanitized HTML (UTF-8).
	 * @param err      Receives a message on failure.
	 * @return true on success.
	 */
	bool WriteIntoDocument( Control &ctrl, const std::string &content, std::string &err ) {
		CComPtr<IDispatch> docDisp;
		if ( !ctrl.webBrowser || FAILED( ctrl.webBrowser->get_Document( &docDisp ) ) || !docDisp ) {
			err = "No document";
			return false;
		}
		CComQIPtr<IHTMLDocument2> htmlDoc( docDisp );
		if ( !htmlDoc ) {
			err = "Invalid document";
			return false;
		}

		// document.open() - resets the document, clears existing content.
		// Invoked via IDispatch because IHTMLDocument2::open() requires
		// several optional parameters that are inconvenient to supply via COM.
		DISPID dispOpen = 0;
		OLECHAR *openName = const_cast<OLECHAR *>( L"open" );
		if ( FAILED( docDisp->GetIDsOfNames( IID_NULL, &openName, 1, LOCALE_USER_DEFAULT, &dispOpen ) ) ) {
			err = "document.open is not available";
			return false;
		}
		DISPPARAMS noArgs = {};
		if ( FAILED( docDisp->Invoke( dispOpen, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &noArgs, nullptr, nullptr, nullptr ) ) ) {
			err = "document.open failed";
			return false;
		}

		// document.write(html) - inject content into the open stream.
		std::wstring wtext = utf8_to_wstring( content );
		CComBSTR bstrText( wtext.c_str() );
		DISPPARAMS params = {};
		VARIANTARG varg;
		VariantInit( &varg );
		varg.vt = VT_BSTR;
		varg.bstrVal = bstrText;
		params.rgvarg = &varg;
		params.cArgs = 1;
		HRESULT hrWrite = docDisp->Invoke( DISPID_IHTMLDOCUMENT2_WRITE, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, nullptr, nullptr, nullptr );
		// varg only borrows bstrText (CComBSTR owns it): do not VariantClear it.

		// document.close() - finalises the parse; MSHTML may raise documentcomplete for it. Mark it so the
		// event sink does not forward that self-generated event to Lua (item 19).
		ctrl.writePending = true;
		ctrl.writeTick = GetTickCount64();
		htmlDoc->close();

		if ( FAILED( hrWrite ) ) {
			err = "document.write failed";
			return false;
		}
		return true;
	}

	/// A script argument names a FILE only if it is a single line ending in ".js" (item 30).
	bool LooksLikeScriptFile( const std::string &s ) {
		if ( s.size() < 4 || s.size() > 1024 || s.find_first_of( "\r\n" ) != std::string::npos )
			return false;
		std::string tail = s.substr( s.size() - 3 );
		std::transform( tail.begin(), tail.end(), tail.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
		return tail == ".js";
	}

	/// Reads a property of the window through IDispatch (used by the execScript fallback).
	static bool GetWindowProperty( IDispatch *winDisp, const wchar_t *name, VARIANT *out ) {
		OLECHAR *n = const_cast<OLECHAR *>( name );
		DISPID id = DISPID_UNKNOWN;
		if ( FAILED( winDisp->GetIDsOfNames( IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id ) ) || id == DISPID_UNKNOWN )
			return false;
		DISPPARAMS none = {};
		return SUCCEEDED( winDisp->Invoke( id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &none, out, nullptr, nullptr ) );
	}

	/**
	 * @brief Runs `body` as a function body in the page and returns its typed result (item 30).
	 *
	 * Two paths, because the document mode decides what exists:
	 *  1. `window.eval` through IDispatch: real VARIANT result and EXCEPINFO errors. It exists in IE9+ document modes.
	 *  2. IE7/IE8 modes (the WebBrowser default unless FEATURE_BROWSER_EMULATION is set) have NO `window.eval`:
	 *     the code goes through IHTMLWindow2::execScript and the result is read back from a temporary
	 *     window property (`__tridentRet` / `__tridentErr`) that is removed right after.
	 *
	 * @param window  The document's window.
	 * @param body    JavaScript function body.
	 * @param out     Receives the result (VariantClear it).
	 * @param error   Receives the script error text on failure.
	 * @return true on success.
	 */
	bool EvalInWindow( IHTMLWindow2 *window, const std::wstring &body, VARIANT *out, std::wstring &error ) {
		CComQIPtr<IDispatch> winDisp( window );
		if ( !winDisp ) {
			error = L"No window";
			return false;
		}

		// ---- Path 1: window.eval ------------------------------------------------------------------
		DISPID evalId = DISPID_UNKNOWN;
		{
			OLECHAR *n = const_cast<OLECHAR *>( L"eval" );
			if ( FAILED( winDisp->GetIDsOfNames( IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &evalId ) ) )
				evalId = DISPID_UNKNOWN;
		}
		if ( evalId != DISPID_UNKNOWN ) {
			// The newline before "})()" keeps a trailing "// comment" in the script from swallowing the call.
			CComVariant arg( ( L"(function(){\n" + body + L"\n})()" ).c_str() ); // VT_BSTR
			DISPPARAMS dp = {};
			dp.rgvarg = &arg;
			dp.cArgs = 1;
			EXCEPINFO ei = {};
			UINT argErr = 0;
			HRESULT hr = winDisp->Invoke( evalId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, out, &ei, &argErr );
			if ( SUCCEEDED( hr ) )
				return true;
			if ( hr == DISP_E_EXCEPTION && ei.pfnDeferredFillIn )
				ei.pfnDeferredFillIn( &ei );
			if ( ei.bstrDescription && *ei.bstrDescription ) {
				error = ei.bstrDescription;
			} else {
				wchar_t buf[64];
				swprintf_s( buf, L"Script error (0x%08X)", (unsigned)hr );
				error = buf;
			}
			SysFreeString( ei.bstrSource );
			SysFreeString( ei.bstrDescription );
			SysFreeString( ei.bstrHelpFile );
			return false;
		}

		// ---- Path 2: execScript + temporary window property ---------------------------------------
		const std::wstring wrapped =
				L"try{window.__tridentErr='';window.__tridentRet=(function(){\n" + body +
				L"\n})();}catch(e){window.__tridentErr=String(e&&e.message?e.message:e);window.__tridentRet=null;}";
		CComBSTR bstrCode( wrapped.c_str() );
		CComBSTR bstrLang( L"JavaScript" );
		CComVariant dummy;
		HRESULT hr = window->execScript( bstrCode, bstrLang, &dummy );
		if ( FAILED( hr ) ) {
			// Typically a syntax error in the script body (the engine reports it to the host).
			wchar_t buf[64];
			swprintf_s( buf, L"Script error (0x%08X)", (unsigned)hr );
			error = buf;
			return false;
		}

		CComVariant err;
		const bool hasErr = GetWindowProperty( winDisp, L"__tridentErr", &err ) && err.vt == VT_BSTR && err.bstrVal && *err.bstrVal;
		if ( hasErr )
			error = err.bstrVal;
		else if ( !GetWindowProperty( winDisp, L"__tridentRet", out ) )
			VariantClear( out ); // nothing returned = undefined

		CComBSTR cleanup( L"window.__tridentRet=null;window.__tridentErr=null;try{delete window.__tridentRet;delete window.__tridentErr;}catch(e){}" );
		window->execScript( cleanup, bstrLang, &dummy );
		return !hasErr;
	}


} // namespace trident
