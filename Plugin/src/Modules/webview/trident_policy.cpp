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
 * @file trident_policy.cpp
 * @brief URL/origin policy: trusted origins, navigation guard, VBScript file and document scans.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief Case-folds a path for comparison. BOTH sides of every path comparison
	 *        (skin folder and document URL) go through this one function, so
	 *        non-ASCII folder names fold identically on each side.
	 */
	std::wstring FoldPath( std::wstring s ) {
		std::replace( s.begin(), s.end(), L'\\', L'/' );
		if ( !s.empty() )
			CharLowerBuffW( &s[0], static_cast<DWORD>( s.size() ) );
		return s;
	}

	/**
	 * @brief "file:///C:/a%20b/c.html?x#y" -> L"c:/a b/c.html" (folded, '/' separators).
	 *
	 * Takes the ORIGINAL-case URL: bytes are percent-decoded first and converted from
	 * UTF-8 to UTF-16 before folding, so the result is comparable with FoldPath( skin ).
	 *
	 * Returns empty if the URL is not "file:///<path>", or if the decoded path contains
	 * NUL/control characters, an empty host-like start ("//"), or any ".." segment
	 * (also after decoding "%2e%2e" or "%5c", and including "..." / ".. " forms that
	 * Windows would normalise).
	 */
	std::wstring FileUrlToFoldedPath( const std::string &url ) {
		if ( url.size() < 9 )
			return {};
		std::string head = url.substr( 0, 8 );
		std::transform( head.begin(), head.end(), head.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
		if ( head != "file:///" )
			return {};

		std::string raw = url.substr( 8 );
		size_t cut = raw.find_first_of( "?#" );
		if ( cut != std::string::npos )
			raw.resize( cut );

		auto hexVal = []( char c ) -> int {
			if ( c >= '0' && c <= '9' )
				return c - '0';
			if ( c >= 'a' && c <= 'f' )
				return c - 'a' + 10;
			if ( c >= 'A' && c <= 'F' )
				return c - 'A' + 10;
			return -1;
		};

		std::string dec;
		dec.reserve( raw.size() );
		for ( size_t i = 0; i < raw.size(); ++i ) {
			unsigned char c = (unsigned char)raw[i];
			if ( c == '%' && i + 2 < raw.size() && hexVal( raw[i + 1] ) >= 0 && hexVal( raw[i + 2] ) >= 0 ) {
				c = (unsigned char)( ( hexVal( raw[i + 1] ) << 4 ) | hexVal( raw[i + 2] ) );
				i += 2;
			}
			if ( c < 0x20 || c == 0x7F )
				return {}; // NUL, CR/LF, other controls (also when percent-encoded)
			dec += ( c == '\\' ) ? '/' : (char)c; // after decoding: "%5c" counts as a separator too
		}

		if ( dec.empty() || dec[0] == '/' )
			return {}; // "file://///server/share" (UNC) and empty paths

		// Reject parent-directory segments, including forms Windows trims ("..", "...", ".. ").
		size_t start = 0;
		while ( start <= dec.size() ) {
			size_t end = dec.find( '/', start );
			if ( end == std::string::npos )
				end = dec.size();
			std::string seg = dec.substr( start, end - start );
			while ( !seg.empty() && seg.back() == ' ' )
				seg.pop_back();
			if ( seg.size() >= 2 && seg.find_first_not_of( '.' ) == std::string::npos )
				return {};
			start = end + 1;
		}

		return FoldPath( utf8_to_wstring( dec ) );
	}

	/**
	 * @brief Parses the origin (scheme://host[:port]) at the start of an http(s) URL.
	 *
	 * Mirrors what MSHTML does: backslashes count as slashes, the scheme and host are
	 * case-insensitive, and an absent port means 80/443. A URL with userinfo
	 * ("https://trusted.com@evil.com/"), whitespace/control characters in the authority,
	 * a bad port or a non-ASCII host is invalid, so it never matches a trusted origin.
	 *
	 * @param requireNoPath  true when parsing configuration: only "scheme://host[:port][/]" is accepted.
	 */
	OriginParts ParseOrigin( std::string url, bool requireNoPath ) {
		OriginParts o;
		std::replace( url.begin(), url.end(), '\\', '/' );
		std::transform( url.begin(), url.end(), url.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );

		size_t sep = url.find( "://" );
		if ( sep == std::string::npos )
			return o;
		std::string scheme = url.substr( 0, sep );
		if ( scheme != "http" && scheme != "https" )
			return o;

		size_t aStart = sep + 3;
		size_t aEnd = url.find_first_of( "/?#", aStart );
		std::string authority = url.substr( aStart, aEnd == std::string::npos ? std::string::npos : aEnd - aStart );

		if ( requireNoPath && aEnd != std::string::npos && url.compare( aEnd, std::string::npos, "/" ) != 0 )
			return o;
		if ( authority.empty() )
			return o;
		for ( unsigned char c : authority ) {
			if ( c <= 0x20 || c >= 0x7F || c == '@' || c == '%' )
				return o;
		}

		std::string host = authority;
		int port = ( scheme == "https" ) ? 443 : 80;

		if ( authority[0] == '[' ) {
			size_t close = authority.find( ']' );
			if ( close == std::string::npos || close < 2 )
				return o;
			host = authority.substr( 0, close + 1 );
			std::string rest = authority.substr( close + 1 );
			if ( !rest.empty() ) {
				if ( rest[0] != ':' )
					return o;
				rest.erase( 0, 1 );
				if ( !rest.empty() ) {
					if ( rest.size() > 5 || rest.find_first_not_of( "0123456789" ) != std::string::npos )
						return o;
					port = std::atoi( rest.c_str() );
				}
			}
			for ( size_t i = 1; i + 1 < host.size(); ++i ) {
				unsigned char c = host[i];
				if ( !( std::isxdigit( c ) || c == ':' || c == '.' ) )
					return o;
			}
		} else {
			size_t colon = authority.rfind( ':' );
			if ( colon != std::string::npos ) {
				host = authority.substr( 0, colon );
				std::string ps = authority.substr( colon + 1 );
				if ( !ps.empty() ) {
					if ( ps.size() > 5 || ps.find_first_not_of( "0123456789" ) != std::string::npos )
						return o;
					port = std::atoi( ps.c_str() );
				}
			}
			while ( !host.empty() && host.back() == '.' )
				host.pop_back();
			for ( unsigned char c : host ) {
				if ( !( std::isalnum( c ) || c == '.' || c == '-' || c == '_' ) )
					return o;
			}
		}

		if ( host.empty() || port < 1 || port > 65535 )
			return o;

		o.scheme = scheme;
		o.host = host;
		o.port = port;
		o.valid = true;
		return o;
	}

	/// "about:blank" itself, optionally followed by a fragment/query. "about:blankx" is NOT about:blank.
	bool IsAboutBlank( const std::string &lowerUrl ) {
		static const char kBlank[] = "about:blank";
		const size_t n = sizeof( kBlank ) - 1;
		if ( lowerUrl.compare( 0, n, kBlank ) != 0 )
			return false;
		return lowerUrl.size() == n || lowerUrl[n] == '#' || lowerUrl[n] == '?';
	}

	/**
	 * @brief Is this URL allowed to use window.external?
	 *
	 * Trusted: about:blank (only when allowAboutBlank; an empty URL means no document has
	 * been loaded yet), local files inside the skin folder, and origins in
	 * Control::externalOrigins (canonical "scheme://host[:port]", compared as parsed
	 * origins, not as string prefixes). Remote pages get no bridge.
	 */
	static bool IsUrlTrusted( Control *ctrl, const std::string &url, bool allowAboutBlank ) {
		std::string lower = url;
		std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );

		if ( allowAboutBlank && ( lower.empty() || IsAboutBlank( lower ) ) )
			return true;
		if ( lower.empty() )
			return false;

		if ( lower.compare( 0, 8, "file:///" ) == 0 ) {
			if ( !ctrl->rain )
				return false;
			std::wstring skin = FoldPath( ctrl->rain->absPath( L"#CURRENTPATH#" ) );
			if ( skin.empty() )
				return false;
			if ( skin.back() != L'/' )
				skin += L'/';
			std::wstring path = FileUrlToFoldedPath( url ); // original case: decoded, then folded like `skin`
			return !path.empty() && path.compare( 0, skin.size(), skin ) == 0;
		}

		OriginParts cur = ParseOrigin( url, false );
		if ( !cur.valid )
			return false;
		const std::string curOrigin = cur.canonical();
		for ( const std::string &origin : ctrl->externalOrigins ) {
			if ( !origin.empty() && origin == curOrigin )
				return true;
		}
		return false;
	}

	/// Same COM object? (QueryInterface to IUnknown on both sides, as COM identity requires.)
	bool SameObject( IUnknown *a, IUnknown *b ) {
		if ( !a || !b )
			return false;
		CComPtr<IUnknown> ua, ub;
		a->QueryInterface( IID_IUnknown, (void **)&ua );
		b->QueryInterface( IID_IUnknown, (void **)&ub );
		return ua && ub && ua.IsEqualObject( ub );
	}

	/**
	 * @brief Is the document hosted by `win` (the CALLER of an external call) trusted?
	 *
	 * Item 5: window.external used to be gated only on the top-level URL, so a third-party
	 * <iframe> inside a trusted page could call it. Now the frame that actually performs the
	 * call must be trusted too. A non-top frame is never trusted just because it is
	 * about:blank: a hostile frame can create about:blank children that inherit ITS origin.
	 */
	static bool IsWindowTrusted( Control *ctrl, IHTMLWindow2 *win ) {
		if ( !win )
			return false;

		CComPtr<IHTMLDocument2> doc;
		if ( FAILED( win->get_document( &doc ) ) || !doc )
			return false;
		BSTR bUrl = nullptr;
		if ( FAILED( doc->get_URL( &bUrl ) ) )
			return false;
		std::string url = bUrl ? wstring_to_utf8( bUrl ) : std::string();
		if ( bUrl )
			SysFreeString( bUrl );

		CComPtr<IHTMLWindow2> top;
		bool isTop = SUCCEEDED( win->get_top( &top ) ) && top && SameObject( win, top );
		return IsUrlTrusted( ctrl, url, isTop );
	}

	/**
	 * @brief Decides whether window.external may be used right now.
	 *
	 * The top-level document must be trusted (IsUrlTrusted). If the caller is known
	 * (IServiceProvider handed to IDispatchEx::InvokeEx), the calling frame must be trusted as
	 * well. If MSHTML gives no caller, only the top-level check applies.
	 */
	bool IsOriginTrusted( Control *ctrl, IServiceProvider *pspCaller ) {
		if ( !ctrl || !ctrl->externalEnabled || !ctrl->webBrowser )
			return false;

		BSTR b = nullptr;
		if ( FAILED( ctrl->webBrowser->get_LocationURL( &b ) ) )
			return false;
		std::string url = b ? wstring_to_utf8( b ) : std::string();
		if ( b )
			SysFreeString( b );

		if ( !IsUrlTrusted( ctrl, url, true ) )
			return false;
		if ( !pspCaller )
			return true;

		// SID_SHTMLWindow == IID_IHTMLWindow2: the window of the script that is calling us.
		CComPtr<IHTMLWindow2> callerWin;
		if ( FAILED( pspCaller->QueryService( IID_IHTMLWindow2, IID_IHTMLWindow2, (void **)&callerWin ) ) || !callerWin )
			return false; // fail closed: a caller was announced but cannot be identified
		return IsWindowTrusted( ctrl, callerWin );
	}

	// -------------------------------------------------------------------------
	// Navigation policy helpers (items 4 and 7)
	// -------------------------------------------------------------------------

	/**
	 * @brief Unconditional VBScript guard for local documents: true if the file must not be loaded.
	 *
	 * Scans web-like files (and extension-less ones up to 1 MiB) for any spelling of VBScript and
	 * refuses script-host/HTA/HTC/VBS files outright. Files above 64 MiB are refused (fail closed).
	 * Limitation: the file can change between this scan and the load; the engine-level block
	 * (BlockVbScriptEngine) and the DOM tripwire cover that gap. Missing files return false so the
	 * browser reports its normal "not found" error.
	 */
	static bool LocalFileHasVbScript( const std::string &fileUrl ) {
		std::wstring path = FileUrlToFoldedPath( fileUrl );
		if ( path.empty() )
			return true; // unparsable / UNC / traversal: refuse

		size_t dot = path.find_last_of( L'.' );
		size_t slash = path.find_last_of( L'/' );
		std::wstring ext = ( dot != std::wstring::npos && ( slash == std::wstring::npos || dot > slash ) ) ? path.substr( dot ) : std::wstring();

		static const wchar_t *kBlockedExt[] = { L".vbs", L".vbe", L".wsf", L".wsh", L".wsc", L".hta", L".htc", L".sct" };
		for ( const wchar_t *b : kBlockedExt ) {
			if ( ext == b )
				return true;
		}

		static const wchar_t *kScanExt[] = { L".htm", L".html", L".xhtml", L".xht", L".shtml", L".mht", L".mhtml", L".svg", L".xml", L".xsl", L".xslt", L".txt", L".js" };
		bool scan = ext.empty();
		for ( const wchar_t *e : kScanExt ) {
			if ( ext == e )
				scan = true;
		}
		if ( !scan )
			return false;

		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if ( !GetFileAttributesExW( path.c_str(), GetFileExInfoStandard, &fad ) || ( fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			return false;
		ULONGLONG size = ( (ULONGLONG)fad.nFileSizeHigh << 32 ) | fad.nFileSizeLow;
		if ( size > 64ull * 1024 * 1024 )
			return true;
		if ( ext.empty() && size > 1024 * 1024 )
			return false;

		std::string content;
		if ( !fs::ReadTextFile( path, content ) )
			return true; // exists but cannot be read for the scan: refuse
		return sanitize::ContainsVbScript( content );
	}

	/// Defense in depth: does the loaded document contain a VBScript <script>? (also catches run-time created ones)
	bool DocumentUsesVbScript( IHTMLDocument2 *doc ) {
		if ( !doc )
			return false;
		CComPtr<IHTMLElementCollection> scripts;
		if ( FAILED( doc->get_scripts( &scripts ) ) || !scripts )
			return false;
		long n = 0;
		scripts->get_length( &n );
		if ( n > 1000 )
			n = 1000;
		for ( long i = 0; i < n; ++i ) {
			CComVariant idx( i );
			CComPtr<IDispatch> disp;
			if ( FAILED( scripts->item( idx, idx, &disp ) ) || !disp )
				continue;
			CComQIPtr<IHTMLScriptElement> se( disp );
			if ( !se )
				continue;
			// IHTMLScriptElement has no `language` property: read it as a plain attribute.
			CComBSTR type, src;
			se->get_type( &type );
			se->get_src( &src );
			std::wstring lang;
			CComQIPtr<IHTMLElement> el( disp );
			if ( el ) {
				CComVariant vLang;
				CComBSTR attrName( L"language" );
				if ( SUCCEEDED( el->getAttribute( attrName, 0, &vLang ) ) )
					lang = luaVariant::ToWString( vLang );
			}
			if ( !lang.empty() && sanitize::ContainsVbScript( wstring_to_utf8( lang ) ) )
				return true;
			const BSTR values[2] = { type.m_str, src.m_str };
			for ( BSTR v : values ) {
				if ( v && sanitize::ContainsVbScript( wstring_to_utf8( v ) ) )
					return true;
			}
		}
		return false;
	}

	/**
	 * @brief Scheme/host policy for a navigation target (top level or frame).
	 *
	 * Uses the same validator as navigate()/create(): javascript:, vbscript:, data:, UNC
	 * (`\\host\x`, `//host`), file://host and (unless allow_local) file:/// are refused.
	 * about:blank and MSHTML's own error pages (res://ieframe.dll/) are always allowed.
	 * With `allow_urls` (VALIDATE_URLS cleared) nothing is checked.
	 */
	bool IsNavigationAllowed( const Control &ctrl, const std::wstring &wurl ) {
		std::string url = wstring_to_utf8( wurl );
		std::string lower = url;
		std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );

		// Unconditional (independent of sanitize / allow_urls): never VBScript.
		if ( sanitize::IsVbScriptUrl( url ) )
			return false;
		if ( lower.compare( 0, 8, "file:///" ) == 0 && LocalFileHasVbScript( url ) )
			return false;

		if ( !( ctrl.sanitizeFlags & VALIDATE_URLS ) )
			return true;
		if ( lower.empty() || IsAboutBlank( lower ) )
			return true;
		if ( lower.compare( 0, 18, "res://ieframe.dll/" ) == 0 )
			return true;
		return sanitize::IsUrlSafe( url, ( ctrl.sanitizeFlags & ALLOW_LOCAL ) != 0 );
	}


} // namespace trident
