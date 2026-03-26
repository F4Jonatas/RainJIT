/**
 * @file depot.cpp
 * @brief Lua bindings for Depot INI file accessor as a Lua module.
 * @license GPL v2.0 License
 *
 * Implements Lua module for the Depot class, allowing Lua scripts to
 * read/write INI files through the `require("depot")` interface.
 *
 * @details
 * Each Depot instance is bound to a Lua table with the following methods:
 * - set(key, value)
 * - get(key [, default, preserve_string ])
 * - hasKey(key)
 * - delKey(key)
 * - delSection()
 *
 * @module depot
 * @usage local depot = require("depot")
 * local d = depot(["MySection", "config.ini"])
 */

#include "depot.hpp"

#include <Includes/rain.hpp>
#include <lua.hpp>

#include <memory>



#define DEPOT_META "depot.meta"


/// helpers
static Depot *checkDepot( lua_State *L ) {
	auto **ud = static_cast<Depot **>( luaL_checkudata( L, 1, DEPOT_META ) );
	luaL_argcheck( L, ud != nullptr && *ud != nullptr, 1, "Invalid Depot" );
	return *ud;
}




static void push_typed( lua_State *L, const std::string &value ) {
	const char *s = value.c_str();

	// boolean detection
	if ( _stricmp( s, "true" ) == 0 ) {
		lua_pushboolean( L, 1 );
		return;
	}

	if ( _stricmp( s, "false" ) == 0 ) {
		lua_pushboolean( L, 0 );
		return;
	}

	// detect if value looks like a float
	bool maybeFloat = false;

	for ( const char *p = s; *p; ++p ) {
		if ( *p == '.' || *p == 'e' || *p == 'E' ) {
			maybeFloat = true;
			break;
		}
	}

	if ( maybeFloat ) {
		char *end = nullptr;

		double f = std::strtod( s, &end );

		if ( end && *end == '\0' ) {
			lua_pushnumber( L, f );
			return;
		}
	}

	else {
		char *end = nullptr;

		long long i = std::strtoll( s, &end, 10 );

		if ( end && *end == '\0' ) {
			lua_pushinteger( L, (lua_Integer)i );
			return;
		}
	}

	lua_pushstring( L, s );
}



/**
 * @brief Lua: d:set(key, value)
 *
 * Writes a string value to the INI file.
 *
 * @param L Lua state.
 * @return Always 0 (no return values).
 *
 * @pre Depot instance in upvalue 1.
 * @param[in] key Key name (string).
 * @param[in] value Value to write (string).
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 * local d = depot("MySection", "config.ini")
 * d:set("Username", "JohnDoe")
 * @endcode
 */
static int depot_set( lua_State *L ) {
	Depot *D = checkDepot( L );

	const char *key = luaL_checkstring( L, 2 );

	if ( lua_isnil( L, 3 ) ) {
		D->delKey( key );
		return 0;
	}

	lua_getglobal( L, "tostring" );
	lua_pushvalue( L, 3 );
	lua_call( L, 1, 1 );

	const char *value = lua_tostring( L, -1 );

	if ( value )
		D->set( key, value );

	return 0;
}



/**
 * @brief Lua: d:get(key [, default [, raw]])
 *
 * Reads a value from the INI file. If raw is true, returns the raw string
 * without type conversion; otherwise applies automatic Lua type detection.
 *
 * @param L Lua state.
 * @return 1 value (converted or raw), or nil if not found and no default.
 *
 * @pre Depot instance in upvalue 1.
 * @param[in] key Key name (string).
 * @param[in] default Optional default value if key doesn't exist.
 * @param[in] raw Optional boolean; if true, return raw string (no conversion).
 *
 * @retval string|number|boolean The value if found (converted or raw).
 * @retval nil If key not found and no default provided.
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 * local d = depot("MySection", "config.ini")
 * d:set("code", "001")
 * print(d:get("code"))            -- 1 (number)
 * print(d:get("code", nil, true)) -- "001" (string)
 * @endcode
 */
static int depot_get( lua_State *L ) {
	Depot *D = checkDepot( L );

	const char *key = luaL_checkstring( L, 2 );

	const int argc = lua_gettop( L );
	const bool hasDefault = argc >= 3;
	const bool raw = (argc >= 4) && lua_toboolean( L, 4 );

	auto value = D->getOptional( key );

	if ( value ) {
		if ( raw )
			lua_pushstring( L, value->c_str() );
		else
			push_typed( L, *value );

		return 1;
	}

	if ( hasDefault ) {
		lua_pushvalue( L, 3 );
		return 1;
	}

	lua_pushnil( L );
	return 1;
}



