/**
 * @file sanitize.hpp
 * @brief Header-only HTML, CSS, and URL sanitizer built on gumbo-parser.
 * @license GPL v2.0 License
 *
 * Shared utility for any module that needs to sanitize untrusted content
 * before injecting it into a hosted browser control (e.g. trident, and
 * future modules).
 *
 * @details
 * Ideologically influenced by leafo/web_sanitize (MIT), adapted for C++
 * with a blocklist model and gumbo-parser as the HTML backend.
 *
 * Three sanitization layers:
 *
 *   1. PreProcess — strips null bytes and dangerous control characters
 *      from the raw string before gumbo sees it. MSHTML ignores null bytes
 *      inside tag names (`<scr\0ipt>`); gumbo must agree on what it parses.
 *
 *   2. HTML tree walk — gumbo builds a full, browser-equivalent DOM tree,
 *      then we reconstruct the output from scratch visiting only safe nodes.
 *        - Blocked tags (script, iframe, object, etc.) and their entire
 *          subtree are dropped silently.
 *        - Structural tags (html, head, body) are unwrapped: their children
 *          are serialized but the tag itself is dropped. This prevents
 *          attribute injection via a second <body onload="..."> from API
 *          content being merged into the existing document body.
 *        - on* event handler attributes are dropped universally.
 *        - URL attributes (href, src, etc.) are validated with IsUrlSafe.
 *        - The `style` attribute is filtered per-declaration, not removed.
 *        - Safety attributes are injected on certain tags (rel=nofollow on <a>).
 *
 *   3. CSS filter (per-declaration) — for `style` attributes:
 *        a. CSS comments are stripped before processing. This defeats the
 *           comment-splitting attack where "expression(" is disguised by
 *           inserting a CSS comment in the middle of the keyword.
 *        b. Each declaration is checked against a pattern blocklist.
 *        c. Function calls in values are validated via CssIsValidFunc, which
 *           blocks by function name (expression, paint, element, -moz-binding)
 *           and validates URL-bearing functions (url, image, image-set) internally.
 *           All other functions (calc, var, rgb, linear-gradient, blur, etc.) pass.
 *        d. Known typed properties validate their value's type additionally.
 *
 * @par URL validation
 * `IsUrlSafe()` is exposed as a standalone utility for Navigate2 validation.
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
 * std::string safe = sanitize::HtmlFragment(rawHtml);  // for document.write
 * std::string safe = sanitize::Html(rawHtml);          // full document
 * std::string safe = sanitize::Style(rawCss);          // style attribute only
 * bool ok          = sanitize::IsUrlSafe(url);         // before Navigate2
 * std::string dec  = sanitize::UnescapeHtml(encoded);  // entity decode
 * @endcode
 *
 * @note Only one external header is required: #include <gumbo.h>
 */

#pragma once

#include <gumbo.h>

