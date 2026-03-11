/**
 * @file RainJIT.cpp
 * @brief Rainmeter plugin with embedded LuaJIT runtime.
 * @license GPL v2.0 License
 *
 * This file implements the full Rainmeter Measure lifecycle:
 *  - Initialize
 *  - Reload
 *  - Update
 *  - ExecuteBang
 *  - Custom functions (eval, dispatch)
 *  - Finalize
 *
 * Each Measure instance owns a dedicated LuaJIT state and execution context.
 * All Lua execution is strictly performed on Rainmeter's main thread,
 * respecting Rainmeter's plugin threading model.
 *
 * @details
 * Architecture overview:
 * - One Rain instance per Measure
 * - One Lua state per Rain instance
 * - No shared state between Measures
 * - Deferred initialization via internal dispatcher
 *
 * Initialization model:
 * - Lua environment is created in Initialize()
 * - Configuration is read in Reload()
 * - rain:init() is deferred via scheduleInit()
 * - Deferred execution is routed through dispatch("init")
 *
 * This avoids:
 * - Window procedure hooks
 * - Blocking Rainmeter's main loop
 * - Unsafe cross-thread Lua calls
 *
 * Supported script modes:
 * - Embedded Lua script via [Script] option
 * - External Lua file
 * - Runtime execution via !CommandMeasure
 * - Expression evaluation via eval()
 *
 * @example
 * [RainJIT]
 * Measure=Plugin
 * Plugin=RainJIT
 * Script=#CURRENTPATH#\myscript.lua
 *
 * @see https://docs.rainmeter.net
 * @see https://luajit.org
 */



#include <Windows.h>
#include <algorithm>
#include <ctime>
#include <mutex>
#include <string>

#include <lua.hpp>

#include <RainmeterAPI.hpp>

#include <Includes/lua.hpp>
#include <Includes/rain.hpp>
#include <Includes/wrapper.hpp>
#include <Modules/fetch/fetch.hpp>
#include <Modules/hotkey/hotkey.hpp>
#include <Utils/filesystem.hpp>





/**
 * @brief Initialize the Rainmeter Measure.
 *
 * Called exactly once when the Measure is created.
 * Responsible for allocating the Measure structure and
 * initializing the LuaJIT runtime.
 *
 * Responsibilities:
 * - Allocate Rain instance
 * - Store Rainmeter contexts (rm, skin, hwnd)
 * - Create Lua state
 * - Initialize high-resolution timer
 * - Configure Lua package paths
 * - Register embedded Lua modules
 * - Execute embedded and/or configured Lua scripts
 *
 * @param[out] data
 * Opaque pointer owned by Rainmeter. Will store the Rain instance.
 *
 * @param[in] rm
 * Pointer to Rainmeter API context.
 *
 * @post
 * - *data contains a valid Rain pointer
 * - Lua state is initialized and ready
 * - Global `rain` object is available in Lua
 * - All configured scripts are executed
 */
PLUGIN_EXPORT void Initialize( void **data, void *rm ) {
	// Allocate new Measure instance
	Rain *rain = new Rain;
	*data = rain;

	// Initialize Measure fields
	rain->rm = rm;
	rain->skin = RmGetSkin( rm );
	rain->hwnd = RmGetSkinWindow( rm );
	rain->L = luaL_newstate();

	/** Initialize delta-time tracking */
	QueryPerformanceFrequency( &rain->freq );
	QueryPerformanceCounter( &rain->lastTick );
	rain->timeInitialized = true;

	// Open standard Lua libraries
	luaL_openlibs( rain->L );


	/// @note Add paths to Lua modules and scripts
	std::string vaultPath = wstring_to_utf8( rain->var( LR"(#SKINSPATH#@Vault\)" ) );
	std::string currPath = wstring_to_utf8( rain->var( L"CURRENTPATH" ) );
	std::string resPath = wstring_to_utf8( rain->var( L"@" ) );

	// clang-format off
	std::string luaPath =
		vaultPath + "lua\\?.lua;" +
		vaultPath + "lua\\?\\init.lua;" +
		resPath   + "lua\\?.lua;" +
		resPath   + "lua\\?\\init.lua;" +
		currPath  + "lua\\?.lua;" +
		currPath  + "lua\\?\\init.lua;" +
		currPath  + "?.lua;" +
		currPath  + "?\\init.lua;" ;

	std::string luaCPath =
		vaultPath + "lua\\bin\\?.dll;" +
		resPath   + "lua\\bin\\?.dll;" +
		currPath  + "lua\\bin\\?.dll;" ;
	// clang-format on

	Lua::addPackage( rain, "path", luaPath.c_str() );
	Lua::addPackage( rain, "cpath", luaCPath.c_str() );


	// Register embedded Lua modules (e.g., meter)
	Lua::registerModules( rain );

	// Load wrappers functions
	if ( !Lua::importScript( rain, luaWrapper::main, "embedded:wrapper.lua" ) )
		Lua::trace( rain, L"Error on importing wrapper.lua" );


	// Execute script configured via [Script] option
	std::wstring scriptParam = RmReadString( rm, L"Script", L"" );
	if ( !scriptParam.empty() ) {
		// Convert to absolute path
		const wchar_t *absolutePath = RmPathToAbsolute( rm, scriptParam.c_str() );

		if ( absolutePath && *absolutePath ) {
			// If it's an existing file, load as external script
			if ( fs::fileExists( absolutePath ) ) {
				if ( !Lua::importFile( rain, absolutePath ) ) {
					Lua::trace( rain, L"Error on importing file\n" );
				}
			}

			// Otherwise, treat as inline Lua script
			else {
				std::string script_utf8 = DetectAndConvertToUTF8( scriptParam );

				if ( !Lua::importScript( rain, script_utf8.c_str(), "embedded:script.lua" ) )
					Lua::trace( rain, L"Error on importing embedded:script.lua\n" );
			}
		}
	}
}



