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

namespace html {

	/**
	 * @brief Register HTML module into Lua package.preload.
	 *
	 * @param L Lua state
	 */
	void RegisterModule( lua_State *L );

} // namespace html
