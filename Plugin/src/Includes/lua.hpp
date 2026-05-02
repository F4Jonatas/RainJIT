/**
 * @file lua.hpp
 * @brief Lua utility functions for RainJIT plugin.
 * @license GPL v2.0 License
 *
 * Provides functions for Lua state management, error handling,
 * package path configuration, and module registration.
 */

#pragma once

#include <Includes/binding.hpp>
#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>

#include "modules/depot/depot.hpp"
#include "modules/fetch/fetch.hpp"
#include "modules/hotkey/hotkey.hpp"
#include "modules/webview/trident.hpp"
#include "modules/html/html.hpp"
#include "modules/xml/xml.hpp"
//#include "modules/browser/browser.hpp"
// #include "modules/interval/interval.hpp"





namespace Lua {

	/**
	 * @brief Lua error handler that generates a complete stack traceback.
	 * _Assumes LuaTraceback is used as the error handler for all Lua pcall invocations._
	 *
	 * This function is intended to be used as the message handler (`msgh`)
	 * parameter of `lua_pcall`. It captures the Lua call stack at the exact
	 * moment an error occurs and formats it using Lua's standard traceback
	 * mechanism.
	 *
	 * Internally, this function calls `luaL_traceback`, which is the same
	 * mechanism used by the standalone Lua interpreter (`lua.exe`) to produce
	 * stack traces. The resulting error object on the Lua stack is a single
	 * UTF-8 string containing both the error message and the full stack
	 * traceback.
	 *
	 * @note This function must be invoked *inside* the `lua_pcall` error
	 *       handling path. Tracebacks cannot be reliably reconstructed after
	 *       the Lua stack has unwound.
	 *
	 * @note When this handler is used, the error string already contains a
	 *       complete traceback. Additional stack inspection via
	 *       `lua_getstack` or similar mechanisms is unnecessary and may
	 *       produce misleading results.
	 *
	 * @param L Lua state.
	 * @return Always returns 1, leaving the formatted error message with
	 *         stack traceback on top of the Lua stack.
	 *
	 * @see lua_pcall
	 * @see luaL_traceback
	 */
	static inline int LuaTraceback( lua_State *L ) {
		const char *msg = lua_tostring( L, 1 );
		if ( msg ) {
			luaL_traceback( L, L, msg, 1 );
		} else {
			lua_pushliteral( L, "Lua Error (no message)" );
		}
		return 1;
	}



	/**
	 * @brief Append a directory to Lua package.path and/or package.cpath.
	 *
	 * This function safely concatenates new search paths without overwriting
	 * existing values.
	 *
	 * @param rain Rain context.
	 * @param luaPath Optional Lua path entry (e.g. "C:\\MyPlugin\\lua\\?.lua")
	 */
	static inline void addPackage( Rain *rain, const char *property, const char *luaPath ) {
		lua_getglobal( rain->L, "package" );
		if ( !lua_istable( rain->L, -1 ) ) {
			lua_pop( rain->L, 1 );
			return;
		}

		if ( luaPath && *luaPath ) {
			lua_getfield( rain->L, -1, property );
			const char *current = lua_tostring( rain->L, -1 );

			std::string newPath = current ? current : "";
			if ( !newPath.empty() && newPath.back() != ';' )
				newPath += ';';

			newPath += luaPath;

			lua_pop( rain->L, 1 );
			lua_pushlstring( rain->L, newPath.c_str(), newPath.length() );
			lua_setfield( rain->L, -2, property );
		}

		lua_pop( rain->L, 1 );
	}



	/**
	 * Register embedded Lua modules (e.g., meter) and expose global `rain` object to Lua
	 */
	static inline void registerModules( Rain *rain ) {
		hotkey::RegisterModule( rain->L, rain );
		RegisterDepotModule( rain->L, rain );
		fetch::RegisterModule( rain->L, rain );
		mshtml::RegisterModule( rain->L, rain );
		html::RegisterModule( rain->L );
		xml::RegisterModule( rain->L, rain );
		//browser::RegisterModule( rain->L, rain );
		// interval::RegisterModule(rain->L, rain);
		exposeRainToLua( rain->L, rain );
	}



