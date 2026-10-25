/**
 * @file json.cpp
 * @brief Lua bindings for the JSON / JSON5 reader and JSON writer.
 * @license GPL v2.0 License
 *
 * Implements the Lua module `json`, allowing Lua scripts to parse and
 * produce JSON through the `require("json")` interface.
 *
 * Third-party code: the reader uses a copy of the header-only json5 library
 * (Copyright (c) 2020 P-i-N, MIT License) kept in third_party/json5 with two
 * small documented fixes (see third_party/json5/RAINJIT.patch). The MIT
 * notice is preserved there and in THIRD_PARTY_LICENSES.md; no json5 code is
 * copied into this file. MIT is compatible with GPL v2.0.
 * The writer is an independent implementation (the upstream writer is not
 * used: it rounds doubles to 6 digits, emits raw control characters and can
 * leave the stream in hexadecimal mode).
 *
 * @details
 * The module table exposes:
 * - decode(text)
 * - encode(value [, options])
 * - load(path)
 * - save(path, value [, options])
 * - array([table])
 * - object([table])
 * - null
 *
 * @module json
 * @usage
 * local json = require("json")
 * local t = json.decode('{ name: "Rain", list: [1, 2, 3] }')
 */

#ifndef NOMINMAX
	#define NOMINMAX
#endif

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <Includes/rain.hpp>
#include <lua.hpp>
#include <utils/strings.hpp>

// json5 (MIT) lives in third_party/json5; add third_party to the include path.
// Its headers rely on <cstring> / <cctype> being included beforehand.
#include <json5/json5_input.hpp>

#if LUA_VERSION_NUM >= 502
	#define lua_objlen lua_rawlen
#endif