/**
 * @brief Reload Measure settings and configuration.
 *
 * Called when the skin is refreshed or reloaded.
 * Reads configuration values required during runtime.
 *
 * Currently responsible for:
 * - Resolving Rainmeter paths
 * - Reading FadeDuration from Rainmeter.ini
 *
 * The FadeDuration value is used to defer the execution
 * of rain:init() until Rainmeter finishes the skin fade-in
 * animation, ensuring stable window metrics.
 *
 * @note
 * This function does NOT execute Lua initialization directly.
 * Initialization is deferred via Rain::scheduleInit().
 *
 * @post
 * - rain->fadeDurationMs is updated
 * - Deferred initialization is scheduled
 */
PLUGIN_EXPORT void Reload( void *data, void *rm, double *maxValue ) {
	auto *rain = static_cast<Rain *>( data );

	/** Path to Rainmeter settings directory */
	std::wstring settingsPath = rain->var( L"SETTINGSPATH" );

	/** Current Rainmeter config (used as INI section name) */
	std::wstring config = rain->var( L"CURRENTCONFIG" );


	/**
	 * Read FadeDuration from Rainmeter.ini
	 *
	 * - File: <SETTINGSPATH>\Rainmeter.ini
	 * - Section: CURRENTCONFIG
	 * - Key: FadeDuration
	 * - Default: 200 (milliseconds)
	 */
	std::wstring iniPath = settingsPath;
	iniPath += L"Rainmeter.ini";

	// clang-format off
	WCHAR buffer[32] = {};
	GetPrivateProfileStringW(
		config.c_str(),  // Section
		L"FadeDuration", // Key
		L"200",          // Default (ms)
		buffer,
		32,
		iniPath.c_str()
	);
	// clang-format on

	/// Convert FadeDuration to milliseconds
	rain->fadeDurationMs = _wtoi( buffer );

	// Execute rain:init() once when skin is ready
	// Unlike the standard, it is triggered after the fadeDuration time
	// This avoids some bugs and improves the collection of values, like rain:getW() or rain:getRect().w
	// rain->onInit();
	rain->scheduleInit();
}



/**
 * @brief Periodic Measure update callback.
 *
 * Called by Rainmeter according to the Measure update rate.
 * Computes delta-time using a high-resolution performance counter
 * and forwards timing data to Lua.
 *
 * Responsibilities:
 * - Compute delta-time
 * - Clamp delta-time to safe range
 * - Execute rain:update(cs, dt) if available
 * - Process auxiliary subsystems (hotkeys, fetch dispatch)
 *
 * @param[in,out] data
 * Pointer to Measure instance.
 *
 * @return
 * A numeric value used by Rainmeter (here: accumulated seconds) or FPS.
 *
 * @post
 * - Lua update callback executed (if present)
 * - Internal timers updated
 */
PLUGIN_EXPORT double Update( void *data ) {
	auto *rain = static_cast<Rain *>( data );

	/** Delta-Time implementation */
	double deltaTime = 0.0;

	if ( rain->timeInitialized ) {
		LARGE_INTEGER now;
		QueryPerformanceCounter( &now );

		deltaTime = double( now.QuadPart - rain->lastTick.QuadPart ) / double( rain->freq.QuadPart );
		rain->lastTick = now;

		// Protection against absurd values (e.g., frozen skin)
		deltaTime = std::clamp( deltaTime, 0.0, 1.0 );
	}


	// Execute rain:update( cs, dt ) if it exists and after init is complete
	rain->onUpdate( deltaTime );


	// FPS
	// return 1 / deltaTime;
	return rain->cumulatedSeconds;
}



