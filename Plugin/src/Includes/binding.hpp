/**
 * @file binding.hpp
 * @brief Lua binding entry point for Rainmeter Measure integration.
 * @license GPL v2.0 License
 *
 * This header declares the public interface used to expose the global
 * `rain` object into a Lua state. The actual binding implementation
 * (methods, metamethods, and Rainmeter API glue) is intentionally hidden
 * from consumers of this header.
 *
 * The exposed `rain` object represents the current Rainmeter Measure
 * instance and provides access to:
 * - Rainmeter bangs execution
 * - Skin variables and options
 * - Skin geometry and window handles
 * - Path resolution and formula evaluation
 *
 * @note
 * This header is intentionally minimal and should be included only by
 * translation units that need to call ::exposeRainToLua.
 */

#pragma once

#include <lua.hpp>

/* Forward declaration to avoid pulling implementation details */
struct Rain;

/**
 * @brief Exposes the global `rain` object to a Lua state.
 *
 * Creates a global Lua table named `rain` and binds all Rainmeter-related
 * methods and properties to it. The object lifetime is controlled by
 * Rainmeter; Lua only receives a lightweight reference.
 *
 * The function must be called exactly once during the Measure
 * initialization phase (typically in `Initialize()`).
 *
 * @param[in,out] L
 *	Pointer to an initialized Lua state.
 *
 * @param[in] rain
 *	Pointer to the associated Rainmeter Measure instance.
 *	Must remain valid for the entire lifetime of the Lua state.
 *
 * @post
 * - A global Lua table named `rain` exists.
 * - All Rainmeter bindings are available via the `rain` object.
 * - A metatable is attached, providing property access and tostring support.
 *
 * @warning
 * The `rain` object is not instantiable from Lua and must never be
 * manually destroyed or modified from the Lua side.
 *
 * @example
 * @code{.cpp}
 * lua_State* L = luaL_newstate();
 * exposeRainToLua(L, rain);
 * @endcode
 *
 * @example
 * @code{.lua}
 * rain:bang("!Refresh")
 * local x = rain:getX()
 * @endcode
 */
void exposeRainToLua( lua_State *L, Rain *rain );