/**
 * @brief Lua: d:has(key) → boolean
 *
 * Checks if a key exists in the INI section.
 *
 * @param L Lua state.
 * @return 1 boolean value.
 *
 * @pre Depot instance in upvalue 1.
 * @param[in] key Key name to check (string).
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 * local d = depot("MySection", "config.ini")
 * if d:has("Username") then
 *     print("Key exists")
 * end
 * @endcode
 */
static int depot_hasKey( lua_State *L ) {
	Depot *D = checkDepot( L );

	const char *key = luaL_checkstring( L, 2 );
	lua_pushboolean( L, D->hasKey( key ) );

	return 1;
}



/**
 * @brief Lua: d:remove(key)
 *
 * Deletes a key from the INI section.
 *
 * @param L Lua state.
 * @return Always 0 (no return values).
 *
 * @pre Depot instance in upvalue 1.
 * @param[in] key Key name to remove (string).
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 * local d = depot("MySection", "config.ini")
 * d:remove("ObsoleteKey")
 * @endcode
 */
static int depot_remove( lua_State *L ) {
	Depot *D = checkDepot( L );

	const char *key = luaL_checkstring( L, 2 );
	D->delKey( key );

	return 0;
}



/**
 * @brief Lua: d:clear()
 *
 * Deletes the entire section from the INI file.
 *
 * @param L Lua state.
 * @return Always 0 (no return values).
 *
 * @pre Depot instance in upvalue 1.
 *
 * @warning This removes all keys under the section.
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 * loca d = depot("MySection", "config.ini")
 * d:clear()  -- Clears entire section
 * @endcode
 */
static int depot_clear( lua_State *L ) {
	Depot *D = checkDepot( L );

	D->delSection();

	return 0;
}



/**
 * @brief Garbage collector for Depot Lua object.
 *
 * Called when Lua garbage collects the Depot table.
 *
 * @param L Lua state.
 * @return Always 0 (no return values).
 */
static int __gc( lua_State *L ) {
	auto **ud = static_cast<Depot **>( lua_touserdata( L, 1 ) );

	if ( ud && *ud ) {
		delete *ud;
		*ud = nullptr;
	}

	return 0;
}



static int depot_name( lua_State *L ) {
	Depot *D = checkDepot( L );
	lua_pushstring( L, D->Section.c_str() );
	return 1;
}



static int depot_filePath( lua_State *L ) {
	Depot *D = checkDepot( L );
	lua_pushstring( L, D->getFilePath().c_str() );
	return 1;
}



/**
 * @brief Lua: depot(section [, filepath])
 *
 * Creates a new Depot instance bound to a section and INI file.
 * Automatically creates directories if they don't exist.
 *
 * @param L Lua state.
 * @return 1 table representing the Depot instance.
 *
 * @pre Measure instance in upvalue 1.
 * @param[in] section Optional section name (default: "root").
 * @param[in] filepath Optional file path (default: "#@#data.cook").
 *
 * @note The default path uses Rainmeter's #@# variable (skin config folder).
 * @note Directories are created automatically if they don't exist.
 * @note Depot instances are allocated on the heap and managed by Lua's GC.
 *
 * @example
 * @code{.lua}
 * local depot = require("depot")
 *
 * -- Default section and file
 * local d1 = depot()
 *
 * -- Custom section, default file
 * local d2 = depot("Settings")
 *
 * -- Custom section and file
 * local d3 = depot("Data", "C:\\data.ini")
 * @endcode
 */
static int constructor( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	std::wstring defaultPath = RmReplaceVariables( rain->rm, L"#@#depot\\#CURRENTCONFIG#\\main.depot" );

	const char *section = luaL_optstring( L, 2, "root" );
	const char *path = luaL_optstring( L, 3, nullptr );

	/* resolve final file path */
	std::string finalPath = path ? path : wstring_to_utf8( defaultPath );

	std::wstring wpath = utf8_to_wstring( finalPath );

	/* ensure directory exists */
	if ( !wpath.empty() ) {
		std::filesystem::path fsPath( wpath );
		std::filesystem::path dir = fsPath.parent_path();

		if ( !dir.empty() ) {
			try {
				std::filesystem::create_directories( dir );
			} catch ( ... ) {
				RmLog( rain->rm, LOG_WARNING, L"Depot: failed creating directories" );
			}
		}
	}

	/* create Lua userdata */
	auto **ud = static_cast<Depot **>( lua_newuserdata( L, sizeof( Depot * ) ) );

	*ud = new Depot( section, finalPath );

	luaL_getmetatable( L, DEPOT_META );
	lua_setmetatable( L, -2 );

	return 1;
}



