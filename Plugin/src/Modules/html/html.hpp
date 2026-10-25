/**
 * @file html.hpp
 * @brief HTML parsing module interface.
 * @license GPL v2.0 License
 *
 * Provides read-only HTML DOM traversal using Gumbo.
 *
 * This module is designed for Lua userdata-based integration,
 * prioritizing performance and memory safety.
 */

#pragma once

#include <string>
#include <string_view>

#include <lua.hpp>

struct Rain;

namespace html {

	/**
	 * @brief Register HTML module into Lua package.preload.
	 *
	 * Stores Rain* in the Lua registry under "__html_rain" so that
	 * binding functions can retrieve it for RmLog calls.
	 *
	 * @param L    Lua state
	 * @param rain Rainmeter measure context
	 */
	void RegisterModule( lua_State *L, Rain *rain );

	/**
	 * @brief Parses HTML text and pushes the resulting HtmlDocument onto the Lua stack.
	 *
	 * Same parser as `html.parse()`, callable from C++ without `require("html")`.
	 * The html metatables are registered on demand, so it also works when the script
	 * never loaded the module. Errors are not logged here; the caller decides.
	 *
	 * @param[in]  L     Lua state.
	 * @param[in]  text  HTML source (UTF-8). It is copied by the parser.
	 * @param[out] err   Receives a human readable message on failure.
	 * @return `true` on success (exactly one value pushed), `false` on failure
	 *         (stack unchanged, @p err filled).
	 */
	bool Parse( lua_State *L, std::string_view text, std::string &err );

} // namespace html
