/**
 * @file binding.cpp
 * @brief Lua binding for Rain (Measure) structure.
 * @license GPL v2.0 License
 *
 * Implements the bridge between Lua and C++ `Measure` structure,
 * exposing a global `rain` object in the Lua environment.
 *
 * @details
 * The `rain` object:
 * - Is not instantiable from Lua
 * - Represents the current Rainmeter Measure instance
 * - Has lifetime controlled by Rainmeter
 * - Uses lightuserdata to avoid Lua memory management
 * - Provides methods for skin control, variable access, and geometry
 *
 * @example
 * @code{.lua}
 * -- Basic usage
 * rain:bang("!Refresh")
 * print(rain.hwnd)
 *
 * -- Variable manipulation
 * local path = rain:var("CURRENTPATH")
 * rain:setVar("MyVar", "123")
 *
 * -- Skin geometry
 * local x = rain:getX()
 * local rect = rain:getRect()
 * @endcode
 */


#include <charconv>
#include <codecvt>
#include <cstdio>
#include <lua.hpp>

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <utils/strings.hpp>
#include <utils/util.hpp>






/**
 * @brief Checks if the current skin window is in focus.
 *
 * This function returns **true** if the skin window managed by
 * the Rain instance is the active foreground window.
 *
 * @param L Lua state.
 * @return 1 (boolean) indicating whether the skin is focused.
 *
 * @example
 * @code{.lua}
 * if rain:isFocused() then
 *     print("Skin is focused")
 * end
 * @endcode
 */
static int isFocused( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	if ( !rain || !rain->hwnd || !IsWindow( rain->hwnd ) ) {
		lua_pushboolean( L, 0 );
		return 1;
	}

	HWND foreground = GetForegroundWindow();

	lua_pushboolean( L, foreground == rain->hwnd );
	return 1;
}



/**
 * @brief Lua: rain:getSkin(config) → lightuserdata | nil
 *
 * Returns the HWND of a loaded Rainmeter skin by config name.
 *
 * @param[in] config Skin config name (e.g. "illustro\\Clock").
 *
 * @example
 * @code{.lua}
 * local hwnd = rain:getSkin("illustro\\Clock")
 * if hwnd then
 *     print("HWND:", hwnd)
 * end
 * @endcode
 */
static int getSkin( lua_State *L ) {
	// Argument: config name
	const char *config_utf8 = luaL_checkstring( L, 2 );
	if ( !config_utf8 || !*config_utf8 ) {
		lua_pushnil( L );
		return 1;
	}

	// UTF-8 → UTF-16
	std::wstring wconfig = utf8_to_wstring( config_utf8 );
	if ( wconfig.empty() ) {
		lua_pushnil( L );
		return 1;
	}

	const WCHAR *configName = wconfig.c_str();

	HWND trayWnd = FindWindow( L"RainmeterTrayClass", NULL );
	if ( !trayWnd || !configName || !*configName ) {
		lua_pushnil( L );
		return 1;
	}

	COPYDATASTRUCT cds{};
	cds.dwData = 5101;
	cds.cbData = (DWORD)( ( wcslen( configName ) + 1 ) * sizeof( WCHAR ) );
	cds.lpData = (PVOID)configName;

	HWND hwnd = (HWND)SendMessage( trayWnd, WM_COPYDATA, 0, (LPARAM)&cds );

	if ( !hwnd || !IsWindow( hwnd ) ) {
		lua_pushnil( L );
		return 1;
	}

	lua_pushlightuserdata( L, hwnd );
	return 1;
}