/**
 * @brief Collect all keys from the current section.
 *
 * @param L Lua state.
 * @return int Lua return count.
 */
static int depot_keys( lua_State *L ) {
	Depot *D = checkDepot( L );

	std::vector<wchar_t> buffer( 4096 );

	// clang-format off
	DWORD len = GetPrivateProfileStringW(
		D->SectionW.c_str(),
		nullptr,
		nullptr,
		buffer.data(),
		static_cast<DWORD>( buffer.size() ),
		D->getFilePathW().c_str()
	);
	// clang-format on

	lua_newtable( L );

	const wchar_t *ptr = buffer.data();
	int index = 1;

	while ( *ptr ) {
		std::string key = wstring_to_utf8( ptr );

		lua_pushstring( L, key.c_str() );
		lua_rawseti( L, -2, index++ );

		ptr += wcslen( ptr ) + 1;
	}

	return 1;
}



/**
 * @brief Collect all values from the current section.
 *
 * @param L Lua state.
 * @return int Lua return count.
 */
static int depot_values( lua_State *L ) {
	Depot *D = checkDepot( L );

	std::vector<wchar_t> buffer( 4096 );

	// clang-format off
	DWORD len = GetPrivateProfileStringW(
		D->SectionW.c_str(),
		nullptr,
		nullptr,
		buffer.data(),
		static_cast<DWORD>( buffer.size() ),
		D->getFilePathW().c_str()
	);
	// clang-format on

	lua_newtable( L );

	const wchar_t *ptr = buffer.data();
	int index = 1;

	while ( *ptr ) {
		std::string key = wstring_to_utf8( ptr );
		std::string value = D->get( key );

		push_typed( L, value );
		lua_rawseti( L, -2, index++ );

		ptr += wcslen( ptr ) + 1;
	}

	return 1;
}



/**
 * @brief Delete the depot file from disk.
 *
 * @param L Lua state.
 * @return int Lua return count.
 */
static int depot_delete( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	Depot *D = checkDepot( L );

	const std::wstring &path = D->getFilePathW();

	BOOL result = DeleteFileW( path.c_str() );

	if ( !result ) {
		DWORD err = GetLastError();

		std::wstring msg = L"Depot: failed deleting file: " + path + L" (error " + std::to_wstring( err ) + L")";

		RmLog( rain->rm, LOG_ERROR, msg.c_str() );

		lua_pushboolean( L, 0 );
		lua_pushinteger( L, (lua_Integer)err );

		return 2;
	}

	lua_pushboolean( L, 1 );
	return 1;
}


// clang-format off
static const luaL_Reg depot_methods[] = {
	{ "set", depot_set },
	{ "get", depot_get },
	{ "has", depot_hasKey },
	{ "remove", depot_remove },
	{ "clear", depot_clear },
	{ "keys", depot_keys },
	{ "values", depot_values },
	{ "delete", depot_delete },
	{ "name", depot_name },
	{ "filePath", depot_filePath },
	{ nullptr, nullptr }
};



static const luaL_Reg depot_meta[] = {
	{ "__gc", __gc },
	{ nullptr, nullptr }
};
// clang-format on




/**
 * @brief Lua module entry point for Depot.
 *
 * Called when Lua executes `require("depot")`.
 *
 * @param L Lua state.
 * @return 1 (module table with __call metamethod).
 *
 * @post Module table returned with __call metamethod that creates Depot instances.
 */
extern "C" int luaopen_depot( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	luaL_newmetatable( L, DEPOT_META );

	/* metamethods */
	luaL_setfuncs( L, depot_meta, 0 );

	/* methods */
	luaL_setfuncs( L, depot_methods, 0 );

	/* enable method lookup: userdata -> metatable */
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	lua_pop( L, 1 );

	/* module table */
	lua_newtable( L );

	lua_newtable( L );
	lua_pushlightuserdata( L, rain );
	lua_pushcclosure( L, constructor, 1 );
	lua_setfield( L, -2, "__call" );

	lua_setmetatable( L, -2 );

	return 1;
}



/**
 * @brief Register depot Lua module in package.preload.
 *
 * Allows embedded depot module to be loaded via `require()` in Lua,
 * without depending on external files.
 *
 * @param L Lua state.
 * @param rain Pointer to the Measure instance (passed as upvalue).
 */
void RegisterDepotModule( lua_State *L, Rain *rain ) {
	lua_getglobal( L, "package" );
	lua_getfield( L, -1, "preload" );

	lua_pushlightuserdata( L, rain );
	lua_pushcclosure( L, luaopen_depot, 1 );

	lua_setfield( L, -2, "depot" );

	lua_pop( L, 2 );
}
