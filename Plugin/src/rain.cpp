/**
 * @file rain.cpp
 * @brief Implementation of Rain structure methods.
 * @license GPL v2.0 License
 *
 * Contains all logic related to:
 * - Lua lifecycle
 * - Update execution
 * - Deferred initialization
 * - Rainmeter interaction
 */

#include <chrono>
#include <thread>

#include <Includes/rain.hpp>
#include <utils/strings.hpp>
#include <utils/util.hpp>





/**
 * @brief Construct a Rain instance with safe defaults.
 */
Rain::Rain() :
	skin( nullptr ),
	rm( nullptr ),
	L( nullptr ),
	hwnd( nullptr ),
	ready( false ),
	initCalled( false ),
	timeInitialized( false ) {}




/**
 * @brief Destroy the Rain instance.
 *
 * Safely releases the Lua state.
 */
Rain::~Rain() {
	if ( L ) {
		lua_close( L );
		L = nullptr;
	}
}




/**
 * @brief Increment the update counter and apply safety wrap.
 * @return The new accumulated update count (raw unsigned long long).
 */
unsigned long long Rain::incrementAndGetUpdates() {
	accumulatedUpdates++;

	if ( accumulatedUpdates >= MAX_UPDATES_COUNT )
		accumulatedUpdates = 0;

	return accumulatedUpdates;
}




/**
 * @brief Per-frame update dispatcher.
 *
 * Calls Lua `rain:update(au, dt)` if present.
 */
void Rain::onUpdate( double deltaTime ) {
	if ( !ready )
		return;

	lua_getglobal( L, "rain" );

	if ( lua_istable( L, -1 ) ) {
		lua_getfield( L, -1, "update" );

		if ( lua_isfunction( L, -1 ) ) {
			lua_pushvalue( L, -2 );
			lua_pushinteger( L, static_cast<lua_Integer>( accumulatedUpdates ) );
			lua_pushnumber( L, deltaTime );

			if ( lua_pcall( L, 3, 0, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L, -1 );

				if ( err && *err ) {
					std::wstring werr = L"Error in rain:update(): " + utf8_to_wstring( err );
					RmLog( rm, LOG_WARNING, werr.c_str() );
				}

				lua_pop( L, 1 );
			}
		}

		else
			lua_pop( L, 1 );
	}

	lua_pop( L, 1 );
}




/**
 * @brief One-time Lua initialization.
 *
 * Executes rain:init() exactly once when the skin is ready.
 */
void Rain::onInit() {
	if ( !ready && IsWindow( hwnd ) )
		ready = true;

	if ( !ready || initCalled )
		return;

	initCalled = true;
	initScheduled.store( true, std::memory_order_relaxed );

	lua_getglobal( L, "rain" );

	if ( lua_istable( L, -1 ) ) {
		lua_getfield( L, -1, "init" );

		if ( lua_isfunction( L, -1 ) ) {
			lua_pushvalue( L, -2 );

			if ( lua_pcall( L, 1, 0, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L, -1 );
				if ( err && *err ) {
					std::wstring werr = L"Error in rain:init(): " + utf8_to_wstring( err );
					RmLog( rm, LOG_WARNING, werr.c_str() );
				}
				lua_pop( L, 1 );
			}
		}
		else
			lua_pop( L, 1 );
	}

	lua_pop( L, 1 );
}




/**
 * @brief Execute a Rainmeter bang safely.
 */
void Rain::bang( const std::wstring &cmd ) {
	if ( !cmd.empty() )
		RmExecute( skin, cmd.c_str() );
}




/**
 * @brief Resolve a Rainmeter variable to its final value.
 */
std::wstring Rain::var( const std::wstring &name ) {
	if ( !rm )
		return L"";

	// clang-format off
	std::wstring expr =
		util::IsValidRainmeterVar( name )
			? L"#" + name + L"#"
			: name;
	// clang-format on

	LPCWSTR result = RmReplaceVariables( rm, expr.c_str() );

	if ( !result || expr == result )
		return L"";

	return std::wstring( result );
}




/**
 * @brief Set or persist a Rainmeter variable.
 */
void Rain::setVar( const std::wstring &name, const std::wstring &value, const std::wstring &filePath ) {
	if ( !skin || name.empty() )
		return;

	// clang-format off
	std::wstring cmd =
		filePath.empty()
			? L"!SetVariable \"" + name + L"\" \"" + value + L"\""
			: L"!WriteKeyValue variables \"" + name + L"\" \"" + value + L"\" \"" + filePath + L"\"";
	// clang-format on

	RmExecute( skin, cmd.c_str() );
}




/**
 * @brief Schedule delayed execution of rain:init().
 *
 * Uses a detached thread to wait and then dispatch
 * execution back to Rainmeter.
 */
void Rain::scheduleInit() {
	if ( initScheduled.exchange( true ) )
		return;

	std::wstring bang = L"[&" + std::wstring( RmGetMeasureName( rm ) ) + L":dispatch(init)]";

	std::thread( [this, bang]() {
		std::this_thread::sleep_for( std::chrono::milliseconds( fadeDurationMs ) );

		RmExecute( skin, bang.c_str() );
	} ).detach();
}