/**
 * @brief Lua: rain:formula(option [, default]) → number
 *
 * Reads a mathematical formula from a skin option using RmReadFormula.
 * Resolves variables, mathematical functions and returns calculated value.
 *
 * @param L Lua state.
 * @return 1 (number) or 2 (nil + error).
 *
 * @param[in] option Skin option name containing formula.
 * @param[in] default Optional default value if option doesn't exist.
 *
 * @example
 * @code{.lua}
 * -- Skin.ini has: MyFormula=2 + 2 * 3
 * local result = rain:formula("#MyFormula#")
 * print(result)  -- 8
 *
 * -- With Rainmeter variables
 * -- Skin: [Variables] Radius=5
 * local area = rain:formula("pi * (#Radius# ^ 2)")
 * print(area)  -- 78.5398
 *
 * -- Default value if option doesn't exist
 * local value = rain:formula("MissingOption", 100)
 * print(value)  -- 100
 * @endcode
 */
static int readFormula( lua_State *L ) {
	Rain *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	if ( !rain || !rain->rm ) {
		lua_pushnil( L );
		return 1;
	}

	/* option (required) */
	const char *expr = luaL_checkstring( L, 2 );
	std::wstring wexpr = utf8_to_wstring( expr );

	/* default (optional, MUST be double) */
	double defaultValue = 0.0;
	if ( lua_gettop( L ) >= 3 )
		/* luaL_checknumber guarantees a lua_Number (double in LuaJIT) */
		defaultValue = static_cast<double>( luaL_checknumber( L, 3 ) );


	std::wstring measureName = RmGetMeasureName( rain->rm );

	/* Define temporary option */
	rain->bang( L"!SetOption " + measureName + L" __Formula " + L"\"(" + wexpr + L")\"" );

	/* Evaluate formula */
	double result = RmReadFormulaFromSection( rain->rm, measureName.c_str(), L"__Formula", defaultValue );

	lua_pushnumber( L, result );
	return 1;
}



/**
 * @brief Metamethod __tostring for rain object.
 *
 * Returns string in format: "module: 0xXXXXXXXX"
 *
 * @param L Lua state.
 * @return 1 (string representation).
 */
static int toString( lua_State *L ) {
	// The rain object is at index 1
	const void *ptr = lua_topointer( L, 1 );

	char buffer[64];
	std::snprintf( buffer, sizeof( buffer ), "module: %p", ptr );

	lua_pushstring( L, buffer );
	return 1;
}



/**
 * @brief Lua: rain:absPath(filePath) → string
 *
 * Converts relative path to absolute using RmPathToAbsolute.
 * Useful for resolving paths relative to current skin.
 *
 * @param L Lua state.
 * @return 1 (absolute path string) or nil on error.
 *
 * @param[in] filePath Path to convert (relative or absolute).
 *
 * @example
 * @code{.lua}
 * local path = rain:absPath("#CURRENTPATH#\\script.lua")
 * print(path)  -- "C:\\Users\\...\\Skins\\MySkin\\script.lua"
 *
 * local abs = rain:absPath("..\\Images\\bg.png")
 * print(abs)  -- Absolute path to image
 * @endcode
 */
static int absPath( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	if ( !rain || !rain->rm ) {
		lua_pushnil( L );
		return 1;
	}

	const char *path_utf8 = luaL_checkstring( L, 2 );
	if ( !path_utf8 || !*path_utf8 ) {
		lua_pushnil( L );
		return 1;
	}

	std::wstring result = rain->absPath( utf8_to_wstring( path_utf8 ) );
	if ( result.empty() ) {
		lua_pushnil( L );
		return 1;
	}

	std::string utf8 = wstring_to_utf8( result );
	lua_pushlstring( L, utf8.c_str(), utf8.length() );
	return 1;
}