#include <algorithm>
#include <cctype>
#include <functional>
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
		bool blockStyle = false; ///< Remove style attribute entirely (overrides filterCss).
		bool filterCss = true; ///< Filter dangerous declarations inside style attribute.
		bool validateUrls = true; ///< Validate href/src/action attrs and url() in CSS.
		bool allowLocal = false; ///< Permit file:// URLs when validateUrls is active.
	};

	// =============================================================================
	// Internal implementation
	// =============================================================================
	namespace detail {

		// -------------------------------------------------------------------------
		// String helpers
		// -------------------------------------------------------------------------

		inline std::string ToLower( const std::string &s ) {
			std::string out = s;
			std::transform( out.begin(), out.end(), out.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
			return out;
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
				default:
					out += static_cast<char>( c );
					break;
				}
			}
			return out;
		}

		// -------------------------------------------------------------------------
		// HTML entity decoder
		// -------------------------------------------------------------------------

		/**
		 * @brief Decode HTML entities in a string.
		 *
		 * @details
		 * Security primitive — not just a display utility.
		 * Attackers encode `javascript:` as `&#106;avascript:` or `&#x6A;avascript:`
		 * to bypass naive string comparisons. We decode before protocol checking so
		 * we see exactly what the browser will see.
		 *
		 * Supports decimal numeric (&#60;), hex numeric (&#x3C;), and common named
		 * entities: &amp; &lt; &gt; &quot; &apos; &nbsp; &colon;
		 */
		inline std::string UnescapeHtml( const std::string &s ) {
			static const std::unordered_map<std::string, char> kNamed = {
					{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' }, { "colon", ':' },
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
							if ( cp > 0 && cp < 0x80 ) {
								out += static_cast<char>( cp );
							} else if ( cp < 0x800 ) {
								out += static_cast<char>( 0xC0 | ( cp >> 6 ) );
								out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
							} else if ( cp < 0x10000 ) {
								out += static_cast<char>( 0xE0 | ( cp >> 12 ) );
								out += static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
								out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
							} else {
								out += '?';
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
		 * Browsers strip embedded whitespace (\t \n \r) from URLs before processing.
		 * `java\tscript:` is equivalent to `javascript:` in MSHTML.
		 * We decode entities first, then strip those whitespace chars, then lowercase.
		 */
		inline std::string NormalizeUrl( const std::string &url ) {
			std::string decoded = UnescapeHtml( url );
			std::string out;
			out.reserve( decoded.size() );
			for ( unsigned char c : decoded ) {
				if ( c == '\t' || c == '\n' || c == '\r' || c == ' ' )
					continue;
				out += static_cast<char>( std::tolower( c ) );
			}
			return out;
		}

		inline bool IsUrlSafeNorm( const std::string &norm, bool allowLocal = false ) {
			if ( norm.empty() )
				return true;
			if ( norm[0] == '/' || norm[0] == '#' || norm[0] == '.' )
				return true;
			if ( norm.find( ':' ) == std::string::npos )
				return true; // relative, no protocol
			if ( norm.substr( 0, 7 ) == "http://" )
				return true;
			if ( norm.substr( 0, 8 ) == "https://" )
				return true;
			if ( norm.substr( 0, 7 ) == "mailto:" )
				return true;
			if ( norm.substr( 0, 6 ) == "ftp://" )
				return true;
			if ( allowLocal && norm.substr( 0, 7 ) == "file://" )
				return true;
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
		// CSS type helpers
		// -------------------------------------------------------------------------

		inline bool CssIsNumber( const std::string &v ) {
			if ( v.empty() )
				return false;
			size_t i = 0;
			if ( v[i] == '-' || v[i] == '+' )
				++i;
			bool hasDigit = false;
			while ( i < v.size() && ( std::isdigit( (unsigned char)v[i] ) || v[i] == '.' ) ) {
				hasDigit = true;
				++i;
			}
			if ( !hasDigit )
				return false;
			while ( i < v.size() && ( std::isalpha( (unsigned char)v[i] ) || v[i] == '%' ) )
				++i;
			return i == v.size();
		}

		inline bool CssIsIdent( const std::string &v ) {
			if ( v.empty() )
				return false;
			for ( unsigned char c : v ) {
				if ( c == '(' || c == ')' || c == ':' || c == ';' || c == '{' || c == '}' )
					return false;
			}
			return true;
		}

		inline bool CssIsColor( const std::string &v ) {
			if ( v.empty() )
				return false;
			std::string lo = ToLower( v );
			if ( lo[0] == '#' ) {
				size_t len = lo.size() - 1;
				if ( len != 3 && len != 4 && len != 6 && len != 8 )
					return false;
				return std::all_of( lo.begin() + 1, lo.end(), []( unsigned char c ) { return std::isxdigit( c ); } );
			}
			static const std::vector<std::string> kColorFuncs = {
					"rgb(", "rgba(", "hsl(", "hsla(", "hwb(", "lch(", "oklch(", "lab(", "oklab(", "color(",
			};
			for ( const auto &fn : kColorFuncs ) {
				if ( lo.substr( 0, fn.size() ) == fn && lo.back() == ')' ) {
					auto inner = lo.substr( fn.size(), lo.size() - fn.size() - 1 );
					return std::all_of( inner.begin(), inner.end(), []( unsigned char c ) { return std::isdigit( c ) || c == ',' || c == ' ' || c == '.' || c == '%' || c == '/' || std::isalpha( c ); } );
				}
			}
			return CssIsIdent( lo );
		}

		/**
		 * @brief Validates a CSS function value against a function blocklist.
		 *
		 * @details Strategy (inspired by web_sanitize css_whitelist ideology):
		 *  - Block by function name: expression, paint, element, -moz-binding, -ms-behavior.
		 *  - For URL-bearing functions (url, image, image-set, cross-fade): validate
		 *    the inner URL with IsUrlSafeNorm.
		 *  - Everything else passes. The list of safe CSS functions is enormous and
		 *    grows with every CSS spec (calc, min, max, clamp, var, env, rgb, hsl,
		 *    hwb, lch, oklch, linear-gradient, blur, translate, rotate, etc.).
		 *    Blocking by name is more correct than trying to enumerate them all.
		 *
		 * @param v Lowercased, trimmed CSS value containing a function call.
		 * @return true if the function is safe to use.
		 */
		inline bool CssIsValidFunc( const std::string &v, bool allowLocal = false ) {
			auto paren = v.find( '(' );
			if ( paren == std::string::npos || v.back() != ')' )
				return false;

			std::string name = v.substr( 0, paren );

			static const std::unordered_set<std::string> kBlocked = {
					"expression", // IE: executes JavaScript directly
					"paint", // Houdini CSS Paint API worklet
					"element", // renders arbitrary DOM subtree as background image
					"-moz-binding", // XBL binding (old Gecko)
					"-ms-behavior", // IE behavior (HTC files)
			};
			if ( kBlocked.count( name ) )
				return false;

			static const std::unordered_set<std::string> kUrlBearing = {
					"url",
					"image",
					"image-set",
					"cross-fade",
			};
			if ( kUrlBearing.count( name ) ) {
				std::string inner = v.substr( paren + 1, v.size() - paren - 2 );
				if ( !inner.empty() && ( inner.front() == '\'' || inner.front() == '"' ) )
					inner = inner.substr( 1, inner.size() - 2 );
				return IsUrlSafeNorm( NormalizeUrl( inner ), allowLocal );
			}

			return true; // all other CSS functions are safe
		}

		// -------------------------------------------------------------------------
		// CSS per-property type validators
		// -------------------------------------------------------------------------

		using CssTypeValidator = std::function<bool( const std::string & )>;

		inline const std::unordered_map<std::string, CssTypeValidator> &CssPropertyValidators() {
			auto isNumOrIdent = []( const auto &v ) { return CssIsNumber( v ) || CssIsIdent( v ) || CssIsValidFunc( v ); };
			auto isNum = []( const auto &v ) { return CssIsNumber( v ) || CssIsValidFunc( v ); };
			auto isColor = []( const auto &v ) { return CssIsColor( v ) || CssIsValidFunc( v ); };
			auto isIdent = []( const auto &v ) { return CssIsIdent( v ) || CssIsValidFunc( v ); };
			auto isAny = []( const auto &v ) { return CssIsNumber( v ) || CssIsIdent( v ) || CssIsColor( v ) || CssIsValidFunc( v ); };

			static const std::unordered_map<std::string, CssTypeValidator> kV = {
					{ "margin", isNumOrIdent },
					{ "margin-top", isNumOrIdent },
					{ "margin-right", isNumOrIdent },
					{ "margin-bottom", isNumOrIdent },
					{ "margin-left", isNumOrIdent },
					{ "padding", isNum },
					{ "padding-top", isNum },
					{ "padding-right", isNum },
					{ "padding-bottom", isNum },
					{ "padding-left", isNum },
					{ "width", isNumOrIdent },
					{ "height", isNumOrIdent },
					{ "max-width", isNumOrIdent },
					{ "min-width", isNumOrIdent },
					{ "max-height", isNumOrIdent },
					{ "min-height", isNumOrIdent },
					{ "font-size", isNumOrIdent },
					{ "font-weight", isNumOrIdent },
					{ "font-style", isIdent },
					{ "text-align", isIdent },
					{ "text-decoration", isIdent },
					{ "line-height", isNumOrIdent },
					{ "letter-spacing", isNumOrIdent },
					{ "word-spacing", isNumOrIdent },
					{ "text-transform", isIdent },
					{ "white-space", isIdent },
					{ "color", isColor },
					{ "background-color", isColor },
					{ "background-image", isAny },
					{ "background", isAny },
					{ "opacity", isNum },
					{ "border", isAny },
					{ "border-width", isNum },
					{ "border-style", isIdent },
					{ "border-color", isColor },
					{ "border-radius", isNum },
					{ "display", isIdent },
					{ "position", isIdent },
					{ "overflow", isIdent },
					{ "visibility", isIdent },
					{ "float", isIdent },
					{ "clear", isIdent },
					{ "z-index", isNum },
					{ "flex", isAny },
					{ "flex-direction", isIdent },
					{ "flex-wrap", isIdent },
					{ "justify-content", isIdent },
					{ "align-items", isIdent },
					{ "align-self", isIdent },
					{ "gap", isNum },
			};
			return kV;
		}

		// -------------------------------------------------------------------------
		// CSS sanitizer
		// -------------------------------------------------------------------------

		inline std::string SanitizeStyle( const std::string &style, bool allowLocal = false ) {
			// Strip CSS comments first.
			// This defeats the attack where "expression(" is split by inserting
			// a CSS comment between characters of the keyword.
			std::string noComments;
			noComments.reserve( style.size() );
			for ( size_t i = 0; i < style.size(); ) {
				if ( i + 1 < style.size() && style[i] == '/' && style[i + 1] == '*' ) {
					i += 2;
					while ( i + 1 < style.size() && !( style[i] == '*' && style[i + 1] == '/' ) )
						++i;
					if ( i + 1 < style.size() )
						i += 2;
				} else {
					noComments += style[i++];
				}
			}

			static const std::vector<std::string> kDangerous = {
					"expression(", "-moz-binding", "behavior:", "-ms-behavior", "@import", "url(javascript:", "url(vbscript:", "url(data:", "url('javascript:", "url(\"javascript:", "url('vbscript:", "url(\"vbscript:", "url('data:", "url(\"data:",
			};

			auto Compact = []( const std::string &s ) {
				std::string out;
				for ( unsigned char c : ToLower( s ) )
					if ( !std::isspace( c ) )
						out += static_cast<char>( c );
				return out;
			};

			auto Trim = []( const std::string &s ) -> std::string {
				size_t a = s.find_first_not_of( " \t\r\n" );
				size_t b = s.find_last_not_of( " \t\r\n" );
				return ( a == std::string::npos ) ? std::string{} : s.substr( a, b - a + 1 );
			};

			std::string result;
			std::istringstream ss( noComments );
			std::string decl;

			while ( std::getline( ss, decl, ';' ) ) {
				if ( decl.find_first_not_of( " \t\r\n" ) == std::string::npos )
					continue;

				// Pattern blocklist
				std::string compact = Compact( decl );
				bool dangerous = false;
				for ( const auto &pat : kDangerous ) {
					if ( compact.find( Compact( pat ) ) != std::string::npos ) {
						dangerous = true;
						break;
					}
				}
				if ( dangerous )
					continue;

				auto colon = decl.find( ':' );
				if ( colon == std::string::npos )
					continue;

				std::string prop = ToLower( Trim( decl.substr( 0, colon ) ) );
				std::string value = Trim( decl.substr( colon + 1 ) );
				if ( prop.empty() || value.empty() )
					continue;

				// Type validation for known properties
				const auto &validators = CssPropertyValidators();
				auto vit = validators.find( prop );

				auto ValidateToken = [allowLocal]( const std::string &token, const CssTypeValidator *validator ) -> bool {
					std::string lo = ToLower( token );
					if ( lo.find( '(' ) != std::string::npos )
						return CssIsValidFunc( lo, allowLocal );
					return validator ? ( *validator )( lo ) : ( CssIsNumber( lo ) || CssIsIdent( lo ) || CssIsColor( lo ) );
				};

				bool allValid = true;
				std::istringstream vs( value );
				std::string token;
				while ( vs >> token ) {
					if ( !ValidateToken( token, vit != validators.end() ? &vit->second : nullptr ) ) {
						allValid = false;
						break;
					}
				}
				if ( !allValid )
					continue;

				if ( !result.empty() )
					result += ';';
				result += prop + ':' + value;
			}
			return result;
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

		inline const std::unordered_set<std::string> &UrlAttributes() {
			static const std::unordered_set<std::string> s = {
					"href", "src", "action", "data", "poster", "background", "dynsrc", "lowsrc", "codebase",
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

		inline void SerializeNode( GumboNode *node, std::ostringstream &out, const Options &opts = {} );

		inline void SerializeChildren( GumboNode *node, std::ostringstream &out, const Options &opts = {} ) {
			const GumboVector *children = nullptr;
			if ( node->type == GUMBO_NODE_ELEMENT )
				children = &node->v.element.children;
			else if ( node->type == GUMBO_NODE_DOCUMENT )
				children = &node->v.document.children;
			if ( !children )
				return;
			for ( unsigned int i = 0; i < children->length; ++i )
				SerializeNode( static_cast<GumboNode *>( children->data[i] ), out, opts );
		}

		inline void SerializeNode( GumboNode *node, std::ostringstream &out, const Options &opts ) {
			if ( !node )
				return;

			if ( node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE ) {
				out << EscapeHtml( node->v.text.text );
				return;
			}
			if ( node->type == GUMBO_NODE_CDATA ) {
				out << node->v.text.text;
				return;
			}
			if ( node->type == GUMBO_NODE_DOCUMENT ) {
				SerializeChildren( node, out, opts );
				return;
			}
			if ( node->type != GUMBO_NODE_ELEMENT )
				return;

			GumboElement &elem = node->v.element;
			if ( elem.tag == GUMBO_TAG_UNKNOWN ) {
				SerializeChildren( node, out, opts );
				return;
			}

			const char *tagName = gumbo_normalized_tagname( elem.tag );
			std::string tag = tagName;

			// Blocked tags: drop entire subtree (only when blockScripts is active)
			if ( opts.blockScripts && BlockedTags().count( tag ) )
				return;

			// Structural tags: always unwrap (drop tag, keep children)
			if ( StructuralTags().count( tag ) ) {
				SerializeChildren( node, out, opts );
				return;
			}

			out << '<' << tag;

			GumboVector &attrs = elem.attributes;
			for ( unsigned int i = 0; i < attrs.length; ++i ) {
				GumboAttribute *attr = static_cast<GumboAttribute *>( attrs.data[i] );
				std::string attrName = ToLower( attr->name );
				std::string attrValue = attr->value ? attr->value : "";

				// Drop event handlers (on*) when blockEvents is active
				if ( opts.blockEvents && attrName.size() >= 2 && attrName[0] == 'o' && attrName[1] == 'n' )
					continue;

				// Drop globally blocked attributes (always — these have no safe use)
				if ( BlockedAttributes().count( attrName ) )
					continue;

				// Validate URL attributes
				if ( opts.validateUrls && UrlAttributes().count( attrName ) ) {
					if ( !IsUrlSafeNorm( NormalizeUrl( attrValue ), opts.allowLocal ) )
						continue;
					out << ' ' << attrName << "=\"" << EscapeAttr( attrValue ) << '"';
					continue;
				}

				// Handle style attribute
				if ( attrName == "style" ) {
					if ( opts.blockStyle )
						continue; // remove entirely
					if ( opts.filterCss ) {
						std::string safe = SanitizeStyle( attrValue, opts.allowLocal );
						if ( !safe.empty() )
							out << " style=\"" << EscapeAttr( safe ) << '"';
					} else {
						out << " style=\"" << EscapeAttr( attrValue ) << '"'; // pass through
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

			if ( VoidElements().count( tag ) ) {
				out << '>';
				return;
			}

			out << '>';
			SerializeChildren( node, out, opts );
			out << "</" << tag << '>';
		}

		inline GumboNode *FindBody( GumboNode *htmlRoot ) {
			if ( !htmlRoot || htmlRoot->type != GUMBO_NODE_ELEMENT )
				return nullptr;
			GumboVector &children = htmlRoot->v.element.children;
			for ( unsigned int i = 0; i < children.length; ++i ) {
				GumboNode *child = static_cast<GumboNode *>( children.data[i] );
				if ( child->type == GUMBO_NODE_ELEMENT && child->v.element.tag == GUMBO_TAG_BODY )
					return child;
			}
			return nullptr;
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
	 * Decodes entities and strips embedded whitespace before checking the protocol.
	 * Allowed: empty, relative (no colon), /, #, ., http://, https://, mailto:, ftp://.
	 * If allowLocal is true: file:// is also accepted.
	 * Blocked: javascript:, vbscript:, data:, unrecognized protocols.
	 */
	inline bool IsUrlSafe( const std::string &url, bool allowLocal = false ) {
		return detail::IsUrlSafeNorm( detail::NormalizeUrl( url ), allowLocal );
	}

	/**
	 * @brief Sanitizes a CSS style attribute value only.
	 *
	 * @param allowLocal If true, file:// URLs inside url() values are allowed.
	 */
	inline std::string Style( const std::string &style, bool allowLocal = false ) {
		return detail::SanitizeStyle( style, allowLocal );
	}

	/**
	 * @brief Sanitizes a complete HTML document string.
	 *
	 * @param opts  Options controlling what is blocked or allowed.
	 */
	inline std::string Html( const std::string &html, const Options &opts = {} ) {
		if ( html.empty() )
			return {};
		std::string clean = detail::PreProcess( html );
		GumboOutput *output = gumbo_parse( clean.c_str() );
		if ( !output )
			return {};
		std::ostringstream out;
		detail::SerializeNode( output->root, out, opts );
		gumbo_destroy_output( &kGumboDefaultOptions, output );
		return out.str();
	}

	/**
	 * @brief Sanitizes an HTML fragment (body content only).
	 *
	 * Use with document.write() to avoid injecting html/head/body wrappers
	 * into an already-open document.
	 *
	 * @param opts  Options controlling what is blocked or allowed.
	 */
	inline std::string HtmlFragment( const std::string &html, const Options &opts = {} ) {
		if ( html.empty() )
			return {};
		std::string clean = detail::PreProcess( html );
		GumboOutput *output = gumbo_parse( clean.c_str() );
		if ( !output )
			return {};
		std::ostringstream out;
		GumboNode *body = detail::FindBody( output->root );
		if ( body )
			detail::SerializeChildren( body, out, opts );
		gumbo_destroy_output( &kGumboDefaultOptions, output );
		return out.str();
	}

} // namespace sanitize
