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
 * @file trident_url.cpp
 * @brief Resolution of user supplied locations (URLs, host:port, paths) into Navigate2 URLs.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/// How a user supplied location string is interpreted by ResolveUrl() (item 28).
	enum class LocationKind {
		Url, ///< Has a scheme: used as is.
		HostPort, ///< "host:port[/path]" without a scheme: http:// is assumed.
		Path, ///< File system path: drive letter, UNC, relative, "./" or with Rainmeter variables.
	};

	/// True for "host:port", "ipv4:port" or "[ipv6]:port", optionally followed by "/path", "?query" or "#fragment".
	static bool IsHostPort( const std::string &s ) {
		size_t i = 0;
		if ( !s.empty() && s[0] == '[' ) {
			const size_t close = s.find( ']' );
			if ( close == std::string::npos )
				return false;
			i = close + 1;
		} else {
			while ( i < s.size() && ( std::isalnum( (unsigned char)s[i] ) || s[i] == '.' || s[i] == '-' || s[i] == '_' ) )
				++i;
			if ( i < 2 )
				return false; // empty host, or a single letter (a drive)
		}
		if ( i >= s.size() || s[i] != ':' )
			return false;
		size_t j = i + 1;
		while ( j < s.size() && std::isdigit( (unsigned char)s[j] ) )
			++j;
		const size_t digits = j - ( i + 1 );
		return digits >= 1 && digits <= 5 && ( j == s.size() || s[j] == '/' || s[j] == '?' || s[j] == '#' );
	}

	/**
	 * @brief Classifies a location string.
	 *
	 * Fixes: "C:\x\y.html" was taken for a scheme "C" (and then blocked), and "127.0.0.1:8080" /
	 * "localhost:3000" were taken for relative paths.
	 *
	 * @param s  Location, UTF-8.
	 */
	static LocationKind ClassifyLocation( const std::string &s ) {
		if ( s.size() >= 2 && ( ( s[0] == '\\' && s[1] == '\\' ) || ( s[0] == '/' && s[1] == '/' ) ) )
			return LocationKind::Path; // UNC / network path
		const size_t colon = s.find( ':' );
		if ( colon == std::string::npos || colon == 0 )
			return LocationKind::Path;
		if ( colon == 1 && std::isalpha( (unsigned char)s[0] ) )
			return LocationKind::Path; // drive letter
		if ( !std::isalpha( (unsigned char)s[0] ) ) {
			// "127.0.0.1:8080" and "[::1]:8080": the host does not start with a letter
			return IsHostPort( s ) ? LocationKind::HostPort : LocationKind::Path;
		}
		for ( size_t i = 1; i < colon; ++i ) {
			const unsigned char c = (unsigned char)s[i];
			if ( !std::isalnum( c ) && c != '+' && c != '-' && c != '.' )
				return LocationKind::Path;
		}

		std::string scheme = s.substr( 0, colon );
		std::transform( scheme.begin(), scheme.end(), scheme.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
		const std::string rest = s.substr( colon + 1 );
		if ( rest.compare( 0, 2, "//" ) == 0 )
			return LocationKind::Url;

		static const char *kSchemes[] = { "about", "mailto", "tel", "data", "javascript", "vbscript", "res", "view-source", "blob", "ftp", "file", "http", "https", "ms-its", "mk", "mhtml" };
		for ( const char *k : kSchemes ) {
			if ( scheme == k )
				return LocationKind::Url;
		}

		// "host:port" optionally followed by a path, query or fragment.
		size_t i = 0;
		while ( i < rest.size() && std::isdigit( (unsigned char)rest[i] ) )
			++i;
		if ( i >= 1 && i <= 5 && ( i == rest.size() || rest[i] == '/' || rest[i] == '?' || rest[i] == '#' ) )
			return LocationKind::HostPort;
		return LocationKind::Url; // unknown scheme: the URL policy decides
	}

	/**
	 * @brief Escapes only the ASCII characters that cannot appear raw in a file URL.
	 *
	 * Non-ASCII characters are left as they are (as the module always did): MSHTML hands the same text
	 * back for sub-resources, so the SkinSecurityManager still recognises them as skin files.
	 *
	 * @param in         Text to escape.
	 * @param escapeSyntax  true for the path part: also escapes '%', '#' and '?'.
	 */
	static std::wstring EscapeFileUrlPart( const std::wstring &in, bool escapeSyntax ) {
		static const wchar_t kHex[] = L"0123456789ABCDEF";
		std::wstring out;
		out.reserve( in.size() );
		for ( wchar_t c : in ) {
			const bool bad = c <= 0x20 || c == 0x7F || c == L'"' || c == L'<' || c == L'>' || c == L'\\' || c == L'^' || c == L'`' || c == L'{' || c == L'|' || c == L'}' ||
											 ( escapeSyntax && ( c == L'%' || c == L'#' || c == L'?' ) );
			if ( bad ) {
				out += L'%';
				out += kHex[( c >> 4 ) & 15];
				out += kHex[c & 15];
			} else {
				out += c;
			}
		}
		return out;
	}

	/**
	 * @brief Converts an absolute Windows path (plus optional "?query#fragment") to a file:// URL.
	 *
	 * Drive path with a space and '#' (backslashes) -> "file:///C:/a%20b/c%23d.html"
	 * UNC path (backslashes)                        -> "file://server/share/x.htm" (the URL policy refuses it)
	 * The path part has spaces, '#', '%', '?' and control characters escaped; non-ASCII text stays as is;
	 * the suffix keeps its URL syntax.
	 */
	static std::wstring LocalPathToFileUrl( const std::wstring &winPath, const std::wstring &suffix ) {
		std::wstring p = winPath;
		std::replace( p.begin(), p.end(), L'\\', L'/' );
		std::wstring url = p.compare( 0, 2, L"//" ) == 0 ? L"file:" : L"file:///";
		url += EscapeFileUrlPart( p, true );
		url += EscapeFileUrlPart( suffix, false );
		return url;
	}

	/**
	 * @brief Resolves a URL or path for use with Navigate2.
	 *
	 * - URLs with a scheme (http://, https://, about:, file://, etc.) pass through unchanged.
	 * - "host:port[/path]" (e.g. 127.0.0.1:8080) becomes http://host:port[/path].
	 * - Paths (drive letter, UNC, relative, "./", with Rainmeter variables like #CURRENTPATH#) are resolved
	 *   with Rain::absPath() and converted to a properly percent-encoded file:// URL. A trailing
	 *   "?query" / "#fragment" is kept as URL syntax unless the whole string names an existing file.
	 *
	 * @param rain     Rain instance (variable expansion and path resolution).
	 * @param url      URL or path, UTF-8.
	 * @param isLocal  Output: true if the final result is a file:// URL.
	 * @return The resolved URL as a wide string, or empty on failure.
	 */
	std::wstring ResolveUrl( Rain *rain, const std::string &url, bool &isLocal ) {
		isLocal = false;
		if ( url.empty() || !rain )
			return {};

		switch ( ClassifyLocation( url ) ) {
		case LocationKind::Url: {
			std::string head = url.substr( 0, 5 );
			std::transform( head.begin(), head.end(), head.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
			isLocal = head == "file:";
			return utf8_to_wstring( url );
		}
		case LocationKind::HostPort:
			return utf8_to_wstring( "http://" + url );
		case LocationKind::Path:
			break;
		}

		// Relative path, Rainmeter variable or "./" - resolve through absPath
		std::wstring wurl = utf8_to_wstring( url );

		// "./" or ".\": RmPathToAbsolute does not handle them reliably,
		// so the skin directory is substituted explicitly.
		if ( wurl.size() >= 2 && wurl[0] == L'.' && ( wurl[1] == L'/' || wurl[1] == L'\\' ) )
			wurl = L"#CURRENTPATH#" + wurl.substr( 2 );

		std::wstring resolved = rain->absPath( wurl );
		if ( resolved.empty() )
			return {};

		// Split "?query" / "#fragment" off, unless the whole string is an existing file name.
		// '#' only counts in the last path segment (folders may legitimately contain it).
		std::wstring suffix;
		if ( !fs::fileExists( resolved ) ) {
			const size_t q = resolved.find( L'?' );
			size_t end = q == std::wstring::npos ? resolved.size() : q;
			const size_t sep = end == 0 ? std::wstring::npos : resolved.find_last_of( L"\\/", end - 1 );
			const size_t h = resolved.find( L'#', sep == std::wstring::npos ? 0 : sep + 1 );
			if ( h != std::wstring::npos && h < end )
				end = h;
			if ( end < resolved.size() ) {
				suffix = resolved.substr( end );
				resolved.resize( end );
			}
		}

		isLocal = true;
		return LocalPathToFileUrl( resolved, suffix );
	}


} // namespace trident