/**
 * @brief Returns window rectangle using WinAPI.
 *
 * Lua: rain:getRect([hwnd]) → table
 *
 * If @p hwnd is provided (as lightuserdata), the function retrieves the
 * rectangle of that window. Otherwise, it falls back to the internal
 * rain->hwnd handle.
 *
 * @param L Lua state.
 * @return 1 (table with rect properties) or nil on failure.
 *
 * @param[in] hwnd Optional lightuserdata representing a valid HWND.
 *
 * @retval table Table with fields:
 *   - x: left position
 *   - y: top position
 *   - r: right position
 *   - w: width  (r - x)
 *   - b: bottom position
 *   - h: height (b - y)
 *
 * @retval nil Returned if:
 *   - No valid HWND is available
 *   - The window is invalid
 *   - GetWindowRect fails
 *
 * @example
 * @code{.lua}
 * -- Using internal hwnd
 * local rect = rain:getRect()
 *
 * -- Using external hwnd
 * local rect2 = rain:getRect(hwnd)
 *
 * if rect then
 *     print("Skin at:", rect.x, rect.y)
 *     print("Size:", rect.w, "x", rect.h)
 * end
 * @endcode
 */
static int getRect( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	if ( !rain ) {
		lua_pushnil( L );
		return 1;
	}

	/**
	 * Resolve target HWND.
	 *
	 * If argument #1 exists and is lightuserdata, treat it as HWND.
	 * Otherwise fallback to rain->hwnd.
	 */
	HWND target = nullptr;

	if ( !lua_isnoneornil( L, 2 ) && lua_islightuserdata( L, 2 ) )
		target = static_cast<HWND>( lua_touserdata( L, 2 ) );
	else
		target = rain->hwnd;

	// Validate window handle.
	if ( !target || !IsWindow( target ) ) {
		lua_pushnil( L );
		return 1;
	}

	/**
	 * Retrieve window rectangle.
	 */
	RECT rc;
	if ( !GetWindowRect( target, &rc ) ) {
		lua_pushnil( L );
		return 1;
	}

	// Create Lua result table.
	lua_newtable( L );

	// left (x)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.left ) );
	lua_setfield( L, -2, "x" );

	// top (y)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.top ) );
	lua_setfield( L, -2, "y" );

	// right (r)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.right ) );
	lua_setfield( L, -2, "r" );

	// width (w)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.right - rc.left ) );
	lua_setfield( L, -2, "w" );

	// bottom (b)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.bottom ) );
	lua_setfield( L, -2, "b" );

	// height (h)
	lua_pushinteger( L, static_cast<lua_Integer>( rc.bottom - rc.top ) );
	lua_setfield( L, -2, "h" );
	return 1;
}




/**
 * @brief Returns current skin X position (#CURRENTCONFIGX#).
 *
 * Lua: rain:getX() → number
 *
 * @param L Lua state.
 * @return 1 (integer) or nil.
 */
static int getX( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	lua_Integer v;
	if ( ! util::RmVarInt( rain, L"#CURRENTCONFIGX#", v ) )
		lua_pushnil( L );
	else
		lua_pushinteger( L, v );

	return 1;
}



/**
 * @brief Returns current skin Y position (#CURRENTCONFIGY#).
 *
 * Lua: rain:getY() → number
 *
 * @param L Lua state.
 * @return 1 (integer) or nil.
 */
static int getY( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	lua_Integer v;
	if ( !util::RmVarInt( rain, L"#CURRENTCONFIGY#", v ) )
		lua_pushnil( L );
	else
		lua_pushinteger( L, v );

	return 1;
}



/**
 * @brief Returns current skin width (#CURRENTCONFIGWIDTH#).
 *
 * Lua: rain:getW() → number
 *
 * @param L Lua state.
 * @return 1 (integer) or nil.
 */
static int getW( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	lua_Integer v;
	if ( !util::RmVarInt( rain, L"#CURRENTCONFIGWIDTH#", v ) )
		lua_pushnil( L );
	else
		lua_pushinteger( L, v );

	return 1;
}



/**
 * @brief Returns current skin height (#CURRENTCONFIGHEIGHT#).
 *
 * Lua: rain:getH() → number
 *
 * @param L Lua state.
 * @return 1 (integer) or nil.
 */