	/**
	 * @brief Import and execute Lua script from file.
	 *
	 * Loads Lua code from file system and executes it.
	 * Uses Lua's built-in file loading mechanism.
	 *
	 * @param rain Rain context.
	 * @param filePath Path to Lua file (UTF-16).
	 * @return true on success, false on Lua error or file not found.
	 *
	 * @note Path is converted from UTF-16 to UTF-8 internally.
	 */
	static inline bool importFile( Rain *rain, const std::wstring &filePath ) {
		std::string utf8 = wstring_to_utf8( filePath );

		lua_pushcfunction( rain->L, LuaTraceback );
		int errfunc = lua_gettop( rain->L );

		if ( luaL_loadfile( rain->L, utf8.c_str() ) != LUA_OK || lua_pcall( rain->L, 0, LUA_MULTRET, errfunc ) != LUA_OK ) {
			lua_remove( rain->L, errfunc );
			return false;
		}

		lua_remove( rain->L, errfunc );
		return true;
	}



	/**
	 * @brief Import and execute Lua script from string.
	 *
	 * Loads Lua code from a buffer and executes it immediately.
	 * Used for embedded scripts and inline code execution.
	 *
	 * @param rain Rain context.
	 * @param script Lua source code (UTF-8).
	 * @param embedName Name for debug/error reporting.
	 * @return true on success, false on Lua error.
	 *
	 * @post On error, error message remains on Lua stack top.
	 */
	static inline bool importScript( Rain *rain, const char *script, const char *embedName ) {
		// Push error handler
		lua_pushcfunction( rain->L, LuaTraceback );
		int errfunc = lua_gettop( rain->L );

		// Load chunk
		if ( luaL_loadbuffer( rain->L, script, strlen( script ), embedName ) != LUA_OK ) {
			lua_remove( rain->L, errfunc ); // remove error handler
			return false;
		}

		// Call chunk using error handler
		if ( lua_pcall( rain->L, 0, LUA_MULTRET, errfunc ) != LUA_OK ) {
			lua_remove( rain->L, errfunc ); // remove error handler
			return false;
		}

		// Remove error handler after success
		lua_remove( rain->L, errfunc );
		return true;
	}



	/**
	 * @brief Log Lua error to Rainmeter log.
	 *
	 * Formats and logs Lua error from stack top with optional prefix.
	 *
	 * @param rain Rain context.
	 * @param prefix Error message prefix (UTF-16) or nullptr.
	 * @param level enum LOGLEVEL( LOG_ERROR | LOG_WARNING | LOG_NOTICE | LOG_DEBUG )
	 *
	 * @post Error message is popped from Lua stack.
	 */
	static inline void log( Rain *rain, const wchar_t *prefix, int level = LOG_ERROR ) {
		const char *err = lua_tostring( rain->L, -1 );
		std::wstring msg = prefix ? prefix : L"";

		if ( err && *err ) {
			std::wstring werr = utf8_to_wstring( err );
			if ( !werr.empty() ) {
				msg += werr;
			}
		}

		RmLog( rain->rm, level, msg.c_str() );
		lua_pop( rain->L, 1 );
	}



	/**
	 * @brief Log Lua error with complete stack trace.
	 *
	 * Similar to Python's traceback, showing full call stack with file,
	 * line numbers, and function names.
	 *
	 * @param rain Rain context.
	 * @param prefix Error message prefix (UTF-16) or nullptr.
	 * @param level Rainmeter log level.
	 * @param includeTrace Whether to include stack trace (default: true).
	 *
	 * @post Error message is popped from Lua stack.
	 *
	 * @example Output:
	 * Error in script.lua: attempt to call a nil value
	 * stack traceback:
	 * 	[C]: in function 'require'
	 * 	script.lua:15: in function 'foo'
	 * 	script.lua:10: in main chunk
	 */
	static inline void trace( Rain *rain, const wchar_t *prefix, int level = LOG_ERROR, bool includeTrace = true ) {
		std::wstring msg;

		if ( prefix && *prefix ) {
			msg += prefix;
			if ( msg.back() != L' ' )
				msg += L" ";
		}

		const char *err = lua_tostring( rain->L, -1 );
		if ( err && *err )
			msg += utf8_to_wstring( err );

		else
			msg += L"Lua Error: (no message)";


		RmLog( rain->rm, level, msg.c_str() );
		lua_pop( rain->L, 1 );
	}



