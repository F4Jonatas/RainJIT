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
 * @file sanitize.hpp
 * @brief Header-only HTML, CSS, and URL sanitizer built on gumbo-parser.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * @par Third-party components
 * - gumbo-parser (https://github.com/google/gumbo-parser), Copyright Google Inc.,
 *   licensed under the Apache License 2.0. It is NOT part of this file; it is only
 *   included/linked. Apache-2.0 is incompatible with GPL-2.0-only but compatible
 *   with GPL-3.0, which is why this project is licensed "v2 or later": the combined
 *   binary may be distributed under GPL-3.0. Ship gumbo's LICENSE/NOTICE with
 *   any binary distribution.
 * - leafo/web_sanitize (MIT): design inspiration only. No source code was copied.
 *
 * Shared utility for any module that needs to sanitize untrusted content
 * before injecting it into a hosted browser control (e.g. trident, and
 * future modules).
 *
 * @details
 * Four sanitization layers:
 *
 *   1. PreProcess - strips null bytes and dangerous control characters
 *      from the raw string before gumbo sees it. MSHTML ignores null bytes
 *      inside tag names (`<scr\0ipt>`); gumbo must agree on what it parses.
 *
 *   2. HTML tree walk - gumbo builds a full, browser-equivalent DOM tree,
 *      then we reconstruct the output from scratch visiting only safe nodes.
 *        - Blocked tags (script, iframe, object, etc.) and their entire
 *          subtree are dropped silently.
 *        - Structural tags (html, head, body) are unwrapped.
 *        - on* event handler attributes are dropped universally.
 *        - URL attributes are validated (see "URL validation").
 *        - `id` / `name` values that would shadow built-in `document`, `window`
 *          or form members are dropped (DOM clobbering, see Options::blockClobbering).
 *        - `<style>` elements are rebuilt rule by rule (layer 4); `<style>` found in
 *          <head> is kept by HtmlFragment().
 *
 *   3. CSS declaration filter - for `style` attributes and rule bodies:
 *        a. The text is first normalised exactly like a CSS parser would see it:
 *           CSS escapes (`\65 xpression`) are decoded, comments are removed
 *           (string-aware, repeated until stable) and `<` is neutralised.
 *           Everything is validated and re-emitted from this normalised text, so
 *           what is checked is exactly what MSHTML will parse. Any declaration
 *           that still contains a backslash is dropped (no double-decoding).
 *        b. Declarations are split at top level only (strings / parentheses aware).
 *        c. Properties `filter`, `-ms-filter`, `behavior`, `-ms-behavior`,
 *           `-moz-binding` are always dropped (progid:/AlphaImageLoader, HTC).
 *        d. The value is scanned structurally: strings must terminate, parentheses
 *           must balance, unquoted characters are restricted to a conservative set
 *           (no `:`, `;`, `{`, `}`, `<`, `\`), function names must be ASCII and not
 *           in the blocklist (expression, paint, element, ...). Every `url()` /
 *           `src()` argument - at any nesting level - and every string inside
 *           `image-set()`/`image()`/`cross-fade()` is validated as a sub-resource URL.
 *        e. Optional anti-redress rules (Options::blockOverlay): `position:fixed|sticky`
 *           and out-of-range `z-index` are dropped.
 *
 *   4. Stylesheet rebuild (`<style>` elements): a small CSS parser walks the rules.
 *      Style rules are kept when the selector uses a conservative character set and
 *      at least one declaration survives layer 3. At-rules: only `@media`,
 *      `@supports`, `@keyframes` (vendor variants) and `@font-face` are kept;
 *      `@import`, `@charset`, `@namespace`, unknown at-rules are dropped.
 *
 * @par URL validation
 * `IsUrlSafe()` is exposed as a standalone utility for Navigate2 validation.
 *  - Backslashes are treated as slashes (MSHTML does); every character <= 0x20 is
 *    ignored before the scheme is inspected.
 *  - `//host`, `\\host\share` and anything that normalises to them are rejected
 *    (protocol-relative URLs resolve to SMB under file://, leaking NTLM hashes).
 *  - Allowed schemes: http, https, ftp, mailto and (Options::allowLocal) `file:///x`.
 *    `file://host/...` and `file://///host/...` (UNC) are always rejected.
 *  - http/https/ftp must have a clean authority: no userinfo (`user@host`), a valid
 *    host and a numeric port.
 *  - Options::restrictHosts + allowedHosts: auto-loaded sub-resources (src, data,
 *    poster, background, `<link href>`, CSS url()) may only point at the listed hosts
 *    ("example.com" or "*.example.com"). An empty list blocks every remote resource.
 *    Plain links (href, action) are only scheme-checked.
 *  - A colon after the first `/`, `?` or `#` is not a scheme separator
 *    (`a/b:c` is a legitimate relative URL).
 *
 * @par Entity decoding
 * `UnescapeHtml()` is a security primitive exposed publicly. It is called
 * internally before URL validation to decode attacks like `&#106;avascript:`
 * and `java\tscript:` before protocol checking.
 *
 * @par Usage
 * @code{.cpp}
 * #include "sanitize.hpp"
 *
 * std::string safe = sanitize::HtmlFragment(rawHtml);          // for document.write
 * std::string safe = sanitize::Html(rawHtml);                  // full document
 * std::string safe = sanitize::Style(rawDeclarations);         // style attribute only
 * std::string safe = sanitize::Stylesheet(rawCss);             // <style> contents
 * bool ok          = sanitize::IsUrlSafe(url);                 // before Navigate2
 * std::string dec  = sanitize::UnescapeHtml(encoded);          // entity decode
 * @endcode
 *
 * @note Only one external header is required: #include <gumbo.h>
 */

#pragma once

#include <gumbo.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace sanitize {

	/**
	 * @brief Options controlling what the HTML sanitizer blocks or allows.
	 *
	 * All fields default to the most restrictive (safest) setting.
	 * Callers loosen restrictions by setting fields to false/true as needed.
	 * In trident, these are built from the SanitizeFlags bitmask via FlagsToOptions.
	 */
	struct Options {
		bool blockScripts = true; ///< Strip <script>, <iframe>, <object>, <embed> + subtree.
		bool blockEvents = true; ///< Strip on* event handler attributes.
		bool blockStyle = false; ///< Remove style attributes AND <style> elements entirely (overrides filterCss).
		bool filterCss = true; ///< Filter style attributes and rebuild <style> elements rule by rule.
		bool validateUrls = true; ///< Validate URL attributes and url() in CSS.
		bool allowLocal = false; ///< Permit file:///path URLs when validateUrls is active.
		bool blockClobbering = true; ///< Drop id/name values that shadow document/window/form members (DOM clobbering).
		bool blockOverlay = true; ///< Drop position:fixed|sticky and large z-index (UI redress).
		bool restrictHosts = false; ///< When true, remote sub-resources must match allowedHosts (empty list = none allowed).
		std::vector<std::string> allowedHosts; ///< Lowercase hosts: "example.com" or "*.example.com".
		std::vector<std::string> reservedNames; ///< Extra lowercase id/name values to drop (e.g. the skin's own JS globals).
	};

	// =============================================================================
	// Internal implementation
	// =============================================================================
	namespace detail {

		static constexpr std::size_t kNpos = std::string::npos;
		static constexpr std::size_t kMaxCssBytes = 512 * 1024; ///< Larger CSS input is dropped entirely.
		static constexpr int kMaxCssDepth = 4; ///< Nesting of @media / @supports / @keyframes.
		static constexpr int kMaxValueDepth = 8; ///< Nesting of functions inside a value.
		static constexpr int kMaxDomDepth = 256; ///< Deeper DOM subtrees are dropped (stack safety).
		static constexpr size_t kMaxHtmlBytes = 8 * 1024 * 1024; ///< Larger HTML input is rejected (empty result).
		static constexpr int kMaxNesting = 8192; ///< Estimated element nesting above this is rejected (gumbo frees the tree recursively).

		// -------------------------------------------------------------------------
		// String helpers
		// -------------------------------------------------------------------------

		inline std::string ToLower( const std::string &s ) {
			std::string out = s;
			std::transform( out.begin(), out.end(), out.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
			return out;
		}

		inline std::string Trim( const std::string &s ) {
			size_t a = s.find_first_not_of( " \t\r\n\f" );
			if ( a == kNpos )
				return {};
			size_t b = s.find_last_not_of( " \t\r\n\f" );
			return s.substr( a, b - a + 1 );
		}

		inline std::string EscapeHtml( const std::string &s ) {
			std::string out;
			out.reserve( s.size() );
			for ( unsigned char c : s ) {
				switch ( c ) {
				case '&':
					out += "&amp;";
					break;
				case '<':
					out += "&lt;";
					break;
				case '>':
					out += "&gt;";
					break;
				default:
					out += static_cast<char>( c );
					break;
				}
			}
			return out;
		}

		inline std::string EscapeAttr( const std::string &s ) {
			std::string out;
			out.reserve( s.size() );
			for ( unsigned char c : s ) {
				switch ( c ) {
				case '&':
					out += "&amp;";
					break;
				case '"':
					out += "&quot;";
					break;
				case '<':
					out += "&lt;";
					break;
				case '>':
					out += "&gt;";
					break;
				case '`': // legacy IE treats ` as an attribute delimiter in some contexts
					out += "&#96;";
					break;
				default:
					out += static_cast<char>( c );
					break;
				}
			}
			return out;
		}

		inline void AppendUtf8( std::string &out, unsigned long cp ) {
			if ( cp < 0x80 ) {
				out += static_cast<char>( cp );
			} else if ( cp < 0x800 ) {
				out += static_cast<char>( 0xC0 | ( cp >> 6 ) );
				out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
			} else if ( cp < 0x10000 ) {
				out += static_cast<char>( 0xE0 | ( cp >> 12 ) );
				out += static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
				out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
			} else {
				out += static_cast<char>( 0xF0 | ( cp >> 18 ) );
				out += static_cast<char>( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
				out += static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
				out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
			}
		}

		inline int HexVal( unsigned char c ) {
			if ( c >= '0' && c <= '9' )
				return c - '0';
			if ( c >= 'a' && c <= 'f' )
				return c - 'a' + 10;
			if ( c >= 'A' && c <= 'F' )
				return c - 'A' + 10;
			return -1;
		}

		// -------------------------------------------------------------------------
		// HTML entity decoder
		// -------------------------------------------------------------------------

		/**
		 * @brief Decode HTML entities in a string.
		 *
		 * @details
		 * Security primitive - not just a display utility.
		 * Attackers encode `javascript:` as `&#106;avascript:` or `&#x6A;avascript:`
		 * to bypass naive string comparisons. We decode before protocol checking so
		 * we see exactly what the browser will see.
		 *
		 * Supports decimal numeric (&#60;), hex numeric (&#x3C;), and common named
		 * entities: &amp; &lt; &gt; &quot; &apos; &nbsp; &colon; &Tab; &NewLine;
		 * `&#0;` is removed (browsers ignore it inside URLs).
		 */
		inline std::string UnescapeHtml( const std::string &s ) {
			static const std::unordered_map<std::string, char> kNamed = {
					{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' }, { "colon", ':' }, { "Tab", '\t' }, { "NewLine", '\n' },
			};

			std::string out;
			out.reserve( s.size() );
			size_t i = 0;

			while ( i < s.size() ) {
				if ( s[i] != '&' || i + 1 >= s.size() ) {
					out += s[i++];
					continue;
				}

				if ( s[i + 1] == '#' ) {
					size_t j = i + 2;
					bool isHex = ( j < s.size() && ( s[j] == 'x' || s[j] == 'X' ) );
					if ( isHex )
						++j;
					size_t start = j;
					while ( j < s.size() && s[j] != ';' && ( j - start ) < 8 )
						++j;
					if ( j < s.size() && s[j] == ';' && j > start ) {
						try {
							unsigned long cp = std::stoul( s.substr( start, j - start ), nullptr, isHex ? 16 : 10 );
							if ( cp == 0 ) {
								// dropped on purpose
							} else if ( cp > 0x10FFFF || ( cp >= 0xD800 && cp <= 0xDFFF ) ) {
								out += '?';
							} else {
								AppendUtf8( out, cp );
							}
							i = j + 1;
							continue;
						} catch ( ... ) {
						}
					}
				} else {
					size_t j = i + 1;
					while ( j < s.size() && s[j] != ';' && std::isalpha( (unsigned char)s[j] ) && ( j - i ) < 12 )
						++j;
					if ( j < s.size() && s[j] == ';' ) {
						std::string name = s.substr( i + 1, j - i - 1 );
						auto it = kNamed.find( name );
						if ( it == kNamed.end() )
							it = kNamed.find( ToLower( name ) );
						if ( it != kNamed.end() ) {
							out += it->second;
							i = j + 1;
							continue;
						}
					}
				}

				out += s[i++];
			}
			return out;
		}

		// -------------------------------------------------------------------------
		// URL normalization and validation
		// -------------------------------------------------------------------------

		/**
		 * @brief Normalize a URL for safe protocol comparison.
		 *
		 * MSHTML ignores embedded whitespace/control characters in URLs and treats
		 * `\` as `/`. We decode entities, drop every byte <= 0x20 (and 0x7F), turn
		 * backslashes into slashes and lowercase ASCII, so the checks below see
		 * the same thing the browser will.
		 */
		inline std::string NormalizeUrl( const std::string &url ) {
			std::string decoded = UnescapeHtml( url );
			std::string out;
			out.reserve( decoded.size() );
			for ( unsigned char c : decoded ) {
				if ( c <= 0x20 || c == 0x7F )
					continue;
				if ( c == '\\' )
					c = '/';
				out += static_cast<char>( std::tolower( c ) );
			}
			return out;
		}

		/**
		 * @brief Extracts and validates the host of an authority component.
		 *
		 * @param auth  Text between "//" and the next "/", "?" or "#" (already normalised).
		 * @return Lowercase host (IPv6 keeps its brackets), or empty if the authority is
		 *         malformed: userinfo, bad port, non-ASCII or otherwise suspicious host.
		 */
		inline std::string HostFromAuthority( const std::string &auth ) {
			if ( auth.empty() || auth.find( '@' ) != kNpos )
				return {};

			auto allDigits = []( const std::string &s ) { return std::all_of( s.begin(), s.end(), []( unsigned char c ) { return std::isdigit( c ) != 0; } ); };

			if ( auth[0] == '[' ) {
				size_t close = auth.find( ']' );
				if ( close == kNpos || close < 2 )
					return {};
				std::string rest = auth.substr( close + 1 );
				if ( !rest.empty() && ( rest[0] != ':' || !allDigits( rest.substr( 1 ) ) ) )
					return {};
				for ( size_t i = 1; i < close; ++i ) {
					unsigned char c = auth[i];
					if ( !( std::isxdigit( c ) || c == ':' || c == '.' ) )
						return {};
				}
				return auth.substr( 0, close + 1 );
			}

			std::string h = auth;
			size_t colon = auth.rfind( ':' );
			if ( colon != kNpos ) {
				if ( !allDigits( auth.substr( colon + 1 ) ) )
					return {};
				h = auth.substr( 0, colon );
			}

			std::string host;
			for ( size_t i = 0; i < h.size(); ++i ) {
				unsigned char c = h[i];
				if ( c == '%' ) {
					if ( i + 2 >= h.size() )
						return {};
					int hi = HexVal( h[i + 1] ), lo = HexVal( h[i + 2] );
					if ( hi < 0 || lo < 0 )
						return {};
					c = static_cast<unsigned char>( ( hi << 4 ) | lo );
					i += 2;
				}
				c = static_cast<unsigned char>( std::tolower( c ) );
				if ( !( std::isalnum( c ) || c == '.' || c == '-' || c == '_' ) || c >= 0x80 )
					return {};
				host += static_cast<char>( c );
			}
			while ( !host.empty() && host.back() == '.' )
				host.pop_back();
			return host;
		}

		inline bool HostAllowed( const std::string &host, const std::vector<std::string> &allowed ) {
			for ( const std::string &raw : allowed ) {
				std::string e = ToLower( raw );
				if ( e.size() > 2 && e[0] == '*' && e[1] == '.' ) {
					std::string suffix = e.substr( 1 ); // ".example.com"
					if ( host.size() > suffix.size() && host.compare( host.size() - suffix.size(), suffix.size(), suffix ) == 0 )
						return true;
				} else if ( host == e ) {
					return true;
				}
			}
			return false;
		}

		/**
		 * @brief Validates a normalised URL (see NormalizeUrl).
		 *
		 * @param subresource true for URLs the browser loads automatically (src, CSS url(), ...):
		 *                    the host allowlist applies only to these.
		 */
		inline bool IsUrlSafeNorm( const std::string &norm, const Options &o, bool subresource ) {
			if ( norm.empty() )
				return true;

			// "//host" / "\\host\share" (normalised): protocol-relative -> UNC/SMB under file://
			if ( norm.size() >= 2 && norm[0] == '/' && norm[1] == '/' )
				return false;
			if ( norm[0] == '/' || norm[0] == '#' || norm[0] == '.' || norm[0] == '?' )
				return true;

			// A scheme ends at the first ':' that comes before any '/', '?' or '#'.
			size_t i = norm.find_first_of( ":/?#" );
			if ( i == kNpos || norm[i] != ':' )
				return true; // relative reference ("a/b:c" included)

			std::string scheme = norm.substr( 0, i );
			std::string rest = norm.substr( i + 1 );

			if ( scheme == "mailto" )
				return true;

			if ( scheme == "http" || scheme == "https" || scheme == "ftp" ) {
				if ( rest.compare( 0, 2, "//" ) != 0 )
					return false;
				size_t end = rest.find_first_of( "/?#", 2 );
				std::string authority = rest.substr( 2, end == kNpos ? kNpos : end - 2 );
				std::string host = HostFromAuthority( authority );
				if ( host.empty() )
					return false;
				if ( subresource && o.restrictHosts && !HostAllowed( host, o.allowedHosts ) )
					return false;
				return true;
			}

			if ( scheme == "file" ) {
				if ( !o.allowLocal )
					return false;
				// Only "file:///<path>": an empty host and a path that does not start with
				// another slash (that would be a UNC path: file://///server/share).
				if ( rest.compare( 0, 3, "///" ) != 0 || rest.size() < 4 || rest[3] == '/' )
					return false;
				return true;
			}

			return false;
		}

		// -------------------------------------------------------------------------
		// PreProcess
		// -------------------------------------------------------------------------

		/**
		 * @brief Remove null bytes and dangerous control characters from raw HTML.
		 *
		 * MSHTML silently ignores null bytes inside tag names (`<scr\0ipt>`).
		 * We strip them so gumbo and MSHTML agree on what they parse.
		 *
		 * Removed: 0x00, 0x01-0x08, 0x0B, 0x0C, 0x0E-0x1F, 0x7F.
		 * Preserved: 0x09 (tab), 0x0A (LF), 0x0D (CR), 0x20+ (normal text).
		 */
		inline std::string PreProcess( const std::string &html ) {
			std::string out;
			out.reserve( html.size() );
			for ( unsigned char c : html ) {
				if ( c == 0x00 )
					continue;
				if ( c < 0x09 )
					continue;
				if ( c == 0x0B || c == 0x0C )
					continue;
				if ( c >= 0x0E && c <= 0x1F )
					continue;
				if ( c == 0x7F )
					continue;
				out += static_cast<char>( c );
			}
			return out;
		}

		// -------------------------------------------------------------------------
		// CSS: tokenizing helpers
		// -------------------------------------------------------------------------

		inline bool IsIdentChar( unsigned char c ) {
			return std::isalnum( c ) || c == '-' || c == '_' || c >= 0x80;
		}

		/// Index of the closing quote of the string opened at `open`, or kNpos (unterminated / raw newline).
		inline size_t FindStringEnd( const std::string &s, size_t open ) {
			const char q = s[open];
			for ( size_t i = open + 1; i < s.size(); ++i ) {
				if ( s[i] == q )
					return i;
				if ( s[i] == '\n' || s[i] == '\r' || s[i] == '\f' )
					return kNpos;
			}
			return kNpos;
		}

		/// Index of the ')' that closes the '(' at `open` (string aware), or kNpos.
		inline size_t FindClosingParen( const std::string &s, size_t open ) {
			int depth = 0;
			for ( size_t i = open; i < s.size(); ++i ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos )
						return kNpos;
					i = e;
				} else if ( c == '(' ) {
					++depth;
				} else if ( c == ')' ) {
					if ( --depth == 0 )
						return i;
				}
			}
			return kNpos;
		}

		/// Splits at `sep` outside strings and parentheses.
		inline std::vector<std::string> SplitTop( const std::string &s, char sep ) {
			std::vector<std::string> parts;
			size_t start = 0;
			int depth = 0;
			for ( size_t i = 0; i < s.size(); ++i ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos )
						break; // rest belongs to the current (invalid) part
					i = e;
				} else if ( c == '(' ) {
					++depth;
				} else if ( c == ')' ) {
					if ( depth > 0 )
						--depth;
				} else if ( c == sep && depth == 0 ) {
					parts.push_back( s.substr( start, i - start ) );
					start = i + 1;
				}
			}
			parts.push_back( s.substr( start ) );
			return parts;
		}

		// -------------------------------------------------------------------------
		// CSS: normalisation (escapes, comments)
		// -------------------------------------------------------------------------

		/**
		 * @brief Decodes CSS escapes (`\65 `, `\000065`, `\(`, line continuations).
		 *
		 * Attackers hide `expression(` or `javascript:` behind `\65 xpression(`.
		 * Control characters produced by escapes become spaces; NUL, surrogates and
		 * out-of-range values become U+FFFD. A decoded backslash stays a backslash:
		 * callers drop any declaration/selector that still contains one, so the
		 * output can never be decoded a second time by the browser.
		 */
		inline std::string CssUnescape( const std::string &s ) {
			std::string out;
			out.reserve( s.size() );
			const size_t n = s.size();
			size_t i = 0;
			while ( i < n ) {
				if ( s[i] != '\\' ) {
					out += s[i++];
					continue;
				}
				if ( i + 1 >= n ) { // trailing backslash
					++i;
					continue;
				}
				unsigned char d = s[i + 1];
				if ( HexVal( d ) >= 0 ) {
					size_t j = i + 1;
					unsigned long cp = 0;
					int digits = 0;
					while ( j < n && digits < 6 && HexVal( s[j] ) >= 0 ) {
						cp = cp * 16 + HexVal( s[j] );
						++j;
						++digits;
					}
					if ( j < n ) { // a single whitespace terminates the escape
						if ( s[j] == '\r' && j + 1 < n && s[j + 1] == '\n' )
							j += 2;
						else if ( s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r' || s[j] == '\f' )
							++j;
					}
					if ( cp == 0 || cp > 0x10FFFF || ( cp >= 0xD800 && cp <= 0xDFFF ) )
						AppendUtf8( out, 0xFFFD );
					else if ( cp < 0x20 || cp == 0x7F )
						out += ' ';
					else
						AppendUtf8( out, cp );
					i = j;
				} else if ( d == '\n' || d == '\f' ) {
					i += 2;
				} else if ( d == '\r' ) {
					i += ( i + 2 < n && s[i + 2] == '\n' ) ? 3 : 2;
				} else {
					out += static_cast<char>( d );
					i += 2;
				}
			}
			return out;
		}

		/// Removes /* ... */ comments outside strings. Unterminated comments swallow the rest.
		inline std::string StripCssComments( const std::string &s, bool &changed ) {
			std::string out;
			out.reserve( s.size() );
			size_t i = 0;
			while ( i < s.size() ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos ) {
						out.append( s, i, kNpos );
						break;
					}
					out.append( s, i, e - i + 1 );
					i = e + 1;
				} else if ( c == '/' && i + 1 < s.size() && s[i + 1] == '*' ) {
					changed = true;
					size_t e = s.find( "*/", i + 2 );
					if ( e == kNpos )
						break;
					i = e + 2;
				} else {
					out += c;
					++i;
				}
			}
			return out;
		}

		inline void EraseAll( std::string &s, const char *needle ) {
			const size_t len = std::char_traits<char>::length( needle );
			size_t pos;
			while ( ( pos = s.find( needle ) ) != kNpos )
				s.erase( pos, len );
		}

		/**
		 * @brief Brings CSS text to the form MSHTML will actually parse.
		 *
		 * Order matters: escapes are decoded first, then comments are removed
		 * (repeatedly, because removal can glue `/` and `*` into a new comment start),
		 * and finally `<` is neutralised so the text can never close a `<style>` element.
		 * Returns an empty string if the input is too large or never stabilises.
		 */
		inline std::string CssPrepare( const std::string &raw ) {
			if ( raw.size() > kMaxCssBytes )
				return {};

			std::string t;
			t.reserve( raw.size() );
			for ( unsigned char c : raw ) {
				if ( c == 0 )
					continue;
				if ( ( c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' ) || c == 0x7F )
					c = ' ';
				t += static_cast<char>( c );
			}

			t = CssUnescape( t );
			EraseAll( t, "<!--" );
			EraseAll( t, "-->" );

			bool stable = false;
			for ( int pass = 0; pass < 16; ++pass ) {
				bool changed = false;
				t = StripCssComments( t, changed );
				if ( !changed ) {
					stable = true;
					break;
				}
			}
			if ( !stable )
				return {};

			std::replace( t.begin(), t.end(), '<', ' ' );
			return t;
		}

		// -------------------------------------------------------------------------
		// CSS: values
		// -------------------------------------------------------------------------

		/// Conservative character set for text outside strings and function arguments.
		inline bool IsAllowedValueChar( unsigned char c ) {
			if ( std::isalnum( c ) || c >= 0x80 )
				return true;
			switch ( c ) {
			case ' ': case '\t': case '\n': case '\r': case '\f':
			case '-': case '_': case '.': case '#': case '%': case ',': case '/': case '!':
			case '+': case '*': case '=': case '~': case '>': case '^': case '|': case '$':
			case '&': case '@': case '[': case ']': case '?':
				return true;
			default:
				return false; // : ; { } < \ ` and controls
			}
		}

		/// Validates the argument of url()/src(): one quoted string or one unquoted token.
		inline bool CssUrlArgOk( const std::string &inner, const Options &o ) {
			std::string arg = Trim( inner );
			if ( arg.empty() )
				return false;

			if ( arg[0] == '"' || arg[0] == '\'' ) {
				size_t e = FindStringEnd( arg, 0 );
				if ( e == kNpos || e != arg.size() - 1 )
					return false;
				arg = arg.substr( 1, arg.size() - 2 );
			} else if ( arg.find_first_of( "\"'() \t\r\n\f" ) != kNpos ) {
				return false;
			}
			if ( arg.find( '\\' ) != kNpos )
				return false;
			return o.validateUrls ? IsUrlSafeNorm( NormalizeUrl( arg ), o, true ) : true;
		}

		inline bool CssValueOk( const std::string &v, const Options &o, int depth );

		/// image-set()/image()/cross-fade(): bare strings are URLs, nested functions are validated recursively.
		inline bool CssImageFuncOk( const std::string &inner, const Options &o, int depth ) {
			if ( !CssValueOk( inner, o, depth ) )
				return false;
			int paren = 0;
			for ( size_t i = 0; i < inner.size(); ++i ) {
				char c = inner[i];
				if ( c == '(' ) {
					++paren;
				} else if ( c == ')' ) {
					--paren;
				} else if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( inner, i );
					if ( e == kNpos )
						return false;
					if ( paren == 0 ) {
						std::string s = inner.substr( i + 1, e - i - 1 );
						if ( s.find( '\\' ) != kNpos || ( o.validateUrls && !IsUrlSafeNorm( NormalizeUrl( s ), o, true ) ) )
							return false;
					}
					i = e;
				}
			}
			return true;
		}

		/**
		 * @brief Structural validation of a declaration value.
		 *
		 * Strings must terminate, parentheses must balance, only a conservative set of
		 * characters is allowed outside strings and arguments, function names must be
		 * ASCII and not blocked, and every url()/src() - at any depth - is checked.
		 */
		inline bool CssValueOk( const std::string &v, const Options &o, int depth ) {
			if ( depth > kMaxValueDepth )
				return false;

			static const std::unordered_set<std::string> kBlockedFuncs = {
					"expression", "paint", "element", "-moz-element", "-webkit-paint", "-moz-binding", "-ms-behavior", "behavior", "binding", "progid",
			};
			static const std::unordered_set<std::string> kUrlFuncs = { "url", "src" };
			static const std::unordered_set<std::string> kImageFuncs = { "image", "image-set", "-webkit-image-set", "cross-fade", "-webkit-cross-fade" };

			size_t i = 0;
			while ( i < v.size() ) {
				unsigned char c = v[i];

				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( v, i );
					if ( e == kNpos )
						return false;
					i = e + 1;
					continue;
				}

				if ( c == '(' ) {
					size_t b = i;
					while ( b > 0 && IsIdentChar( (unsigned char)v[b - 1] ) )
						--b;
					std::string rawName = v.substr( b, i - b );
					for ( unsigned char nc : rawName ) {
						if ( nc >= 0x80 ) // e.g. full-width "expression" that legacy IE normalises
							return false;
					}
					std::string name = ToLower( rawName );

					size_t close = FindClosingParen( v, i );
					if ( close == kNpos )
						return false;
					std::string inner = v.substr( i + 1, close - i - 1 );

					if ( kBlockedFuncs.count( name ) )
						return false;
					if ( kUrlFuncs.count( name ) ) {
						if ( !CssUrlArgOk( inner, o ) )
							return false;
					} else if ( kImageFuncs.count( name ) ) {
						if ( !CssImageFuncOk( inner, o, depth + 1 ) )
							return false;
					} else if ( !CssValueOk( inner, o, depth + 1 ) ) {
						return false;
					}
					i = close + 1;
					continue;
				}

				if ( c == ')' || !IsAllowedValueChar( c ) )
					return false;
				++i;
			}
			return true;
		}

		/// Anti-redress rules (Options::blockOverlay).
		inline bool CssOverlaySafe( const std::string &prop, const std::string &value ) {
			if ( prop != "position" && prop != "z-index" )
				return true;

			std::string v;
			for ( unsigned char c : ToLower( value ) )
				if ( !std::isspace( c ) )
					v += static_cast<char>( c );
			size_t imp = v.find( "!important" );
			if ( imp != kNpos )
				v.erase( imp );

			if ( prop == "position" )
				return !( v == "fixed" || v == "sticky" || v == "-webkit-sticky" );

			// z-index
			if ( v == "auto" || v == "inherit" || v == "initial" )
				return true;
			size_t k = 0;
			if ( k < v.size() && ( v[k] == '-' || v[k] == '+' ) )
				++k;
			if ( k >= v.size() || v.size() - k > 4 )
				return false;
			int n = 0;
			for ( ; k < v.size(); ++k ) {
				if ( !std::isdigit( (unsigned char)v[k] ) )
					return false;
				n = n * 10 + ( v[k] - '0' );
			}
			return n <= 100;
		}

		/**
		 * @brief Filters a declaration list that has already gone through CssPrepare().
		 * @return "prop:value;prop:value" (no trailing semicolon), possibly empty.
		 */
		inline std::string SanitizeDeclList( const std::string &text, const Options &o ) {
			static const std::unordered_set<std::string> kBlockedProps = {
					"filter", "-ms-filter", "behavior", "-ms-behavior", "-moz-binding", "binding",
			};
			static const std::vector<std::string> kDangerous = {
					"expression(", "-moz-binding", "progid:", "alphaimageloader", "javascript:", "vbscript:", "livescript:", "mocha:", "data:", "@import",
			};

			std::string result;
			for ( const std::string &part : SplitTop( text, ';' ) ) {
				std::string decl = Trim( part );
				if ( decl.empty() )
					continue;
				if ( decl.find_first_of( "\\{}" ) != kNpos )
					continue;

				size_t colon = decl.find( ':' );
				if ( colon == kNpos )
					continue;

				std::string prop = ToLower( Trim( decl.substr( 0, colon ) ) );
				std::string value = Trim( decl.substr( colon + 1 ) );
				if ( prop.empty() || value.empty() )
					continue;
				if ( !std::all_of( prop.begin(), prop.end(), []( unsigned char c ) { return std::isalnum( c ) || c == '-' || c == '_'; } ) )
					continue;
				if ( kBlockedProps.count( prop ) )
					continue;
				if ( !CssValueOk( value, o, 0 ) )
					continue;

				std::string compact;
				for ( unsigned char c : ToLower( prop + ":" + value ) )
					if ( !std::isspace( c ) )
						compact += static_cast<char>( c );
				bool dangerous = false;
				for ( const std::string &pat : kDangerous ) {
					if ( compact.find( pat ) != kNpos ) {
						dangerous = true;
						break;
					}
				}
				if ( dangerous )
					continue;

				if ( o.blockOverlay && !CssOverlaySafe( prop, value ) )
					continue;

				if ( !result.empty() )
					result += ';';
				result += prop + ':' + value;
			}
			return result;
		}

		/// style="..." attribute value.
		inline std::string SanitizeStyle( const std::string &style, const Options &o ) {
			std::string prepared = CssPrepare( style );
			if ( prepared.empty() )
				return {};
			return SanitizeDeclList( prepared, o );
		}

		// -------------------------------------------------------------------------
		// CSS: stylesheets (<style> elements)
		// -------------------------------------------------------------------------

		/// Index of the first ';', '{' or '}' outside strings/parentheses at or after `from`, or kNpos.
		inline size_t FindPreludeEnd( const std::string &s, size_t from ) {
			int depth = 0;
			for ( size_t i = from; i < s.size(); ++i ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos )
						return kNpos;
					i = e;
				} else if ( c == '(' ) {
					++depth;
				} else if ( c == ')' ) {
					if ( depth > 0 )
						--depth;
				} else if ( depth == 0 && ( c == ';' || c == '{' || c == '}' ) ) {
					return i;
				}
			}
			return kNpos;
		}

		/// Index of the '}' matching the '{' at `open`; s.size() if the block is never closed.
		inline size_t FindBlockEnd( const std::string &s, size_t open ) {
			int depth = 0;
			for ( size_t i = open; i < s.size(); ++i ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos )
						return s.size();
					i = e;
				} else if ( c == '{' ) {
					++depth;
				} else if ( c == '}' ) {
					if ( --depth == 0 )
						return i;
				}
			}
			return s.size();
		}

		inline bool BalancedSimple( const std::string &s ) {
			int depth = 0;
			for ( size_t i = 0; i < s.size(); ++i ) {
				char c = s[i];
				if ( c == '"' || c == '\'' ) {
					size_t e = FindStringEnd( s, i );
					if ( e == kNpos )
						return false;
					i = e;
				} else if ( c == '(' ) {
					++depth;
				} else if ( c == ')' ) {
					if ( --depth < 0 )
						return false;
				}
			}
			return depth == 0;
		}

		inline bool SelectorOk( const std::string &sel ) {
			if ( sel.empty() || sel.size() > 1024 || !BalancedSimple( sel ) )
				return false;
			for ( size_t i = 0; i < sel.size(); ++i ) {
				unsigned char c = sel[i];
				if ( c == '"' || c == '\'' ) {
					i = FindStringEnd( sel, i ); // validated by BalancedSimple
					continue;
				}
				if ( std::isalnum( c ) || c >= 0x80 )
					continue;
				switch ( c ) {
				case ' ': case '\t': case '\n': case '\r': case '\f':
				case '_': case '-': case '.': case '#': case '*': case ',': case '>': case '+':
				case '~': case ':': case '(': case ')': case '[': case ']': case '=': case '|':
				case '^': case '$': case '%': case '@': case '!': case '/': case '&':
					break;
				default:
					return false; // ; { } < \ ` controls
				}
			}
			return true;
		}

		inline bool MediaPreludeOk( const std::string &p ) {
			if ( p.empty() || p.size() > 512 )
				return false;
			int depth = 0;
			for ( unsigned char c : p ) {
				if ( std::isalnum( c ) )
					continue;
				switch ( c ) {
				case ' ': case '\t': case '\n': case '\r': case '\f':
				case '_': case '-': case '.': case ',': case ':': case '/': case '%': case '=': case '+': case '>':
					break;
				case '(':
					++depth;
					break;
				case ')':
					if ( --depth < 0 )
						return false;
					break;
				default:
					return false;
				}
			}
			return depth == 0;
		}

		inline void SanitizeRules( const std::string &text, const Options &o, int depth, std::string &out ) {
			static const std::unordered_set<std::string> kContainers = { "media", "supports" };
			static const std::unordered_set<std::string> kKeyframes = { "keyframes", "-webkit-keyframes", "-ms-keyframes", "-moz-keyframes", "-o-keyframes" };

			const size_t n = text.size();
			size_t i = 0;
			while ( i < n ) {
				if ( std::isspace( (unsigned char)text[i] ) ) {
					++i;
					continue;
				}
				if ( text[i] == '}' || text[i] == ';' ) { // stray terminators
					++i;
					continue;
				}

				const bool isAt = text[i] == '@';
				size_t nameEnd = i + 1;
				std::string atName;
				if ( isAt ) {
					while ( nameEnd < n && ( std::isalnum( (unsigned char)text[nameEnd] ) || text[nameEnd] == '-' || text[nameEnd] == '_' ) )
						++nameEnd;
					atName = ToLower( text.substr( i + 1, nameEnd - i - 1 ) );
				}

				size_t k = FindPreludeEnd( text, isAt ? nameEnd : i );
				if ( k == kNpos )
					break; // unterminated tail is dropped
				if ( text[k] != '{' ) { // "@import ...;" or junk up to ';' / '}'
					i = k + 1;
					continue;
				}

				const size_t end = FindBlockEnd( text, k );
				const std::string block = text.substr( k + 1, end > k + 1 ? end - k - 1 : 0 );
				const std::string prelude = Trim( text.substr( isAt ? nameEnd : i, k - ( isAt ? nameEnd : i ) ) );
				i = end < n ? end + 1 : n;

				if ( isAt ) {
					if ( kContainers.count( atName ) ) {
						if ( depth >= kMaxCssDepth || !MediaPreludeOk( prelude ) )
							continue;
						std::string inner;
						SanitizeRules( block, o, depth + 1, inner );
						if ( !inner.empty() )
							out += "@" + atName + " " + prelude + "{" + inner + "}";
					} else if ( kKeyframes.count( atName ) ) {
						if ( depth >= kMaxCssDepth || prelude.empty() || prelude.size() > 128 || !std::all_of( prelude.begin(), prelude.end(), []( unsigned char c ) { return IsIdentChar( c ) && c < 0x80; } ) )
							continue;
						std::string inner;
						SanitizeRules( block, o, depth + 1, inner );
						if ( !inner.empty() )
							out += "@" + atName + " " + prelude + "{" + inner + "}";
					} else if ( atName == "font-face" ) {
						std::string decls = SanitizeDeclList( block, o );
						if ( !decls.empty() )
							out += "@font-face{" + decls + "}";
					}
					// every other at-rule is dropped
					continue;
				}

				if ( !SelectorOk( prelude ) )
					continue;
				std::string decls = SanitizeDeclList( block, o );
				if ( !decls.empty() )
					out += prelude + "{" + decls + "}";
			}
		}

		inline std::string SanitizeStylesheet( const std::string &css, const Options &o ) {
			std::string prepared = CssPrepare( css );
			if ( prepared.empty() )
				return {};
			std::string out;
			SanitizeRules( prepared, o, 0, out );
			return out;
		}

		// -------------------------------------------------------------------------
		// DOM clobbering
		// -------------------------------------------------------------------------

		/**
		 * @brief true if an id/name value would shadow a built-in member.
		 *
		 * In MSHTML, `id="location"` can replace `document.location`, `name="action"`
		 * replaces `form.action`, and any id becomes a `window` global. Same strategy as
		 * DOMPurify's SANITIZE_DOM: drop the attribute on collision.
		 */
		inline bool IsClobberName( const std::string &value, const Options &o ) {
			static const std::unordered_set<std::string> kNames = {
					// document
					"all", "anchors", "applets", "body", "characterset", "charset", "childnodes", "children", "compatmode", "contenttype", "cookie", "currentscript",
					"defaultview", "designmode", "dir", "doctype", "documentelement", "documenturi", "domain", "embeds", "execcommand", "fgcolor", "bgcolor", "linkcolor",
					"vlinkcolor", "alinkcolor", "forms", "getelementbyid", "getelementsbyname", "getelementsbytagname", "getelementsbyclassname", "head", "images",
					"implementation", "lastmodified", "links", "location", "namespaces", "open", "close", "write", "writeln", "parentwindow", "plugins", "queryselector",
					"queryselectorall", "readystate", "referrer", "scripts", "selection", "stylesheets", "title", "url", "createelement", "createtextnode", "activeelement",
					"hidden",
					// window
					"window", "self", "top", "parent", "frames", "opener", "document", "external", "navigator", "history", "screen", "event", "alert", "confirm", "prompt",
					"settimeout", "setinterval", "cleartimeout", "clearinterval", "eval", "execscript", "localstorage", "sessionstorage", "length", "constructor", "prototype",
					"__proto__", "valueof", "hasownproperty", "clientinformation", "closed", "status", "defaultstatus", "frameelement", "performance", "console",
					"xmlhttprequest", "activexobject", "json", "array", "object", "function", "string", "number", "date", "math", "regexp", "boolean", "error", "undefined",
					"nan", "infinity", "name", "onerror", "onload", "onunload", "onbeforeunload", "onmessage", "navigate", "print", "stop", "focus", "blur", "showmodaldialog",
					"createpopup", "moveto", "resizeto", "scroll", "scrollto", "scrollby", "innerwidth", "innerheight", "outerwidth", "outerheight", "pageyoffset", "pagexoffset",
					"getcomputedstyle", "postmessage", "atob", "btoa", "origin",
					// form / element
					"action", "method", "enctype", "encoding", "target", "elements", "submit", "reset", "acceptcharset", "autocomplete", "novalidate", "checkvalidity",
					"attributes", "innerhtml", "outerhtml", "innertext", "outertext", "style", "classname", "tagname", "nodename", "nodetype", "nodevalue", "nextsibling",
					"previoussibling", "firstchild", "lastchild", "parentnode", "parentelement", "ownerdocument", "appendchild", "removechild", "replacechild", "insertbefore",
					"clonenode", "setattribute", "getattribute", "removeattribute", "hasattribute", "hasattributes", "click", "contains", "remove", "id", "textcontent", "dataset",
					"tostring",
			};

			std::string v = ToLower( Trim( value ) );
			if ( v.empty() )
				return false;
			if ( kNames.count( v ) )
				return true;
			for ( const std::string &r : o.reservedNames ) {
				if ( v == ToLower( r ) )
					return true;
			}
			return false;
		}

		// -------------------------------------------------------------------------
		// Tag / attribute data sets
		// -------------------------------------------------------------------------

		inline const std::unordered_set<std::string> &BlockedTags() {
			static const std::unordered_set<std::string> s = {
					"script", "noscript", "iframe", "frame", "frameset", "noframes", "object", "embed", "applet", "param", "base", "link", "meta", "svg", "math", "template", "xml",
			};
			return s;
		}

		/**
		 * @brief Tags dropped (with their subtree) regardless of Options.
		 *
		 * Item 11: elements whose parsing differs between gumbo (HTML5) and MSHTML (legacy IE)
		 * are the raw material of mutation XSS. `plaintext`/`xmp`/`noembed`/`comment` switch the
		 * tokenizer to a raw-text mode in some engines and not in others; `bgsound`, `layer`
		 * and `ilayer` are IE-only and have no safe use here.
		 */
		inline const std::unordered_set<std::string> &AlwaysBlockedTags() {
			static const std::unordered_set<std::string> s = {
					"plaintext", "xmp", "noembed", "comment", "bgsound", "layer", "ilayer", "import",
			};
			return s;
		}

		/**
		 * @brief Structural tags: tag itself is dropped, children are serialized.
		 *
		 * When API content contains `<body onload="...">` and is injected via
		 * document.write, the browser merges the second body's attributes into
		 * the existing body element. Dropping the tag prevents that injection vector
		 * while preserving the content inside.
		 */
		inline const std::unordered_set<std::string> &StructuralTags() {
			static const std::unordered_set<std::string> s = { "html", "head", "body" };
			return s;
		}

		inline const std::unordered_set<std::string> &BlockedAttributes() {
			static const std::unordered_set<std::string> s = {
					"xmlns",
					"formaction",
					"srcdoc",
					"xlink:href",
			};
			return s;
		}

		/// Attributes that carry a URL and are therefore validated.
		inline const std::unordered_set<std::string> &UrlAttributes() {
			static const std::unordered_set<std::string> s = {
					"href", "src", "action", "data", "poster", "background", "dynsrc", "lowsrc", "codebase", "cite", "longdesc", "profile", "manifest", "ping",
			};
			return s;
		}

		/// Subset that the browser fetches automatically (host allowlist applies).
		inline const std::unordered_set<std::string> &SubresourceAttributes() {
			static const std::unordered_set<std::string> s = {
					"src", "data", "poster", "background", "dynsrc", "lowsrc", "codebase", "manifest",
			};
			return s;
		}

		inline const std::unordered_set<std::string> &VoidElements() {
			static const std::unordered_set<std::string> s = {
					"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
			};
			return s;
		}

		// Attributes injected into specific tags (web_sanitize add_attributes ideology)
		inline const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> &InjectedAttributes() {
			static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s = {
					{ "a", { { "rel", "nofollow noopener" } } },
					{ "img", { { "loading", "lazy" } } },
			};
			return s;
		}

		// -------------------------------------------------------------------------
		// Tree serialization
		// -------------------------------------------------------------------------

		// -------------------------------------------------------------------------
		// VBScript: blocked unconditionally (independent of every Option)
		// -------------------------------------------------------------------------

		/// Lowercase, entity-decoded, whitespace/control-free form of an attribute value.
		inline std::string CompactValue( const std::string &v ) {
			return NormalizeUrl( v );
		}

		/// "vbscript:" (or a prefix spelled with whitespace/entities/backslashes) anywhere in the value.
		inline bool HasVbScriptScheme( const std::string &value ) {
			return CompactValue( value ).find( "vbscript:" ) != kNpos;
		}

		/// Any spelling of the VBScript engine in a language/type value ("vbs", "vbscript", "text/vbscript.encode").
		inline bool NamesVbScript( const std::string &value ) {
			std::string c = CompactValue( value );
			return c.find( "vbs" ) != kNpos || c.find( "vbscript" ) != kNpos || c.find( "visualbasic" ) != kNpos;
		}

		/**
		 * @brief true if a <script> element declares a language we know to be JavaScript.
		 *
		 * Allowlist, not blocklist: an unknown `language`/`type` is dropped, which also covers
		 * VBScript, VBScript.Encode, JScript.Encode, PerlScript and anything a legacy engine registers.
		 */
		inline bool ScriptLanguageIsJs( const GumboVector &attrs ) {
			for ( unsigned int i = 0; i < attrs.length; ++i ) {
				const GumboAttribute *a = static_cast<GumboAttribute *>( attrs.data[i] );
				std::string name = ToLower( a->name );
				if ( name != "language" && name != "type" )
					continue;
				std::string v = CompactValue( a->value ? a->value : "" );
				size_t semi = v.find( ';' );
				if ( semi != kNpos )
					v.erase( semi );
				if ( v.empty() )
					continue;
				if ( name == "language" ) {
					static const std::unordered_set<std::string> kLang = { "javascript", "jscript", "ecmascript", "js" };
					bool ok = kLang.count( v ) != 0 || ( v.compare( 0, 10, "javascript" ) == 0 && v.size() <= 13 && v.find_first_not_of( "0123456789.", 10 ) == kNpos );
					if ( !ok )
						return false;
				} else {
					static const std::unordered_set<std::string> kType = {
							"text/javascript", "application/javascript", "application/x-javascript", "text/ecmascript", "application/ecmascript", "text/jscript", "text/x-javascript",
					};
					if ( !kType.count( v ) )
						return false;
				}
			}
			return true;
		}

		/// Style text that can pull in script through the back door (HTC behaviors, expression(), bindings).
		inline bool CssHasScriptVector( const std::string &css ) {
			std::string prepared = CssPrepare( css );
			if ( prepared.empty() && !css.empty() )
				return true; // too large / never stabilised: fail closed
			std::string compact;
			for ( unsigned char c : ToLower( prepared ) )
				if ( !std::isspace( c ) )
					compact += static_cast<char>( c );
			return compact.find( "behavior" ) != kNpos || compact.find( "expression(" ) != kNpos || compact.find( "-moz-binding" ) != kNpos || compact.find( ".htc" ) != kNpos || compact.find( "vbscript" ) != kNpos || compact.find( "vbs" ) != kNpos || compact.find( "progid" ) != kNpos;
		}

		/// Attribute names we are willing to emit: letters, digits, '_', ':', '.', '-'.
		inline bool AttrNameOk( const std::string &n ) {
			if ( n.empty() || n.size() > 64 )
				return false;
			return std::all_of( n.begin(), n.end(), []( unsigned char c ) { return std::isalnum( c ) || c == '_' || c == ':' || c == '.' || c == '-'; } );
		}

		inline void SerializeNode( GumboNode *node, std::ostringstream &out, const Options &opts, int depth );

		inline void SerializeChildren( GumboNode *node, std::ostringstream &out, const Options &opts, int depth ) {
			const GumboVector *children = nullptr;
			if ( node->type == GUMBO_NODE_ELEMENT )
				children = &node->v.element.children;
			else if ( node->type == GUMBO_NODE_DOCUMENT )
				children = &node->v.document.children;
			if ( !children )
				return;
			for ( unsigned int i = 0; i < children->length; ++i )
				SerializeNode( static_cast<GumboNode *>( children->data[i] ), out, opts, depth + 1 );
		}

		/// <style> element: rebuilt through the stylesheet sanitizer, never serialised verbatim.
		inline void SerializeStyleElement( GumboNode *node, std::ostringstream &out, const Options &opts ) {
			if ( opts.blockStyle )
				return;

			std::string css;
			const GumboVector &children = node->v.element.children;
			for ( unsigned int i = 0; i < children.length; ++i ) {
				GumboNode *child = static_cast<GumboNode *>( children.data[i] );
				if ( child->type == GUMBO_NODE_TEXT || child->type == GUMBO_NODE_WHITESPACE || child->type == GUMBO_NODE_CDATA )
					css += child->v.text.text;
			}

			std::string clean;
			if ( opts.filterCss ) {
				clean = SanitizeStylesheet( css, opts );
			} else {
				// allow_css: no declaration filtering, but script vectors (behavior/HTC, expression(),
				// VBScript) are refused unconditionally, and the text must never be able to end the element.
				if ( CssHasScriptVector( css ) )
					return;
				size_t pos = 0;
				clean = css;
				while ( ( pos = clean.find( "</", pos ) ) != kNpos ) {
					clean.insert( pos + 1, "\\" );
					pos += 3;
				}
			}
			if ( clean.empty() )
				return;
			out << "<style>" << clean << "</style>";
		}

		/// Emits the (filtered) attributes of `elem`, including the safety attributes injected per tag.
		/// Shared by ordinary elements, <script> and the <html>/<body> wrappers of a full document.
		inline void EmitAttributes( GumboElement &elem, const std::string &tag, std::ostringstream &out, const Options &opts ) {
		GumboVector &attrs = elem.attributes;
		for ( unsigned int i = 0; i < attrs.length; ++i ) {
			GumboAttribute *attr = static_cast<GumboAttribute *>( attrs.data[i] );
			std::string attrName = ToLower( attr->name );
			std::string attrValue = attr->value ? attr->value : "";

			// Attributes we inject ourselves (a[rel], img[loading]) always win; a user-supplied one is dropped,
			// otherwise every re-parse of our own output would add a duplicate.
			{
				const auto &inj = InjectedAttributes();
				auto injIt0 = inj.find( tag );
				if ( injIt0 != inj.end() && injIt0->second.count( attrName ) )
					continue;
			}

			// Drop event handlers (on*) when blockEvents is active
			if ( opts.blockEvents && attrName.size() >= 2 && attrName[0] == 'o' && attrName[1] == 'n' )
				continue;

			// Drop globally blocked attributes (always - these have no safe use)
			if ( BlockedAttributes().count( attrName ) )
				continue;

			// Item 11: only well-formed attribute names are re-emitted (no quotes, '<', '/', '=' ...).
			if ( !AttrNameOk( attrName ) )
				continue;

			// VBScript, unconditionally:
			//  - `language` on ANY element selects the engine of its on* handlers in MSHTML
			//    (<button language=vbscript onclick="MsgBox 1">), so it is never emitted;
			//  - any value that spells the "vbscript:" scheme is dropped (href, src, on*, style...).
			if ( attrName == "language" || HasVbScriptScheme( attrValue ) )
				continue;

			// DOM clobbering: id/name must not shadow document/window/form members
			if ( opts.blockClobbering && ( attrName == "id" || attrName == "name" ) && IsClobberName( attrValue, opts ) )
				continue;

			// Validate URL attributes
			if ( opts.validateUrls && UrlAttributes().count( attrName ) ) {
				bool sub = SubresourceAttributes().count( attrName ) != 0 || ( tag == "link" && attrName == "href" );
				if ( !IsUrlSafeNorm( NormalizeUrl( attrValue ), opts, sub ) )
					continue;
				out << ' ' << attrName << "=\"" << EscapeAttr( attrValue ) << '"';
				continue;
			}

			// Handle style attribute
			if ( attrName == "style" ) {
				if ( opts.blockStyle )
					continue; // remove entirely
				if ( opts.filterCss ) {
					std::string safe = SanitizeStyle( attrValue, opts );
					if ( !safe.empty() )
						out << " style=\"" << EscapeAttr( safe ) << '"';
				} else if ( !CssHasScriptVector( attrValue ) ) {
					out << " style=\"" << EscapeAttr( attrValue ) << '"'; // pass through (script vectors still refused)
				}
				continue;
			}

			out << ' ' << attrName << "=\"" << EscapeAttr( attrValue ) << '"';
		}

		// Inject safety attributes (always)
		const auto &injected = InjectedAttributes();
		auto injIt = injected.find( tag );
		if ( injIt != injected.end() ) {
			for ( const auto &pair : injIt->second )
				out << ' ' << pair.first << "=\"" << EscapeAttr( pair.second ) << '"';
		}
		}

		/// Case-insensitive replace of `needle` (lower-case ASCII) in `s`.
		inline std::string ReplaceNoCase( const std::string &s, const std::string &needle, const std::string &repl ) {
			std::string out;
			out.reserve( s.size() );
			size_t i = 0;
			while ( i < s.size() ) {
				if ( i + needle.size() <= s.size() && ToLower( s.substr( i, needle.size() ) ) == needle ) {
					out += repl;
					i += needle.size();
				} else {
					out += s[i++];
				}
			}
			return out;
		}

		/**
		 * @brief <script> element (only reached with allow_scripts and a JavaScript language).
		 *
		 * Item 17: the body of <script> is raw text. It must NOT be HTML-escaped (`&&` / `<` would be
		 * corrupted) but it must never be able to end the element or switch the tokenizer state:
		 * `</script` becomes `<\/script` and `<!--` becomes `<\!--` (both neutral inside JS strings/regex).
		 */
		inline void SerializeScriptElement( GumboNode *node, std::ostringstream &out, const Options &opts ) {
			GumboElement &elem = node->v.element;
			std::string js;
			const GumboVector &children = elem.children;
			for ( unsigned int i = 0; i < children.length; ++i ) {
				GumboNode *child = static_cast<GumboNode *>( children.data[i] );
				if ( child->type == GUMBO_NODE_TEXT || child->type == GUMBO_NODE_WHITESPACE || child->type == GUMBO_NODE_CDATA )
					js += child->v.text.text;
			}
			js = ReplaceNoCase( js, "</script", "<\\/script" );
			js = ReplaceNoCase( js, "<!--", "<\\!--" );
			out << "<script";
			EmitAttributes( elem, "script", out, opts );
			out << '>' << js << "</script>";
		}

		inline void SerializeNode( GumboNode *node, std::ostringstream &out, const Options &opts, int depth ) {
			if ( !node || depth > kMaxDomDepth )
				return;

			if ( node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE ) {
				out << EscapeHtml( node->v.text.text );
				return;
			}
			if ( node->type == GUMBO_NODE_CDATA ) {
				// Item 11: was emitted raw, which could close the surrounding element or open new
				// markup after a round trip. Treated as ordinary text.
				out << EscapeHtml( node->v.text.text );
				return;
			}
			if ( node->type == GUMBO_NODE_DOCUMENT ) {
				SerializeChildren( node, out, opts, depth );
				return;
			}
			if ( node->type != GUMBO_NODE_ELEMENT )
				return;

			GumboElement &elem = node->v.element;
			if ( elem.tag == GUMBO_TAG_UNKNOWN ) {
				SerializeChildren( node, out, opts, depth );
				return;
			}

			const char *tagName = gumbo_normalized_tagname( elem.tag );
			std::string tag = tagName;

			// Unconditional: mutation-prone / IE-only elements
			if ( AlwaysBlockedTags().count( tag ) )
				return;

			// Blocked tags: drop entire subtree (only when blockScripts is active)
			if ( opts.blockScripts && BlockedTags().count( tag ) )
				return;

			// Unconditional: <meta http-equiv="Content-Script-Type" content="text/vbscript"> changes the
			// default language of every on* handler on the page (<meta> is only blocked with blockScripts).
			if ( tag == "meta" ) {
				const GumboVector &ma = elem.attributes;
				for ( unsigned int i = 0; i < ma.length; ++i ) {
					const GumboAttribute *a = static_cast<GumboAttribute *>( ma.data[i] );
					std::string v = a->value ? a->value : "";
					if ( NamesVbScript( v ) || CompactValue( v ).find( "content-script-type" ) != kNpos )
						return;
				}
			}

			// Unconditional: with allow_scripts only JavaScript <script> elements survive.
			// VBScript (any spelling), JScript.Encode and unknown languages are dropped.
			if ( tag == "script" && !ScriptLanguageIsJs( elem.attributes ) )
				return;
			if ( tag == "script" ) {
				SerializeScriptElement( node, out, opts );
				return;
			}

			// Structural tags: always unwrap (drop tag, keep children)
			if ( StructuralTags().count( tag ) ) {
				SerializeChildren( node, out, opts, depth );
				return;
			}

			// <style>: contents are rebuilt by the stylesheet sanitizer
			if ( tag == "style" ) {
				SerializeStyleElement( node, out, opts );
				return;
			}

			out << '<' << tag;
			EmitAttributes( elem, tag, out, opts );

			if ( VoidElements().count( tag ) ) {
				out << '>';
				return;
			}

			out << '>';
			SerializeChildren( node, out, opts, depth );
			out << "</" << tag << '>';
		}

		inline GumboNode *FindChildTag( GumboNode *parent, GumboTag tag ) {
			if ( !parent || parent->type != GUMBO_NODE_ELEMENT )
				return nullptr;
			GumboVector &children = parent->v.element.children;
			for ( unsigned int i = 0; i < children.length; ++i ) {
				GumboNode *child = static_cast<GumboNode *>( children.data[i] );
				if ( child->type == GUMBO_NODE_ELEMENT && child->v.element.tag == tag )
					return child;
			}
			return nullptr;
		}

		/// Item 16: DOCTYPE kept (it selects standards vs quirks mode). Name and identifiers are re-validated.
		inline std::string DoctypeString( const GumboDocument &d ) {
			if ( !d.has_doctype )
				return {};
			std::string name = ToLower( d.name ? d.name : "" );
			bool ok = !name.empty();
			for ( unsigned char c : name )
				if ( !std::isalnum( c ) )
					ok = false;
			if ( !ok )
				name = "html";
			auto clean = []( const char *p ) {
				std::string r;
				for ( ; p && *p; ++p ) {
					unsigned char c = (unsigned char)*p;
					if ( c < 0x20 || c == '"' || c == '\'' || c == '<' || c == '>' || c == '&' )
						continue;
					r += (char)c;
				}
				return r;
			};
			const std::string pub = clean( d.public_identifier ), sys = clean( d.system_identifier );
			std::string out = "<!DOCTYPE " + name;
			if ( !pub.empty() ) {
				out += " PUBLIC \"" + pub + "\"";
				if ( !sys.empty() )
					out += " \"" + sys + "\"";
			} else if ( !sys.empty() ) {
				out += " SYSTEM \"" + sys + "\"";
			}
			return out + ">";
		}

		inline GumboNode *FindBody( GumboNode *htmlRoot ) {
			return FindChildTag( htmlRoot, GUMBO_TAG_BODY );
		}

		inline GumboNode *FindHead( GumboNode *htmlRoot ) {
			return FindChildTag( htmlRoot, GUMBO_TAG_HEAD );
		}

	} // namespace detail


	// =============================================================================
	// Public API
	// =============================================================================

	/**
	 * @brief Decodes HTML entities in a string.
	 *
	 * Security primitive: must be called before URL validation to catch
	 * encoded attacks like &#106;avascript: and java\tscript:.
	 */
	inline std::string UnescapeHtml( const std::string &s ) {
		return detail::UnescapeHtml( s );
	}

	/**
	 * @brief Validates a URL for safe use in navigation or HTML attributes.
	 *
	 * Decodes entities, drops whitespace/control characters and treats `\` as `/`
	 * before checking.
	 * Allowed: empty, relative references, http://, https://, ftp:// (clean authority,
	 * no userinfo), mailto:. With allowLocal: file:///path (never UNC / file://host).
	 * Blocked: javascript:, vbscript:, data:, `//host`, `\\host\share`, unknown schemes.
	 */
	inline bool IsUrlSafe( const std::string &url, bool allowLocal = false ) {
		Options o;
		o.allowLocal = allowLocal;
		return detail::IsUrlSafeNorm( detail::NormalizeUrl( url ), o, false );
	}

	/**
	 * @brief Same as above, honouring Options (allowLocal, restrictHosts, allowedHosts).
	 *
	 * @param subresource true if the URL is loaded automatically (src, CSS url(), ...);
	 *                    only then is the host allowlist applied.
	 */
	inline bool IsUrlSafe( const std::string &url, const Options &opts, bool subresource ) {
		return detail::IsUrlSafeNorm( detail::NormalizeUrl( url ), opts, subresource );
	}

	/**
	 * @brief Sanitizes a CSS declaration list (the value of a style attribute).
	 *
	 * @param allowLocal If true, file:///path URLs inside url() values are allowed.
	 */
	inline std::string Style( const std::string &style, bool allowLocal = false ) {
		Options o;
		o.allowLocal = allowLocal;
		return detail::SanitizeStyle( style, o );
	}

	inline std::string Style( const std::string &style, const Options &opts ) {
		return detail::SanitizeStyle( style, opts );
	}

	/**
	 * @brief Sanitizes the text of a <style> element (a full stylesheet).
	 *
	 * Rules are rebuilt from validated parts; @import and unknown at-rules are dropped.
	 */
	inline std::string Stylesheet( const std::string &css, const Options &opts = {} ) {
		return detail::SanitizeStylesheet( css, opts );
	}

	namespace detail {

		/// Rough upper bound of element nesting (void/self-closing elements and comments are skipped).
		inline bool ExceedsNesting( const std::string &html, int limit ) {
			static const std::unordered_set<std::string> kVoid = {
					"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
			};
			int depth = 0;
			const size_t n = html.size();
			for ( size_t i = 0; i < n; ++i ) {
				if ( html[i] != '<' || i + 1 >= n )
					continue;
				if ( html.compare( i, 4, "<!--" ) == 0 ) {
					size_t e = html.find( "-->", i + 4 );
					if ( e == kNpos )
						break;
					i = e + 2;
					continue;
				}
				if ( html[i + 1] == '/' ) {
					if ( depth > 0 )
						--depth;
					continue;
				}
				if ( !std::isalpha( (unsigned char)html[i + 1] ) )
					continue;
				size_t j = i + 1;
				while ( j < n && ( std::isalnum( (unsigned char)html[j] ) || html[j] == '-' ) )
					++j;
				std::string name = ToLower( html.substr( i + 1, j - i - 1 ) );
				size_t gt = html.find( '>', j );
				bool selfClose = gt != kNpos && gt > 0 && html[gt - 1] == '/';
				if ( !kVoid.count( name ) && !selfClose && ++depth > limit )
					return true;
			}
			return false;
		}

		/// One parse + serialise pass over already pre-processed text.
		inline std::string SanitizePass( const std::string &clean, const Options &opts, bool fragment ) {
			GumboOutput *output = gumbo_parse( clean.c_str() );
			if ( !output )
				return {};
			std::ostringstream out;
			if ( fragment ) {
				if ( GumboNode *head = FindHead( output->root ) ) {
					const GumboVector &kids = head->v.element.children;
					for ( unsigned int i = 0; i < kids.length; ++i ) {
						GumboNode *child = static_cast<GumboNode *>( kids.data[i] );
						// Leading <style>/<script>/<link>/<meta>/<base> end up in <head> for gumbo; they go
						// through the normal serialiser, so blocked ones are dropped by the active Options.
						if ( child->type == GUMBO_NODE_ELEMENT && child->v.element.tag != GUMBO_TAG_TITLE )
							SerializeNode( child, out, opts, 1 );
					}
				}
				if ( GumboNode *body = FindBody( output->root ) )
					SerializeChildren( body, out, opts, 0 );
			} else {
				// Item 16: a complete document keeps its DOCTYPE and the (filtered) attributes of <html>/<body>
				// (class, style, lang, bgcolor...). <head> keeps <title>/<style> and whatever the Options allow.
				// Event handlers, URLs, ids and styles go through the same filters as every other element.
				if ( output->document && output->document->type == GUMBO_NODE_DOCUMENT )
					out << DoctypeString( output->document->v.document );
				GumboNode *root = output->root;
				if ( root && root->type == GUMBO_NODE_ELEMENT ) {
					out << "<html";
					EmitAttributes( root->v.element, "html", out, opts );
					out << "><head>";
					if ( GumboNode *head = FindHead( root ) )
						SerializeChildren( head, out, opts, 1 );
					out << "</head><body";
					GumboNode *body = FindBody( root );
					if ( body )
						EmitAttributes( body->v.element, "body", out, opts );
					out << '>';
					if ( body )
						SerializeChildren( body, out, opts, 1 );
					out << "</body></html>";
				}
			}
			gumbo_destroy_output( &kGumboDefaultOptions, output );
			return out.str();
		}

		/**
		 * @brief Sanitize with a mutation-XSS guard (round-trip stability).
		 *
		 * The output is what MSHTML will re-parse. If parsing and serialising the output again
		 * changes it, gumbo and the serialiser disagree about its structure, which is exactly how
		 * mutation XSS works. We iterate until a fixed point (at most 3 passes); output that never
		 * stabilises is discarded (empty string, fail closed).
		 */
		inline std::string SanitizeStable( const std::string &html, const Options &opts, bool fragment ) {
			if ( html.empty() || html.size() > kMaxHtmlBytes )
				return {};
			std::string clean = PreProcess( html );
			if ( ExceedsNesting( clean, kMaxNesting ) )
				return {};

			std::string cur = SanitizePass( clean, opts, fragment );
			for ( int pass = 0; pass < 3; ++pass ) {
				std::string next = SanitizePass( PreProcess( cur ), opts, fragment );
				if ( next == cur )
					return cur;
				cur = std::move( next );
			}
			return {};
		}

	} // namespace detail

	/**
	 * @brief Sanitizes a complete HTML document string.
	 *
	 * The result is always a full document: DOCTYPE (if any), `<html>` and `<body>` with their filtered
	 * attributes, and `<head>` with `<title>` and the sanitized `<style>` blocks.
	 *
	 * Inputs above 8 MiB or with absurd nesting are rejected (empty result). The result has been
	 * checked for round-trip stability (see detail::SanitizeStable).
	 *
	 * @param opts  Options controlling what is blocked or allowed.
	 */
	inline std::string Html( const std::string &html, const Options &opts = {} ) {
		return detail::SanitizeStable( html, opts, false );
	}

	/**
	 * @brief Sanitizes an HTML fragment (body content only).
	 *
	 * Use with document.write() to avoid injecting html/head/body wrappers
	 * into an already-open document. Sanitized <style> elements that gumbo placed
	 * in <head> (leading <style> blocks) are kept, in front of the body content.
	 * Same size / nesting / stability guarantees as Html().
	 *
	 * @param opts  Options controlling what is blocked or allowed.
	 */
	inline std::string HtmlFragment( const std::string &html, const Options &opts = {} ) {
		return detail::SanitizeStable( html, opts, true );
	}

	/**
	 * @brief true if the text mentions VBScript in ANY spelling (language/type values, "vbscript:" URLs,
	 *        ".vbs"/".vbe" files, entity- or whitespace-obfuscated forms).
	 *
	 * Used for content that bypasses the sanitizer (sanitize = false) and for local files before
	 * they are navigated to. Deliberately conservative: it also matches the word in plain text.
	 */
	inline bool ContainsVbScript( const std::string &text ) {
		std::string c = detail::PreProcess( text );
		c.erase( std::remove_if( c.begin(), c.end(), []( unsigned char ch ) { return ch <= 0x20 || ch == '"' || ch == '\'' || ch == '`'; } ), c.end() );
		c = detail::ToLower( detail::UnescapeHtml( c ) );
		return c.find( "vbscript" ) != std::string::npos || c.find( "vbs" ) != std::string::npos || c.find( "visualbasic" ) != std::string::npos || c.find( ".vbe" ) != std::string::npos;
	}

	/// true if the URL (after entity decoding / whitespace stripping) uses the vbscript: scheme.
	inline bool IsVbScriptUrl( const std::string &url ) {
		return detail::HasVbScriptScheme( url );
	}

} // namespace sanitize
