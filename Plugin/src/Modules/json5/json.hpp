/**
 * @file json.hpp
 * @brief JSON / JSON5 reader and JSON writer exposed to Lua.
 * @license GPL v2.0 License
 *
 * Provides a safe C++ layer between Lua values and JSON text.
 * Reading is delegated to the header-only json5 library (MIT License, bundled
 * in third_party/json5, see THIRD_PARTY_LICENSES.md); writing is implemented
 * natively.
 *
 * @details
 * - All strings exposed to Lua are UTF-8
 * - Reader accepts JSON and JSON5 (comments, unquoted keys, single quotes,
 *   trailing commas)
 * - Writer always emits valid UTF-8 and finite numbers only
 * - JSON null is represented in Lua by the json.null sentinel
 * - Table keys are always written in sorted order (deterministic output)
 *
 * @example
 * @code
 * std::string err;
 * json::Decode( L, "{ a: 1 }", err );   // pushes a Lua table on success
 * @endcode
 */

#pragma once

#include <string>
#include <string_view>

struct lua_State;
struct Rain;



namespace json {

/// @brief Maximum nesting depth accepted by the reader and the writer.
inline constexpr int MaxDepth = 200;

/**
 * @struct EncodeOptions
 * @brief Options controlling how Lua values are written as JSON text.
 */
struct EncodeOptions {

	/// @brief Write one element per line, indented by #indent.
	bool pretty = false;

	/// @brief Indentation unit used when #pretty is true (spaces or tabs only).
	std::string indent = "  ";

	/// @brief Write keys that are valid identifiers without quotes (JSON5 style).
	bool json5 = false;

	/// @brief Escape every non-ASCII character as \\uXXXX (ASCII-only output).
	bool escapeUnicode = false;
};



/**
 * @brief Parses JSON/JSON5 text and pushes the result onto the Lua stack.
 *
 * The root of the document must be an object or an array. A leading UTF-8
 * BOM is ignored. JSON null becomes the json.null sentinel (light userdata
 * NULL); empty arrays and objects get a marker metatable so they round-trip.
 * The text must be valid UTF-8.
 *
 * @param L    Lua state.
 * @param text Document text (UTF-8).
 * @param err  Receives a human readable message on failure.
 * @return true on success (exactly one value pushed), false on failure
 *         (stack unchanged, @p err filled).
 */
bool Decode( lua_State *L, std::string_view text, std::string &err );



/**
 * @brief Writes the Lua value at @p index as JSON text.
 *
 * Tables whose keys are exactly 1..n are written as arrays, other tables as
 * objects. Tables marked with json.array() / json.object() keep their marker.
 *
 * @param L     Lua state.
 * @param index Stack index of the value to write.
 * @param opts  Writer options.
 * @param out   Receives the JSON text on success.
 * @param err   Receives a human readable message on failure.
 * @return true on success, false on failure (stack unchanged, @p err filled).
 */
bool Encode( lua_State *L, int index, const EncodeOptions &opts, std::string &out, std::string &err );

} // namespace json



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
extern "C" int luaopen_json( lua_State *L );



namespace json {

/**
 * @brief Register the json Lua module into package.preload.
 *
 * Allows the embedded json module to be loaded via `require("json")`
 * from any Lua script without depending on external files.
 *
 * Consistent with depot::RegisterModule, xml::RegisterModule and
 * html::RegisterModule.
 *
 * @param L    Lua state.
 * @param rain Rainmeter measure context (passed as upvalue to luaopen_json).
 *
 * @example
 * @code{.cpp}
 * json::RegisterModule( L, rain );
 * @endcode
 *
 * @example
 * @code{.lua}
 * local json = require("json")
 * local t = json.decode('{ name: "Rain", list: [1, 2, 3] }')
 * print(json.encode(t))  -- {"list":[1,2,3],"name":"Rain"}
 * @endcode
 */
void RegisterModule( lua_State *L, Rain *rain );

} // namespace json