static int getH( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	lua_Integer v;
	if ( !util::RmVarInt( rain, L"#CURRENTCONFIGHEIGHT#", v ) )
		lua_pushnil( L );
	else
		lua_pushinteger( L, v );

	return 1;
}



/**
 * @brief Lua: rain:moveSkin(x, y [, config]) → nil
 *
 * Moves the skin window to the specified screen coordinates using
 * Rainmeter's !Move bang.
 *
 * @param L Lua state.
 * @return 0 (no return values).
 *
 * @param[in] x Target X coordinate (screen position).
 * @param[in] y Target Y coordinate (screen position).
 * @param[in] config Optional configuration name (default: current config).
 *
 * @example
 * @code{.lua}
 * -- Move current skin to position (100, 200)
 * rain:moveSkin(100, 200)
 *
 * -- Move specific config
 * rain:moveSkin(300, 400, "MyConfig.ini")
 *
 * -- Move to center of screen (example calculation)
 * local screenWidth = tonumber(rain:var("SCREENAREAWIDTH"))
 * local screenHeight = tonumber(rain:var("SCREENAREAHEIGHT"))
 * local skinWidth = rain:getW() or 200
 * local skinHeight = rain:getH() or 100
 *
 * local centerX = math.floor((screenWidth - skinWidth) / 2)
 * local centerY = math.floor((screenHeight - skinHeight) / 2)
 * rain:moveSkin(centerX, centerY)
 * @endcode
 *
 * @note Coordinates are screen-relative (0,0 is top-left corner).
 * @note If config is not specified, uses current skin config.
 * @see https://docs.rainmeter.net/manual/bangs/#Move
 */
static int moveSkin( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	if ( !rain || !rain->skin )
		return 0;

	// Get X coordinate (required)
	lua_Integer x = luaL_checkinteger( L, 2 );

	// Get Y coordinate (required)
	lua_Integer y = luaL_checkinteger( L, 3 );

	// Get optional config parameter
	std::wstring wconfig;
	if ( lua_gettop( L ) >= 4 && !lua_isnil( L, 4 ) ) {
		const char *config = luaL_checkstring( L, 4 );
		wconfig = utf8_to_wstring( config );
	}

	// Build !Move bang command
	std::wstring cmd = L"!Move " + std::to_wstring( x ) + L" " + std::to_wstring( y );

	if ( !wconfig.empty() )
		cmd += L" \"" + wconfig + L"\"";

	// Execute the bang
	rain->bang( cmd );

	return 0;
}



/**
 * @brief Implements Lua method `rain:bang(command)`
 *
 * Executes a Rainmeter bang/command through native API (`RmExecute`).
 *
 * Lua convention:
 * - Used with `:` operator (colon syntax)
 * - First argument (`self`) is the `rain` table
 * - Second argument is command string
 *
 * @param L Lua state.
 * @return Number of values returned to Lua (always 0).
 *
 * @param[in] command Rainmeter bang command(s).
 *
 * @example
 * @code{.lua}
 * -- Single argument: full command
 * rain:bang("!Refresh")
 *
 * -- Multiple arguments: concatenated
 * rain:bang("!SetVariable", "Counter", "42")
 * rain:bang("!ShowMeter", "MyMeter", "MyConfig.ini")
 * @endcode
 */
static int executeBang( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int argc = lua_gettop( L );

	if ( !rain || argc < 2 )
		return 0;

	// CASE 1: single argument → complete command
	if ( argc == 2 ) {
		const char *cmd = luaL_checkstring( L, 2 );
		std::wstring wcmd = utf8_to_wstring( cmd );

		if ( !wcmd.empty() )
			rain->bang( wcmd );

		return 0;
	}

	// CASE 2: multiple arguments → concatenate
	std::wstring cmd;

	for ( int i = 2; i <= argc; ++i ) {
		const char *arg = luaL_checkstring( L, i );
		std::wstring warg = utf8_to_wstring( arg );

		if ( ! cmd.empty() )
			cmd += L" ";

		// Quote only if needed
		if (( warg.find( L' ' ) != std::wstring::npos && warg.front() != L'"' && warg.back() != L'"' )
		|| ( warg.empty() )) {
			cmd += L"\"";
			cmd += warg;
			cmd += L"\"";
		}

		else
			cmd += warg;
	}

	rain->bang( cmd );
	return 0;
}