/**
 * @brief Execute Lua code received via !CommandMeasure.
 *
 * This function allows Rainmeter to execute arbitrary Lua code
 * in the context of the Measure's Lua state.
 *
 * The script is treated as inline Lua code and executed immediately.
 *
 * Special handling:
 * - Internal interval callbacks are detected and executed safely
 * - All other scripts are executed as generic Lua chunks
 *
 * @param[in] data
 * Pointer to Measure instance.
 *
 * @param[in] script
 * Lua code in UTF-16 encoding.
 *
 * @note
 * Empty or null scripts are silently ignored.
 */
PLUGIN_EXPORT void ExecuteBang( void *data, LPCWSTR script ) {
	auto *rain = static_cast<Rain *>( data );

	if ( !script || !*script )
		return; // Could add warning log

	// Convert UTF-16 → UTF-8 (robust)
	std::string script_utf8 = DetectAndConvertToUTF8( script );

	///@brief Module interval (TRIAL) - Check if it's a callback from the interval.
	if ( script_utf8.find( "rainjit._callbacks" ) != std::string::npos ) {
		// Execute directly (it's safe Lua code)
		if ( !Lua::importScript( rain, script_utf8.c_str(), "embedded:interval_callback" ) )
			Lua::trace( rain, L"Error executing interval callback\n" );
	}

	else {
		// Execute Lua script
		if ( !Lua::importScript( rain, script_utf8.c_str(), "embedded:commandMeasure.lua" ) ) {
			Lua::trace( rain, L"Error executing commandMeasure\n" );
		}
	}
}



/**
 * @brief Evaluate a Lua expression and return its result.
 *
 * Provides support for Rainmeter custom functions:
 *   [RainJIT:eval(<lua expression>)]
 *
 * The Lua code is executed and its first return value
 * is converted to a string representation.
 *
 * Supported return types:
 * - string
 * - number
 * - boolean
 * - nil
 * - basic type labels for tables/functions/etc.
 *
 * Execution guarantees:
 * - Lua stack is restored via RAII
 * - Errors are logged to Rainmeter log
 * - Result buffer is thread-local and reused safely
 *
 * @warning
 * This function runs on Rainmeter's main thread.
 * Long-running Lua code should be avoided.
 */

PLUGIN_EXPORT LPCWSTR eval( void *data, const int argc, const WCHAR *argv[] ) {
	auto *rain = static_cast<Rain *>( data );

	if ( !rain || !rain->L )
		return L"";


	if ( argc < 1 || !argv || !argv[0] ) {
		RmLog( rain->rm, LOG_ERROR, L"eval: No Lua code provided" );
		return L"";
	}

	return Lua::eval( rain, argv[0] );
}



/**
 * @brief Internal dispatcher for deferred plugin commands.
 *
 * Acts as a lightweight command router invoked via:
 *   [&MeasureName:dispatch(<command>)]
 *
 * Currently supported commands:
 * - "init" → triggers Rain::onInit()
 *
 * This mechanism is used to safely execute deferred logic
 * (such as initialization) on Rainmeter's main thread
 * without relying on window hooks or timers inside Update().
 *
 * @param[in] data
 * Pointer to Measure instance.
 *
 * @param[in] argc
 * Number of arguments.
 *
 * @param[in] argv
 * Argument array.
 *
 * @return
 * Always returns an empty string.
 */
PLUGIN_EXPORT LPCWSTR dispatch( void *data, int argc, const WCHAR *argv[] ) {
	auto *rain = static_cast<Rain *>( data );

	if ( argc == 0 )
		return L"";

	std::wstring cmd = argv[0];

	if ( cmd == L"init" )
		rain->onInit();


	return L"";
}




/**
 * @brief Finalize the Measure and release all resources.
 *
 * Called when the Measure is destroyed or the skin is unloaded.
 * Responsible for cleaning up all allocated resources.
 *
 * Cleanup responsibilities:
 * - Stop and cleanup auxiliary subsystems
 * - Close Lua state
 * - Delete Rain instance
 *
 * @param[in,out] data
 * Pointer to Measure instance.
 *
 * @post
 * - Lua state closed
 * - All internal resources released
 * - Measure memory deallocated
 */
PLUGIN_EXPORT void Finalize( void *data ) {
	auto *rain = static_cast<Rain *>( data );

	// interval::Cleanup(rain);
	// Sleep(50);

	fetch::CleanupAutoDispatch( rain );
	fetch::CleanupContexts( rain );

	hotkey::Cleanup( rain );

	if ( rain->L ) {
		lua_close( rain->L );
		rain->L = nullptr;
	}

	delete rain;
}
