/**
 * @file wrapper.hpp
 * @brief Embedded Lua scripts for RainJIT plugin.
 * @license GPL v2.0 License
 *
 * This file contains Lua scripts defined as string literals
 * that are compiled and executed directly by LuaJIT runtime
 * during Measure initialization.
 *
 * @namespace luaWrapper
 * @brief Wrapper Lua script resources.
 */

#pragma once

namespace luaWrapper {

	/**
	 * @brief Console and basic utilities Lua script.
	 *
	 * Overrides global Lua `print` function to redirect output
	 * to Rainmeter log using `!Log` bang.
	 *
	 * @details
	 * The overridden print function:
	 * - Converts all arguments to strings
	 * - Joins them with tab characters
	 * - Sends to Rainmeter log at Debug level
	 *
	 * @example
	 * @code{.lua}
	 * print("Hello", "RainJIT", 123, {x = 1})
	 * -- Output in Rainmeter log (Debug level):
	 * -- Hello RainJIT 123 table: 0x...
	 * @endcode
	 *
	 * @note The original Lua print function is replaced.
	 */
	static const char *main = R"lua(
	-- Override global print() to log to Rainmeter
	function print( ... )
		local messages = {}
		local count = select( '#', ... )

		for i = 1, count do
			local v = select( i, ... )
			table.insert( messages, tostring( v ))
		end

		local message = table.concat( messages, '    ' )
		message = message:gsub( '"', '”' )
		rain:bang( '!Log "' .. message .. '" Debug' )
	end


	-- Loads a native Lua module implemented as a DLL.
	local ffi = require( 'ffi' )
	local path = {
		x64 = '#SKINSPATH#@Vault\\lua\\bin\\x64\\',
		x86 = '#SKINSPATH#@Vault\\lua\\bin\\x86\\'
	}


	function import( module )
		if package.loaded[ module ] then
			return package.loaded[ module ]
		end


		local file = rain:var(
			path[ ffi.arch ] ..
			module:gsub( '%.', '\\' ) ..
			'.dll'
		)

		local init = 'luaopen_' .. module:gsub( '%.', '_' )

		local loader, err = package.loadlib( file, init )
		if not loader then
			error( err )
		end

		local result = loader()

		if result == nil then
			result = true
		end

		package.loaded[ module ] = result
		return result
	end
)lua";

} // namespace luaWrapper