/**
 * @brief Metamethod __index for Lua `rain` object.
 *
 * Allows property access on `rain` object in Lua, such as:
 * - `rain.hwnd` : skin window handle
 * - `rain.name` : measure name
 *
 * If key is not recognized, returns nil.
 *
 * @param L Lua state.
 * @return Number of values returned to Lua (always 1).
 *
 * @param[in] key Property name to access.
 */
static int __index( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	const char *key = luaL_checkstring( L, 2 );

	if ( !rain ) {
		lua_pushnil( L );
		return 1;
	}

	// Property: hwnd
	if ( strcmp( key, "hwnd" ) == 0 ) {
		lua_pushlightuserdata( L, rain->hwnd );
		return 1;
	}
	// Measure name
	else if ( strcmp( key, "name" ) == 0 ) {
		// Obter nome do measure usando RmGetString
		const wchar_t *measureName = RmGetMeasureName( rain->rm );
		if ( measureName && *measureName ) {
			std::string utf8Name = wstring_to_utf8( measureName );
			lua_pushstring( L, utf8Name.c_str() );
		}

		else
			lua_pushstring( L, "" );


		return 1;
	}

	// Not found
	lua_pushnil( L );
	return 1;
}



/**
 * @brief Lua: rain:var(name [, value [, filePath]])
 *
 * Gets or sets a Rainmeter skin variable.
 *
 * When used as a getter, the variable value is retrieved and automatically
 * converted to the most appropriate Lua type.
 *
 * Supported conversions:
 * - **boolean**  → "true"/"false" (case-insensitive)
 * - **integer**  → numeric string without decimal
 * - **number**   → floating point value
 * - **formula**  → evaluated using the Rainmeter formula engine
 * - **string**   → returned as-is
 *
 * When used as a setter, the variable is updated through the Rainmeter API.
 *
 * @param L Lua state.
 * @return
 * - Getter: 1 value (boolean, number, or string) or nil if variable is empty.
 * - Setter: 1 string containing the value that was set.
 *
 * @param[in] name Variable name.
 * @param[in] value Optional value to assign.
 * @param[in] filePath Optional include file where the variable should be written.
 *
 * @details
 * If the variable contains a Rainmeter mathematical expression (e.g.
 * `(2+3)` or `(#Var# * 5)`), the expression is detected heuristically and
 * evaluated through the Rainmeter formula engine before returning the value.
 *
 * @overload
 * @code{.lua}
 * -- Getter
 * local value = rain:var("MyVariable")
 *
 * -- Setter
 * rain:var("MyVariable", "new value")
 *
 * -- Setter with include file
 * rain:var("Position", "100,200", "filePath.inc")
 *
 * -- Formula variable
 * -- [Variables]
 * -- Radius=(#Size# * 2)
 * local r = rain:var("Radius")
 * print(r) -- numeric result
 * @endcode
 */