namespace json {



#define JSON_ARRAY_META  "json.array"
#define JSON_OBJECT_META "json.object"



/* ------------------------------------------------------------------------ */
/* helpers: markers                                                         */
/* ------------------------------------------------------------------------ */

enum class Marker { None, Array, Object };



/**
 * @brief Returns the json.array / json.object marker of the table at @p index.
 *
 * @param L     Lua state.
 * @param index Absolute stack index of a table.
 * @return Marker::Array, Marker::Object or Marker::None.
 */
static Marker markerOf( lua_State *L, int index ) {
	if ( !lua_getmetatable( L, index ) )
		return Marker::None;

	Marker result = Marker::None;

	luaL_getmetatable( L, JSON_ARRAY_META );
	if ( lua_rawequal( L, -1, -2 ) )
		result = Marker::Array;
	lua_pop( L, 1 );

	if ( result == Marker::None ) {
		luaL_getmetatable( L, JSON_OBJECT_META );
		if ( lua_rawequal( L, -1, -2 ) )
			result = Marker::Object;
		lua_pop( L, 1 );
	}

	lua_pop( L, 1 ); // table's metatable
	return result;
}



/// Sets the named marker metatable on the table at the top of the stack.
static void setMarker( lua_State *L, const char *name ) {
	luaL_getmetatable( L, name );
	lua_setmetatable( L, -2 );
}



/* ------------------------------------------------------------------------ */
/* helpers: UTF-8                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Decodes one UTF-8 sequence starting at s[i].
 *
 * Rejects stray continuation bytes, truncated sequences, overlong forms,
 * surrogates and values above U+10FFFF.
 *
 * @param s   Source bytes.
 * @param len Source length in bytes.
 * @param i   Offset of the first byte of the sequence (must be < len).
 * @param cp  Receives the code point.
 * @return Sequence length in bytes (1..4), or 0 if the sequence is invalid.
 */
static size_t decodeUtf8( const char *s, size_t len, size_t i, uint32_t &cp ) {
	const unsigned char c = static_cast<unsigned char>( s[i] );

	if ( c < 0x80 ) {
		cp = c;
		return 1;
	}

	size_t n = 0;
	if ( c >= 0xC2 && c <= 0xDF )
		n = 2;
	else if ( c >= 0xE0 && c <= 0xEF )
		n = 3;
	else if ( c >= 0xF0 && c <= 0xF4 )
		n = 4;

	if ( n == 0 || i + n > len )
		return 0;

	cp = c & ( 0xFFu >> ( n + 1 ) );

	for ( size_t k = 1; k < n; ++k ) {
		const unsigned char cc = static_cast<unsigned char>( s[i + k] );

		if ( ( cc & 0xC0 ) != 0x80 )
			return 0;

		cp = ( cp << 6 ) | ( cc & 0x3Fu );
	}

	if ( ( n == 3 && cp < 0x800 ) || ( n == 4 && ( cp < 0x10000 || cp > 0x10FFFF ) ) || ( cp >= 0xD800 && cp <= 0xDFFF ) )
		return 0;

	return n;
}



/// True if the whole buffer is well-formed UTF-8.
static bool isValidUtf8( std::string_view s ) noexcept {
	size_t i = 0;

	while ( i < s.size() ) {
		uint32_t cp    = 0;
		const size_t n = decodeUtf8( s.data(), s.size(), i, cp );

		if ( n == 0 )
			return false;

		i += n;
	}

	return true;
}



/* ------------------------------------------------------------------------ */
/* reader                                                                   */
/* ------------------------------------------------------------------------ */

/// Appends a Unicode code point to @p out as UTF-8.
static void appendUtf8( std::string &out, uint32_t cp ) {
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



/**
 * @brief Pushes a string produced by json5, repairing surrogate pairs.
 *
 * json5 encodes every \\uXXXX escape on its own, so a surrogate pair
 * (for example emoji) becomes two 3-byte sequences (CESU-8), which is not
 * valid UTF-8. Pairs are merged into one 4-byte sequence; lone surrogates
 * become U+FFFD.
 *
 * @param L Lua state.
 * @param s NUL terminated string owned by the json5 document.
 */
static void pushString( lua_State *L, const char *s ) {
	if ( !std::strchr( s, '\xED' ) ) {
		lua_pushstring( L, s );
		return;
	}

	std::string out;
	out.reserve( std::strlen( s ) );

	const unsigned char *p = reinterpret_cast<const unsigned char *>( s );

	while ( *p ) {
		if ( p[0] == 0xED && p[1] >= 0xA0 && p[1] <= 0xBF && ( p[2] & 0xC0 ) == 0x80 ) {
			const uint32_t hi = 0xD000u | ( ( p[1] & 0x3Fu ) << 6 ) | ( p[2] & 0x3Fu );

			if ( hi <= 0xDBFF && p[3] == 0xED && p[4] >= 0xB0 && p[4] <= 0xBF && ( p[5] & 0xC0 ) == 0x80 ) {
				const uint32_t lo = 0xD000u | ( ( p[4] & 0x3Fu ) << 6 ) | ( p[5] & 0x3Fu );
				appendUtf8( out, 0x10000u + ( ( hi - 0xD800u ) << 10 ) + ( lo - 0xDC00u ) );
				p += 6;
			} else {
				appendUtf8( out, 0xFFFD );
				p += 3;
			}
		} else
			out += static_cast<char>( *p++ );
	}

	lua_pushlstring( L, out.data(), out.size() );
}



/**
 * @brief Checks that brackets are not nested deeper than MaxDepth.
 *
 * The json5 parser is recursive, so this guard runs first to protect the
 * host process from stack exhaustion on hostile input. Strings and comments
 * are skipped so their content is not counted.
 */
static bool nestingWithinLimit( std::string_view s ) noexcept {
	const size_t n = s.size();
	int depth      = 0;
	size_t i       = 0;

	while ( i < n ) {
		const char c = s[i];

		if ( c == '"' || c == '\'' ) {
			for ( ++i; i < n && s[i] != c; ++i ) {
				if ( s[i] == '\\' )
					++i;
			}
		} else if ( c == '/' && i + 1 < n && s[i + 1] == '/' ) {
			while ( i < n && s[i] != '\n' )
				++i;
			continue;
		} else if ( c == '/' && i + 1 < n && s[i + 1] == '*' ) {
			i += 2;
			while ( i + 1 < n && !( s[i] == '*' && s[i + 1] == '/' ) )
				++i;
			i += 2;
			continue;
		} else if ( c == '{' || c == '[' ) {
			if ( ++depth > MaxDepth )
				return false;
		} else if ( c == '}' || c == ']' ) {
			if ( depth > 0 )
				--depth;
		}

		++i;
	}

	return true;
}



/// Pushes the json5 value @p v (and its children) onto the Lua stack.
static bool pushValue( lua_State *L, const json5::value &v, int depth, std::string &err ) {
	if ( depth > MaxDepth ) {
		err = "nesting too deep";
		return false;
	}

	if ( !lua_checkstack( L, 4 ) ) {
		err = "stack overflow";
		return false;
	}

	switch ( v.type() ) {
		case json5::value_type::null:
			lua_pushlightuserdata( L, nullptr );
			break;

		case json5::value_type::boolean:
			lua_pushboolean( L, v.get_bool() ? 1 : 0 );
			break;

		case json5::value_type::number:
			lua_pushnumber( L, v.get<double>() );
			break;

		case json5::value_type::string:
			pushString( L, v.get_c_str() );
			break;

		case json5::value_type::array: {
			const json5::array_view av( v );

			lua_createtable( L, static_cast<int>( av.size() ), 0 );

			int index = 1;
			for ( const json5::value &item : av ) {
				if ( !pushValue( L, item, depth + 1, err ) )
					return false;

				lua_rawseti( L, -2, index++ );
			}

			if ( av.empty() )
				setMarker( L, JSON_ARRAY_META );

			break;
		}

		case json5::value_type::object: {
			const json5::object_view ov( v );

			lua_createtable( L, 0, static_cast<int>( ov.size() ) );

			for ( const auto &kv : ov ) {
				pushString( L, kv.first );

				if ( !pushValue( L, kv.second, depth + 1, err ) )
					return false;

				lua_rawset( L, -3 );
			}

			if ( ov.empty() )
				setMarker( L, JSON_OBJECT_META );

			break;
		}
	}

	return true;
}



bool Decode( lua_State *L, std::string_view text, std::string &err ) {
	const int top = lua_gettop( L );

	try {
		// Ignore UTF-8 byte order mark.
		if ( text.size() >= 3 && std::memcmp( text.data(), "\xEF\xBB\xBF", 3 ) == 0 )
			text.remove_prefix( 3 );

		if ( !isValidUtf8( text ) ) {
			err = "invalid UTF-8 in input";
			return false;
		}

		if ( !nestingWithinLimit( text ) ) {
			err = "nesting too deep";
			return false;
		}

		json5::document doc;
		const json5::error e = json5::from_string( text, doc );

		if ( e.type != json5::error::none ) {
			const size_t count = sizeof( json5::error::type_string ) / sizeof( json5::error::type_string[0] );
			const char *name   = ( e.type >= 0 && static_cast<size_t>( e.type ) < count )
			                       ? json5::error::type_string[e.type]
			                       : "unknown error";

			err = std::string( name ) + " at line " + std::to_string( e.line ) + ", column " + std::to_string( e.column );
			return false;
		}

		if ( pushValue( L, doc, 0, err ) )
			return true;

	} catch ( const std::bad_alloc & ) {
		err = "out of memory";
	} catch ( ... ) {
		err = "internal error";
	}

	lua_settop( L, top );
	return false;
}



/* ------------------------------------------------------------------------ */
/* writer                                                                   */
/* ------------------------------------------------------------------------ */

/// Appends "\\uXXXX" for a 16-bit code unit.
static void appendEscapedUnit( std::string &out, uint32_t unit ) {
	static const char kHex[] = "0123456789abcdef";

	out += "\\u";
	out += kHex[( unit >> 12 ) & 0x0F];
	out += kHex[( unit >> 8 ) & 0x0F];
	out += kHex[( unit >> 4 ) & 0x0F];
	out += kHex[unit & 0x0F];
}



/**
 * @brief Appends a quoted, escaped JSON string; validates UTF-8 on the way.
 *
 * @param out           Destination buffer.
 * @param s             Source bytes (UTF-8).
 * @param len           Source length in bytes (embedded NUL allowed).
 * @param escapeUnicode Escape every non-ASCII character as \\uXXXX.
 * @return false if @p s is not valid UTF-8 (@p out is then unspecified).
 */
static bool appendString( std::string &out, const char *s, size_t len, bool escapeUnicode ) {
	out += '"';

	size_t i = 0;
	while ( i < len ) {
		const unsigned char c = static_cast<unsigned char>( s[i] );

		if ( c < 0x80 ) {
			switch ( c ) {
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\b': out += "\\b";  break;
				case '\f': out += "\\f";  break;
				case '\n': out += "\\n";  break;
				case '\r': out += "\\r";  break;
				case '\t': out += "\\t";  break;
				default:
					if ( c < 0x20 )
						appendEscapedUnit( out, c );
					else
						out += static_cast<char>( c );
			}

			++i;
			continue;
		}

		uint32_t cp    = 0;
		const size_t n = decodeUtf8( s, len, i, cp );

		if ( n == 0 )
			return false;

		if ( !escapeUnicode )
			out.append( s + i, n );
		else if ( cp <= 0xFFFF )
			appendEscapedUnit( out, cp );
		else {
			cp -= 0x10000;
			appendEscapedUnit( out, 0xD800 + ( cp >> 10 ) );
			appendEscapedUnit( out, 0xDC00 + ( cp & 0x3FF ) );
		}

		i += n;
	}

	out += '"';
	return true;
}



/**
 * @brief Appends a finite number using the shortest round-trip form.
 *
 * Integral values below 1e15 are written without fraction or exponent.
 */
static void appendNumber( std::string &out, double d ) {
	char buf[48];

	if ( d == std::floor( d ) && std::fabs( d ) < 1e15 ) {
		const auto r = std::to_chars( buf, buf + sizeof( buf ), static_cast<long long>( d ) );
		out.append( buf, r.ptr );
		return;
	}

#if defined( __cpp_lib_to_chars )
	const auto r = std::to_chars( buf, buf + sizeof( buf ), d );
	out.append( buf, r.ptr );
#else
	const int n = std::snprintf( buf, sizeof( buf ), "%.17g", d );
	out.append( buf, n > 0 ? static_cast<size_t>( n ) : 0 );
#endif
}



/// True if @p key can be written without quotes (same rule the reader accepts).
static bool isIdentifier( const std::string &key ) noexcept {
	if ( key.empty() )
		return false;

	for ( size_t i = 0; i < key.size(); ++i ) {
		const char c       = key[i];
		const bool alpha   = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_';
		const bool numeric = c >= '0' && c <= '9';

		if ( !alpha && !( numeric && i > 0 ) )
			return false;
	}

	return true;
}



/**
 * @class Writer
 * @brief Recursive Lua value to JSON text serializer.
 *
 * Never raises Lua errors: failures are reported through @c err so that no
 * C++ object is skipped by a longjmp.
 */
class Writer {
public:
	Writer( lua_State *L, const EncodeOptions &opts, std::string &out, std::string &err ) :
		L( L ),
		opts( opts ),
		out( out ),
		err( err ) {
	}

	/// Writes the value at absolute stack index @p index.
	bool write( int index, int depth ) {
		switch ( lua_type( L, index ) ) {
			case LUA_TNIL:
				out += "null";
				return true;

			case LUA_TBOOLEAN:
				out += lua_toboolean( L, index ) ? "true" : "false";
				return true;

			case LUA_TNUMBER: {
				const double d = lua_tonumber( L, index );

				if ( !std::isfinite( d ) )
					return fail( "cannot encode NaN or Infinity" );

				appendNumber( out, d );
				return true;
			}

			case LUA_TSTRING: {
				size_t len     = 0;
				const char *s = lua_tolstring( L, index, &len );

				if ( !appendString( out, s, len, opts.escapeUnicode ) )
					return fail( "invalid UTF-8 in string" );

				return true;
			}

			case LUA_TLIGHTUSERDATA:
				if ( lua_touserdata( L, index ) == nullptr ) {
					out += "null";
					return true;
				}

				return fail( "cannot encode value of type 'userdata'" );

			case LUA_TTABLE:
				return writeTable( index, depth );

			default:
				return fail( std::string( "cannot encode value of type '" ) + luaL_typename( L, index ) + "'" );
		}
	}

private:
	lua_State *L;
	const EncodeOptions &opts;
	std::string &out;
	std::string &err;

	bool fail( const std::string &message ) {
		err = message;
		return false;
	}

	void newline( int depth ) {
		if ( !opts.pretty )
			return;

		out += '\n';

		for ( int i = 0; i < depth; ++i )
			out += opts.indent;
	}

	bool writeTable( int index, int depth ) {
		if ( depth >= MaxDepth )
			return fail( "nesting too deep (circular reference?)" );

		if ( !lua_checkstack( L, 8 ) )
			return fail( "stack overflow" );

		const Marker marker = markerOf( L, index );

		if ( marker == Marker::Array )
			return writeArray( index, lua_objlen( L, index ), depth );

		if ( marker == Marker::Object )
			return writeObject( index, depth );

		// Structural detection: it is an array when the keys are exactly 1..n.
		size_t count    = 0;
		double maxIndex = 0;
		bool sequence   = true;

		lua_pushnil( L );
		while ( lua_next( L, index ) != 0 ) {
			++count;

			if ( sequence && lua_type( L, -2 ) == LUA_TNUMBER ) {
				const double k = lua_tonumber( L, -2 );

				if ( k >= 1 && k == std::floor( k ) ) {
					if ( k > maxIndex )
						maxIndex = k;
				} else
					sequence = false;
			} else
				sequence = false;

			lua_pop( L, 1 );
		}

		if ( sequence && count > 0 && maxIndex == static_cast<double>( count ) )
			return writeArray( index, count, depth );

		return writeObject( index, depth );
	}

	bool writeArray( int index, size_t count, int depth ) {
		if ( count == 0 ) {
			out += "[]";
			return true;
		}

		out += '[';

		for ( size_t i = 1; i <= count; ++i ) {
			if ( i > 1 )
				out += ',';

			newline( depth + 1 );

			lua_rawgeti( L, index, static_cast<int>( i ) );
			const bool ok = write( lua_gettop( L ), depth + 1 );
			lua_pop( L, 1 );

			if ( !ok )
				return false;
		}

		newline( depth );
		out += ']';
		return true;
	}

	bool writeObject( int index, int depth ) {
		struct Entry {
			std::string key;
			bool numeric;
			double number;
		};

		std::vector<Entry> entries;

		lua_pushnil( L );
		while ( lua_next( L, index ) != 0 ) {
			const int keyType = lua_type( L, -2 );

			if ( keyType == LUA_TSTRING ) {
				size_t len     = 0;
				const char *s = lua_tolstring( L, -2, &len );

				entries.push_back( Entry{ std::string( s, len ), false, 0.0 } );

			} else if ( keyType == LUA_TNUMBER ) {
				const double d = lua_tonumber( L, -2 );

				if ( !std::isfinite( d ) ) {
					lua_pop( L, 2 );
					return fail( "cannot encode Infinity as a table key" );
				}

				std::string text;
				appendNumber( text, d );
				entries.push_back( Entry{ std::move( text ), true, d } );

			} else {
				lua_pop( L, 2 );
				return fail( "table keys must be strings or numbers" );
			}

			lua_pop( L, 1 );
		}

		// Sorted by bytes: output is deterministic between runs.
		std::sort( entries.begin(), entries.end(), []( const Entry &a, const Entry &b ) {
			return a.key < b.key;
		} );

		for ( size_t i = 1; i < entries.size(); ++i ) {
			if ( entries[i].key == entries[i - 1].key )
				return fail( "duplicate key '" + entries[i].key + "' after conversion to string" );
		}

		if ( entries.empty() ) {
			out += "{}";
			return true;
		}

		out += '{';

		bool first = true;
		for ( const Entry &e : entries ) {
			if ( !first )
				out += ',';

			first = false;
			newline( depth + 1 );

			if ( opts.json5 && isIdentifier( e.key ) )
				out += e.key;
			else if ( !appendString( out, e.key.data(), e.key.size(), opts.escapeUnicode ) )
				return fail( "invalid UTF-8 in table key" );

			out += opts.pretty ? ": " : ":";

			if ( e.numeric )
				lua_pushnumber( L, e.number );
			else
				lua_pushlstring( L, e.key.data(), e.key.size() );

			lua_rawget( L, index );
			const bool ok = write( lua_gettop( L ), depth + 1 );
			lua_pop( L, 1 );

			if ( !ok )
				return false;
		}

		newline( depth );
		out += '}';
		return true;
	}
};



bool Encode( lua_State *L, int index, const EncodeOptions &opts, std::string &out, std::string &err ) {
	const int top = lua_gettop( L );

	if ( index < 0 && index > LUA_REGISTRYINDEX )
		index = top + index + 1;

	out.clear();

	try {
		Writer writer( L, opts, out, err );

		if ( writer.write( index, 0 ) )
			return true;

	} catch ( const std::bad_alloc & ) {
		err = "out of memory";
	} catch ( ... ) {
		err = "internal error";
	}

	lua_settop( L, top );
	out.clear();
	return false;
}



/* ------------------------------------------------------------------------ */
/* Lua bindings                                                             */
/* ------------------------------------------------------------------------ */

/// Pushes options[name] using a raw access (no metamethods).
static void rawOption( lua_State *L, int index, const char *name ) {
	lua_pushstring( L, name );
	lua_rawget( L, index );
}



/**
 * @brief Validates the options table at @p index.
 *
 * Raises a Lua error on invalid input, so it must run while no C++ object
 * with a destructor is alive in the caller.
 */
static void checkOptions( lua_State *L, int index ) {
	if ( lua_isnoneornil( L, index ) )
		return;

	luaL_checktype( L, index, LUA_TTABLE );

	rawOption( L, index, "indent" );

	const int type = lua_type( L, -1 );

	if ( type == LUA_TNUMBER ) {
		const double n = lua_tonumber( L, -1 );

		if ( !( n >= 0 && n <= 16 ) )
			luaL_argerror( L, index, "indent must be between 0 and 16" );

	} else if ( type == LUA_TSTRING ) {
		size_t len     = 0;
		const char *s = lua_tolstring( L, -1, &len );

		for ( size_t i = 0; i < len; ++i ) {
			if ( s[i] != ' ' && s[i] != '\t' )
				luaL_argerror( L, index, "indent string may only contain spaces and tabs" );
		}

	} else if ( type != LUA_TNIL )
		luaL_argerror( L, index, "indent must be a number or a string" );

	lua_pop( L, 1 );
}



/// Reads an options table already validated by checkOptions(). Never raises.
static EncodeOptions readOptions( lua_State *L, int index ) {
	EncodeOptions o;

	if ( lua_isnoneornil( L, index ) )
		return o;

	rawOption( L, index, "pretty" );
	const bool hasPretty = !lua_isnil( L, -1 );
	if ( hasPretty )
		o.pretty = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );

	// An explicit indent turns pretty printing on unless pretty = false.
	rawOption( L, index, "indent" );
	if ( lua_type( L, -1 ) == LUA_TNUMBER ) {
		o.indent.assign( static_cast<size_t>( lua_tonumber( L, -1 ) ), ' ' );
		if ( !hasPretty )
			o.pretty = true;
	} else if ( lua_type( L, -1 ) == LUA_TSTRING ) {
		size_t len     = 0;
		const char *s = lua_tolstring( L, -1, &len );
		o.indent.assign( s, len );
		if ( !hasPretty )
			o.pretty = true;
	}
	lua_pop( L, 1 );

	rawOption( L, index, "json5" );
	o.json5 = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );

