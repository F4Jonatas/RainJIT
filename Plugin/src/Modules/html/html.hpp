/**
 * @file html.hpp
 * @brief HTML parsing module interface.
 *
 * Provides read-only HTML DOM traversal using Gumbo.
 *
 * This module is designed for Lua userdata-based integration,
 * prioritizing performance and memory safety.
 */

#pragma once

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

} // namespace html