static int variable( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int argc = lua_gettop( L );

	// param1: name (required)
	const char *name = luaL_checkstring( L, 2 );
	std::wstring wname = utf8_to_wstring( name );


	// Getter: rain:var(name)
	if ( argc == 2 ) {
		std::wstring value = rain->var( wname );

		if ( value.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		/* Detect Rainmeter formula */
		if ( util::isRainmeterFormula( value ) ) {
			/* Define temporary option */
			std::wstring measureName = RmGetMeasureName( rain->rm );
			rain->bang( L"!setOption " + measureName + L" __Formula " + L"\"(" + value.c_str() + L")\"" );

			/* Evaluate formula */
			double result = RmReadFormula( rain->rm, L"__Formula", 0.0 );

			lua_pushnumber( L, result );
			return 1;
		}


		std::string utf8 = wstring_to_utf8( value );

		/* Boolean */
		if ( _stricmp( utf8.c_str(), "true" ) == 0 ) {
			lua_pushboolean( L, 1 );
			return 1;
		}
		if ( _stricmp( utf8.c_str(), "false" ) == 0 ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		/* Integer */
		long long intVal;
		auto [ ptr, ec ] = std::from_chars( utf8.data(), utf8.data() + utf8.size(), intVal );

		if ( ec == std::errc() && ptr == utf8.data() + utf8.size() ) {
			lua_pushinteger( L, static_cast<lua_Integer>( intVal ) );
			return 1;
		}

		/* Float */
		double floatVal;
		auto [ ptrf, ecf ] = std::from_chars( utf8.data(), utf8.data() + utf8.size(), floatVal );

		if ( ecf == std::errc() && ptrf == utf8.data() + utf8.size() ) {
			lua_pushnumber( L, floatVal );
			return 1;
		}

		// Fallback string
		lua_pushstring( L, utf8.c_str() );
		return 1;
	}


	// Setter: rain:var(name, value [, config])
	const char *value = luaL_checkstring( L, 3 );
	std::wstring wvalue = utf8_to_wstring( value );

	std::wstring wconfig;
	if ( argc >= 4 && !lua_isnil( L, 4 ) ) {
		const char *config = luaL_checkstring( L, 4 );
		wconfig = utf8_to_wstring( config );
	}

	rain->setVar( wname, wvalue, wconfig );

	// Return the value set
	lua_pushstring( L, value );
	return 1;
}



/**
 * @brief Lua: rain:option(section, option [, default]) → value
 *
 * Reads a skin option from any section and automatically detects its type.
 * Returns boolean, integer, number, or string based on the option's content.
 *
 * @param L Lua state.
 * @return 1 value of appropriate Lua type (boolean/number/string).
 *
 * @param[in] section Section name (e.g., "MyMeter", "Variables").
 * @param[in] option Option name to read.
 * @param[in] default Optional default value if option doesn't exist.
 *
 * @note Type detection order:
 *       1. Boolean ("true"/"false", case-insensitive)
 *       2. Integer (no decimal point)
 *       3. Float (with decimal point)
 *       4. String (everything else)
 *       5. Strinf empty (nil)
 *
 * @example
 * @code{.lua}
 * -- Read various option types
 * local text = rain:option("MyMeter", "Text")        -- string
 * local size = rain:option("MyMeter", "FontSize")    -- number
 * local hidden = rain:option("MyMeter", "Hidden")    -- number
 * local x = rain:option("MyMeter", "X")              -- number
 *
 * -- With default value
 * local value = rain:option("Section", "Missing", "default")
 * local count = rain:option("Counter", "Value", 0)   -- number 0
 * local flag = rain:option("Flags", "Enabled", true) -- boolean true
 * @endcode
 */
static int option( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	if ( !rain || !rain->rm ) {
		lua_pushnil( L );
		return 1;
	}

	// section (required)
	const char *section_utf8 = luaL_checkstring( L, 2 );
	std::wstring wsection = utf8_to_wstring( section_utf8 );
	if ( wsection.empty() ) {
		lua_pushnil( L );
		return 1;
	}

	// option (required)
	const char *option_utf8 = luaL_checkstring( L, 3 );
	std::wstring woption = utf8_to_wstring( option_utf8 );
	if ( woption.empty() ) {
		lua_pushnil( L );
		return 1;
	}

	// default value (optional)
	std::wstring wdefault;
	BOOL hasDefault = FALSE;

	if ( lua_gettop( L ) >= 4 && !lua_isnil( L, 4 ) ) {
		if ( lua_isboolean( L, 4 ) )
			wdefault = lua_toboolean( L, 4 ) ? L"1" : L"0";

		else if ( lua_isnumber( L, 4 ) ) {
			// Preserve float precision
			double num = static_cast<double>( lua_tonumber( L, 4 ) );

			wchar_t buffer[64];
			swprintf( buffer, 64, L"%.15g", num );
			wdefault = buffer;
		}

		else if ( lua_isstring( L, 4 ) ) {
			const char *def_utf8 = lua_tostring( L, 4 );
			wdefault = utf8_to_wstring( def_utf8 );
		}

		else
			wdefault = L"";

		hasDefault = TRUE;
	}

	// Read option from Rainmeter
	// clang-format off
	const wchar_t *raw = RmReadStringFromSection(
		rain->rm,
		wsection.c_str(),
		woption.c_str(),
		hasDefault ? wdefault.c_str() : L"",
		TRUE
	);
	// clang-format on


	if ( !raw || !*raw ) {
		if ( !hasDefault || wdefault.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		raw = wdefault.c_str();
	}


	// UTF-16 → UTF-8
	std::string value = wstring_to_utf8( raw );

	// Boolean
	if ( _stricmp( value.c_str(), "true" ) == 0 ) {
		lua_pushboolean( L, 1 );
		return 1;
	}
	if ( _stricmp( value.c_str(), "false" ) == 0 ) {
		lua_pushboolean( L, 0 );
		return 1;
	}

	// Integer
	char *end = nullptr;
	long long intVal = std::strtoll( value.c_str(), &end, 10 );
	if ( end && *end == '\0' ) {
		lua_pushinteger( L, static_cast<lua_Integer>( intVal ) );
		return 1;
	}

	// Float
	char *endf = nullptr;
	double floatVal = std::strtod( value.c_str(), &endf );
	if ( endf && *endf == '\0' ) {
		lua_pushnumber( L, floatVal );
		return 1;
	}

	// Fallback: string
	lua_pushstring( L, value.c_str() );
	return 1;
}




// clang-format off
static const luaL_Reg rain_methods[] = {
	{ "bang", executeBang },
	{ "var", variable },
	{ "option", option },
	{ "formula", readFormula },
	{ "absPath", absPath },
	{ "getX", getX },
	{ "getY", getY },
	{ "getW", getW },
	{ "getH", getH },
	{ "getRect", getRect },
	{ "moveSkin", moveSkin },
	{ "getSkin", getSkin },
	{ "isFocused", isFocused },
	{ NULL, NULL }
};
// clang-format on


/**
 * @brief Expose global `rain` object to Lua environment.
 *
 * Creates a global Lua table named `rain`, associates methods and configures
 * a metatable for property access via __index.
 *
 * Must be called once during Measure initialization (Initialize).
 *
 * @param L Lua state.
 * @param rain Pointer to associated Measure instance.
 *
 * @post
 * - Global `rain` table exists in Lua
 * - All methods properly bound with upvalues
 * - Metatable configured with __index and __tostring
 */
void exposeRainToLua( lua_State *L, Rain *rain ) {
	// Create global `rain` table
	lua_newtable( L );

	// rain.__type = "RainJIT"
	lua_pushstring( L, "RainJIT" );
	lua_setfield( L, -2, "__type" );

	// Register methods using clean loop
	for ( const luaL_Reg *reg = rain_methods; reg->name; ++reg ) {
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, reg->func, 1 );
		lua_setfield( L, -2, reg->name );
	}


	// Create metatable
	lua_newtable( L );
	lua_pushlightuserdata( L, rain );

	// Configure __index
	lua_pushcclosure( L, __index, 1 );
	lua_setfield( L, -2, "__index" );

	// __tostring
	lua_pushcfunction( L, toString );
	lua_setfield( L, -2, "__tostring" );

	// Apply metatable to `rain` table
	lua_setmetatable( L, -2 );

	// Define as global
	lua_setglobal( L, "rain" );
}