	rawOption( L, index, "escapeUnicode" );
	o.escapeUnicode = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );

	return o;
}



/// Logs a failure through Rainmeter.
static void logFailure( Rain *rain, const std::string &message ) {
	const std::wstring msg = L"[RainJIT:Json] " + utf8_to_wstring( message );
	RmLog( rain->rm, LOG_ERROR, msg.c_str() );
}



/// Expands Rainmeter variables (#@#, #CURRENTPATH#, ...) in a UTF-8 path.
static std::filesystem::path resolvePath( Rain *rain, const std::string &utf8Path ) {
	std::wstring path = RmReplaceVariables( rain->rm, utf8_to_wstring( utf8Path ).c_str() );
	return std::filesystem::path( path );
}



/// Reads and decodes a file; pushes one value on success.
static bool loadFile( lua_State *L, Rain *rain, const std::string &rawPath, std::string &err ) {
	try {
		const std::filesystem::path path = resolvePath( rain, rawPath );

		std::ifstream ifs( path, std::ios::binary );
		if ( !ifs.is_open() ) {
			err = "could not open '" + rawPath + "'";
			return false;
		}

		const std::string content( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
		if ( ifs.bad() ) {
			err = "could not read '" + rawPath + "'";
			return false;
		}

		std::string parseError;
		if ( !Decode( L, content, parseError ) ) {
			err = rawPath + ": " + parseError;
			return false;
		}

		return true;

	} catch ( ... ) {
		err = "internal error while reading '" + rawPath + "'";
		return false;
	}
}



/**
 * @brief Writes @p data to a file through a temporary file and a rename,
 *        so an interrupted write never leaves a truncated file behind.
 *        Missing parent directories are created.
 */
static bool saveFile( Rain *rain, const std::string &rawPath, const std::string &data, std::string &err ) {
	try {
		const std::filesystem::path path = resolvePath( rain, rawPath );
		std::error_code ec;

		const std::filesystem::path dir = path.parent_path();
		if ( !dir.empty() )
			std::filesystem::create_directories( dir, ec );

		std::filesystem::path temp = path;
		temp += ".tmp";

		{
			std::ofstream ofs( temp, std::ios::binary | std::ios::trunc );
			if ( !ofs.is_open() ) {
				err = "could not create '" + rawPath + "'";
				return false;
			}

			ofs.write( data.data(), static_cast<std::streamsize>( data.size() ) );
			ofs.flush();

			if ( !ofs ) {
				ofs.close();
				std::filesystem::remove( temp, ec );
				err = "could not write '" + rawPath + "'";
				return false;
			}
		}

		std::filesystem::rename( temp, path, ec );
		if ( ec ) {
			std::filesystem::remove( temp, ec );
			err = "could not replace '" + rawPath + "'";
			return false;
		}

		return true;

	} catch ( ... ) {
		err = "internal error while writing '" + rawPath + "'";
		return false;
	}
}



/**
 * @brief Lua: json.decode(text) → value | nil, err
 *
 * Parses JSON or JSON5 text. The root must be an object or an array.
 * JSON null becomes json.null.
 *
 * @param L Lua state.
 * @return 1 value on success, or 2 values (nil, message) on failure.
 *
 * @param[in] text Document text (string, UTF-8).
 *
 * @retval table The decoded root.
 * @retval nil,string Error message with line and column.
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * local cfg, err = json.decode('{ width: 100, tags: ["a", "b"], // note\n }')
 * if not cfg then print(err) end
 * @endcode
 */
static int json_decode( lua_State *L ) {
	size_t len        = 0;
	const char *text = luaL_checklstring( L, 1, &len );

	std::string err;

	if ( !Decode( L, std::string_view( text, len ), err ) ) {
		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	return 1;
}



/**
 * @brief Lua: json.encode(value [, options]) → string | nil, err
 *
 * Writes a Lua value as JSON text. Keys are sorted, so the output is
 * deterministic.
 *
 * @param L Lua state.
 * @return 1 string on success, or 2 values (nil, message) on failure.
 *
 * @param[in] value Any Lua value except functions, threads and userdata.
 * @param[in] options Optional table: pretty, indent, json5, escapeUnicode.
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * print(json.encode({ a = 1, b = { 1, 2 } }))               -- {"a":1,"b":[1,2]}
 * print(json.encode({ a = 1 }, { pretty = true, indent = 4 }))
 * @endcode
 */
static int json_encode( lua_State *L ) {
	luaL_checkany( L, 1 );
	checkOptions( L, 2 );

	const EncodeOptions opts = readOptions( L, 2 );

	std::string out;
	std::string err;

	if ( !Encode( L, 1, opts, out, err ) ) {
		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	lua_pushlstring( L, out.data(), out.size() );
	return 1;
}



/**
 * @brief Lua: json.load(path) → value | nil, err
 *
 * Reads and decodes a file. Rainmeter variables such as `#@#` are expanded
 * in the path. A UTF-8 byte order mark is ignored. Failures are also logged.
 *
 * @param L Lua state.
 * @return 1 value on success, or 2 values (nil, message) on failure.
 *
 * @pre Measure instance in upvalue 1.
 * @param[in] path File path (string, UTF-8).
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * local cfg, err = json.load("#@#settings.json")
 * @endcode
 */
static int json_load( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	const char *rawPath = luaL_checkstring( L, 1 );

	const std::string path = rawPath;
	std::string err;

	if ( !loadFile( L, rain, path, err ) ) {
		logFailure( rain, err );

		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	return 1;
}



/**
 * @brief Lua: json.save(path, value [, options]) → true | nil, err
 *
 * Encodes a value and writes it to a file (followed by a newline).
 * Missing directories are created, Rainmeter variables are expanded and the
 * file is replaced atomically. Failures are also logged.
 *
 * @param L Lua state.
 * @return 1 boolean on success, or 2 values (nil, message) on failure.
 *
 * @pre Measure instance in upvalue 1.
 * @param[in] path File path (string, UTF-8).
 * @param[in] value Value to write.
 * @param[in] options Optional table, same as json.encode().
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * json.save("#@#settings.json", { width = 100 }, { pretty = true })
 * @endcode
 */
static int json_save( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	const char *rawPath = luaL_checkstring( L, 1 );
	luaL_checkany( L, 2 );
	checkOptions( L, 3 );

	const std::string path   = rawPath;
	const EncodeOptions opts = readOptions( L, 3 );

	std::string text;
	std::string err;

	if ( Encode( L, 2, opts, text, err ) ) {
		text += '\n';

		if ( saveFile( rain, path, text, err ) ) {
			lua_pushboolean( L, 1 );
			return 1;
		}
	}

	logFailure( rain, err );

	lua_pushnil( L );
	lua_pushlstring( L, err.data(), err.size() );
	return 2;
}



/// Shared implementation of json.array() and json.object().
static int markTable( lua_State *L, const char *meta ) {
	lua_settop( L, 1 );

	if ( lua_isnil( L, 1 ) ) {
		lua_newtable( L );
		lua_replace( L, 1 );
	} else
		luaL_checktype( L, 1, LUA_TTABLE );

	// Never replace an unrelated metatable.
	if ( markerOf( L, 1 ) == Marker::None && lua_getmetatable( L, 1 ) )
		return luaL_argerror( L, 1, "table already has a metatable" );

	luaL_getmetatable( L, meta );
	lua_setmetatable( L, 1 );

	lua_settop( L, 1 );
	return 1;
}



/**
 * @brief Lua: json.array([table]) → table
 *
 * Marks a table so it is always written as a JSON array (useful for empty
 * tables). Creates a new table when called without arguments.
 *
 * @param L Lua state.
 * @return 1 table.
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * print(json.encode({ items = json.array() }))  -- {"items":[]}
 * @endcode
 */
static int json_array( lua_State *L ) {
	return markTable( L, JSON_ARRAY_META );
}



/**
 * @brief Lua: json.object([table]) → table
 *
 * Marks a table so it is always written as a JSON object. Creates a new
 * table when called without arguments.
 *
 * @param L Lua state.
 * @return 1 table.
 */
static int json_object( lua_State *L ) {
	return markTable( L, JSON_OBJECT_META );
}



// clang-format off
static const luaL_Reg json_funcs[] = {
	{ "decode", json_decode },
	{ "encode", json_encode },
	{ "array",  json_array  },
	{ "object", json_object },
	{ nullptr,  nullptr     }
};
// clang-format on



} // namespace json (internal helpers)




/**
 * @brief Lua module entry point for json.
 *
 * Called when Lua executes `require("json")`.
 * Kept as `extern "C"` for semantic compatibility with Lua's module loader.
 *
 * @param L Lua state.
 * @return 1 (module table).
 *
 * @note Requires a Rain* pointer as upvalue 1.
 */
extern "C" int luaopen_json( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	/* marker metatables used by json.array() / json.object() */
	luaL_newmetatable( L, JSON_ARRAY_META );
	lua_pop( L, 1 );

	luaL_newmetatable( L, JSON_OBJECT_META );
	lua_pop( L, 1 );

	/* module table */
	lua_newtable( L );

	/* functions without upvalues */
	luaL_setfuncs( L, json::json_funcs, 0 );

	/* json.load / json.save need the Rain* upvalue */
	lua_pushlightuserdata( L, rain );
	lua_pushcclosure( L, json::json_load, 1 );
	lua_setfield( L, -2, "load" );

	lua_pushlightuserdata( L, rain );
	lua_pushcclosure( L, json::json_save, 1 );
	lua_setfield( L, -2, "save" );

	/* json.null sentinel (light userdata NULL) */
	lua_pushlightuserdata( L, nullptr );
	lua_setfield( L, -2, "null" );

	return 1;
}



namespace json {

	/**
	 * @brief Register the json Lua module into package.preload.
	 *
	 * Consistent with depot::RegisterModule, xml::RegisterModule and
	 * html::RegisterModule.
	 *
	 * @param L    Lua state.
	 * @param rain Rainmeter measure context (passed as upvalue to luaopen_json).
	 */
	void RegisterModule( lua_State *L, Rain *rain ) {
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_json, 1 );

		lua_setfield( L, -2, "json" );

		lua_pop( L, 2 );
	}

} // namespace json