	/**
	 * @brief Evaluate a Lua expression and return its result as a string.
	 *
	 * This function executes a Lua script or expression and returns the first
	 * return value converted to a wide string (UTF-16), suitable for direct use
	 * in Rainmeter measures or plugins. It is designed to be called repeatedly
	 * from Rainmeter (e.g., in an @ref Update() function) without leaking Lua
	 * stack items.
	 *
	 * The input script is automatically wrapped in a `return (...)` statement
	 * unless it already contains the literal `"return "`. This allows simple
	 * expressions like `"2 + 2"` to be evaluated and their result captured.
	 *
	 * @param rain   Pointer to the Rain context (contains Lua state and Rainmeter
	 *               instance).
	 * @param script Lua script/expression as a null-terminated UTF-16 string
	 *               (Windows `WCHAR`). The string is converted to UTF-8 before
	 *               being passed to the Lua interpreter.
	 *
	 * @return A pointer to a null-terminated UTF-16 string containing the
	 *         formatted result of the evaluation, or an empty string (`L""`) if
	 *         an error occurs or the script returns no value.
	 *
	 * @note The returned pointer points to a **thread-local static buffer**.
	 *       The string is valid until the next call to `eval` from the same
	 *       thread. Callers should copy the result immediately if they need to
	 *       retain it.
	 *
	 * @note Result strings are truncated to 4095 characters to avoid excessive
	 *       memory usage and potential buffer overflows in Rainmeter.
	 *
	 * @warning **No error handler is installed for `lua_pcall`.** If a runtime
	 *          error occurs, the error message is pushed onto the Lua stack but
	 *          is **not logged**; the function simply returns an empty string.
	 *          Compilation errors (syntax errors) are logged via `Lua::log`.
	 *          For debugging, consider using @ref importScript or manually
	 *          enabling a traceback handler.
	 *
	 * @par Type Conversion Rules
	 * The first return value of the Lua script is converted to a wide string
	 * according to its type:
	 * - `string`: Converted from UTF-8 to UTF-16, truncated to 4095 characters.
	 * - `boolean`: `"true"` or `"false"`.
	 * - `number`: Converted using `std::to_wstring`, with trailing zeros removed
	 *             for integers. Special values become `"nan"`, `"inf"`, or `"-inf"`.
	 * - `nil`: `"nil"`.
	 * - `table`: `"[table]"`.
	 * - `function`: `"[function]"`.
	 * - `userdata`: `"[userdata]"`.
	 * - `thread`: `"[thread]"`.
	 * - `lightuserdata`: `"[lightuserdata]"`.
	 * - any other: `"[unknown]"`.
	 *
	 * If the script returns multiple values, only the first one is used.
	 *
	 * @par Error Handling
	 * - Conversion from UTF-16 to UTF-8 may fail; such failures are logged and
	 *   result in an empty return string.
	 * - Lua compilation errors are logged via `Lua::log` with the prefix
	 *   `"eval: Lua compile error: "`.
	 * - Runtime errors are **not logged** (design limitation). The Lua stack is
	 *   restored to its original state, so no permanent pollution occurs.
	 *
	 * @par Stack Management
	 * This function preserves the Lua stack across calls. It uses a RAII guard
	 * (@ref StackGuard) to restore the stack top to its original value before
	 * returning, even if an exception is thrown. This ensures that repeated calls
	 * from a measure do not accumulate garbage on the Lua stack.
	 *
	 * @see importScript, importFile, Lua::log
	 */
	static inline LPCWSTR eval( Rain *rain, const WCHAR *script ) {
		std::string luaCodeUTF8;
		try {
			luaCodeUTF8 = DetectAndConvertToUTF8( script );
		} catch ( ... ) {
			Lua::log( rain, L"eval: Failed to convert Lua code to UTF-8" );
			return L"";
		}

		if ( luaCodeUTF8.empty() )
			return L"";


		// Store current stack size to restore later
		int stackTop = lua_gettop( rain->L );

		// RAII helper para garantir limpeza da stack
		struct StackGuard {
			lua_State *L;
			int originalTop;
			StackGuard( lua_State *l, int top ) :
				L( l ),
				originalTop( top ) {
			}
			~StackGuard() {
				if ( L )
					lua_settop( L, originalTop );
			}
		} guard( rain->L, stackTop );

		// Prepare the Lua code - wrap in return statement if not present
		std::string wrappedCode;
		bool hasReturn = ( luaCodeUTF8.find( "return " ) != std::string::npos );

		if ( hasReturn ) {
			wrappedCode = luaCodeUTF8;
		} else {
			// Wrap in return statement to capture the result
			wrappedCode = "return (" + luaCodeUTF8 + ")";
		}

		// Load and execute the Lua code
		int loadResult = luaL_loadbuffer( rain->L, wrappedCode.c_str(), wrappedCode.length(), "eval" );
		if ( loadResult != LUA_OK ) {
			// Log Lua compilation error
			const char *err = lua_tostring( rain->L, -1 );
			if ( err && *err ) {
				std::wstring werr;
				try {
					werr = L"eval: Lua compile error: " + utf8_to_wstring( err );
				} catch ( ... ) {
					werr = L"eval: Lua compile error (failed to convert error message)";
				}
				Lua::log( rain, werr.c_str() );
			}
			return L"";
		}

		// Execute the loaded chunk
		int callResult = lua_pcall( rain->L, 0, LUA_MULTRET, 0 );
		if ( callResult != LUA_OK ) {
			// Log Lua runtime error
			const char *err = lua_tostring( rain->L, -1 );
			if ( err && *err ) {
				std::wstring werr;
				try {
					werr = L"eval: Lua runtime error: " + utf8_to_wstring( err );
				} catch ( ... ) {
					werr = L"eval: Lua runtime error (failed to convert error message)";
				}
				Lua::log( rain, werr.c_str() );
			}
			return L"";
		}

		// Get the number of returned values
		int numReturns = lua_gettop( rain->L ) - stackTop;

		// Convert first return value to string
		std::wstring result;

		if ( numReturns > 0 ) {
			try {
				// Get value at top of stack (last return value)
				int valueIndex = lua_gettop( rain->L );

				// Get type and convert safely
				int type = lua_type( rain->L, valueIndex );

				switch ( type ) {
				case LUA_TSTRING: {
					size_t len;
					const char *str = lua_tolstring( rain->L, valueIndex, &len );
					if ( str && len > 0 ) {
						// Limit size to prevent huge strings
						const size_t MAX_RESULT_SIZE = 4095;
						if ( len > MAX_RESULT_SIZE ) {
							result = L"[string too long]";
						} else {
							result = utf8_to_wstring( std::string( str, len ) );
						}
					}
					break;
				}
				case LUA_TBOOLEAN:
					result = lua_toboolean( rain->L, valueIndex ) ? L"true" : L"false";
					break;
				case LUA_TNUMBER: {
					lua_Number num = lua_tonumber( rain->L, valueIndex );
					// Check for NaN/Inf
					if ( std::isnan( num ) ) {
						result = L"nan";
					} else if ( std::isinf( num ) ) {
						result = ( num > 0 ) ? L"inf" : L"-inf";
					} else {
						result = std::to_wstring( num );
						// Remove trailing zeros for integers
						if ( result.find( L'.' ) != std::wstring::npos ) {
							while ( result.back() == L'0' )
								result.pop_back();
							if ( result.back() == L'.' )
								result.pop_back();
						}
					}
					break;
				}
				case LUA_TNIL:
					result = L"nil";
					break;
				case LUA_TTABLE:
					result = L"[table]";
					break;
				case LUA_TFUNCTION:
					result = L"[function]";
					break;
				case LUA_TUSERDATA:
					result = L"[userdata]";
					break;
				case LUA_TTHREAD:
					result = L"[thread]";
					break;
				case LUA_TLIGHTUSERDATA:
					result = L"[lightuserdata]";
					break;
				default:
					result = L"[unknown]";
					break;
				}
			} catch ( const std::exception &e ) {
				try {
					std::wstring werr = L"eval: Conversion error: " + utf8_to_wstring( e.what() );
					Lua::log( rain, werr.c_str() );
				} catch ( ... ) {
					Lua::log( rain, L"eval: Conversion error (unknown)" );
				}
				result = L"";
			} catch ( ... ) {
				Lua::log( rain, L"eval: Unexpected conversion error" );
				result = L"";
			}
		}


		// The stack will be automatically cleared by StackGuard (RAII).
		// Return result in thread-safe static buffer
		static thread_local std::wstring g_returnBuffer;
		g_returnBuffer = result;

		// Verificar tamanho do buffer (segurança extra)
		if ( g_returnBuffer.length() > 4095 ) {
			g_returnBuffer.resize( 4095 );
			g_returnBuffer.shrink_to_fit();
		}

		return g_returnBuffer.c_str();
	}
} // namespace Lua
