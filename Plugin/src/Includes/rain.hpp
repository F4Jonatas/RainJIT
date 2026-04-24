/**
 * @file rain.hpp
 * @brief Main Measure structure declaration for RainJIT plugin.
 * @license GPL v2.0 License
 *
 * Defines the central Measure structure that represents a Rainmeter
 * Measure instance with an embedded LuaJIT runtime. Each Measure
 * maintains its own Lua state, timing, scheduling, and skin context.
 *
 * This header declares the Rain structure, which acts as the
 * bridge between Rainmeter's plugin lifecycle and the Lua runtime.
 *
 * @struct Rain
 * @brief Core Measure structure for RainJIT plugin.
 *
 * Each instance represents an independent Rainmeter Measure,
 * fully isolated from other Measures, including its Lua state
 * and internal timing.
 *
 * @invariant
 * - Lifetime managed by Rainmeter (Initialize → Finalize)
 * - Exactly one Lua state per Measure
 * - No state sharing between Measures
 * - All Lua execution occurs on Rainmeter's main thread
 * - Skin window handle is valid only after skin creation
 *
 * @lifecycle
 * 1. Created in Initialize()
 * 2. Configured in Reload()
 * 3. Updated in Update()
 * 4. Destroyed in Finalize()
 */

#pragma once

/**
 * @def NOMINMAX
 * @brief Prevent Windows headers from defining min/max macros.
 *
 * This avoids conflicts with std::min, std::max and other STL utilities.
 * Must be defined before including <Windows.h>.
 */
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <ctime>
#include <string>

#include <RainmeterAPI.hpp>
#include <lua.hpp>




/**
 * @struct Rain
 * @brief Main Measure state container.
 *
 * Stores all runtime state associated with a RainJIT Measure,
 * including Rainmeter context, Lua runtime, timing information,
 * and deferred initialization control.
 */
struct Rain {
	/// @brief Pointer to Rainmeter skin context (used for bangs execution).
	void *skin;

	/// @brief Pointer to Rainmeter API context.
	void *rm;

	/// @brief Lua state associated with this Measure.
	lua_State *L;

	/// @brief Handle to the skin window.
	/// @details Valid only after the skin has been created.
	HWND hwnd;

	/// @brief Indicates whether the Measure is ready for execution.
	/// @details Set when the skin window exists and is valid.
	bool ready;

	/// @brief Indicates whether rain:init() has already been executed.
	/// @details Ensures one-time initialization.
	bool initCalled;

	/// @brief Fade duration in milliseconds.
	/// @details Read from Rainmeter.ini (FadeDuration key).
	int fadeDurationMs = 0;

	/// @brief Guards against scheduling init multiple times.
	/// @details Used to ensure deferred initialization is only queued once.
	std::atomic<bool> initScheduled{ false };

	/// @brief Last high-resolution tick count.
	/// @details Used for delta-time calculation.
	LARGE_INTEGER lastTick{ 0 };

	/// @brief Performance counter frequency.
	LARGE_INTEGER freq{ 0 };

	/// @brief Indicates whether time tracking has been initialized.
	bool timeInitialized;

	/// @brief Number of updates since the measure was initialized.
	/// @details Incremented each time the plugin Update() function is called.
	unsigned long long accumulatedUpdates = 0;

	/// @brief Maximum safe value for accumulated updates.
	/// @details Based on 2^53 - 1 (largest integer representable exactly in double).
	static constexpr unsigned long long MAX_UPDATES_COUNT = 9007199254740991ULL;



	/**
	 * @brief Default constructor.
	 *
	 * Initializes internal state to safe defaults.
	 *
	 * @post
	 * - All pointers set to nullptr
	 * - ready and initCalled set to false
	 * - Time tracking not yet initialized
	 */
	Rain();



	/**
	 * @brief Destructor.
	 *
	 * Releases the Lua state if it still exists.
	 * Normally invoked from the plugin's Finalize() function.
	 *
	 * @post
	 * - Lua state closed
	 * - Internal pointers invalidated
	 */
	~Rain();



	/**
	 * @brief Increment the update counter and wrap at the maximum safe value.
	 *
	 * Increments the internal counter of update cycles. If the counter reaches
	 * or exceeds MAX_UPDATES_COUNT, it resets to zero to prevent overflow while
	 * preserving exact representation in double-precision floating point.
	 *
	 * @return The new update count (raw unsigned long long) after increment.
	 */
	unsigned long long incrementAndGetUpdates();



	/**
	 * @brief Perform periodic update logic.
	 *
	 * Invokes the Lua function rain:update(cs, dt) if it exists.
	 * Called from the plugin Update() function.
	 *
	 * @param deltaTime Elapsed time since last update (seconds).
	 *
	 * @pre Measure must be ready.
	 * @post Lua stack remains balanced.
	 *
	 * @note Errors are logged to the Rainmeter log.
	 */
	void onUpdate( double deltaTime );



	/**
	 * @brief Perform one-time Lua initialization.
	 *
	 * Executes rain:init() exactly once, after the skin window
	 * exists and all deferred conditions are met.
	 *
	 * @pre hwnd must be a valid window handle.
	 * @post initCalled is set to true.
	 *
	 * @note Safe to call multiple times; execution is idempotent.
	 */
	void onInit();



	/**
	 * @brief Execute a Rainmeter bang.
	 *
	 * Wraps RmExecute() and ignores empty commands.
	 *
	 * @param cmd Bang command to execute.
	 */
	void bang( const std::wstring &cmd );



	/**
	 * @brief Resolve a Rainmeter variable.
	 *
	 * Expands Rainmeter variables such as:
	 * - #CURRENTPATH#
	 * - #@#
	 * - #UserDefinedVariable#
	 *
	 * @param name Variable name (with or without # delimiters).
	 * @return Resolved value or empty string on failure.
	 */
	std::wstring var( const std::wstring &name );



	/**
	 * @brief Set or persist a Rainmeter variable.
	 *
	 * Uses !SetVariable or !WriteKeyValue depending on parameters.
	 *
	 * @param name Variable name.
	 * @param value Value to assign.
	 * @param config Optional config file for persistence.
	 */
	void setVar( const std::wstring &name, const std::wstring &value, const std::wstring &config = L"" );



	/**
	 * @brief Schedule deferred execution of rain:init().
	 *
	 * Defers initialization by fadeDurationMs, allowing
	 * Rainmeter skin animations (fade-in) to complete before
	 * Lua initialization occurs.
	 *
	 * @note Scheduling is guaranteed to occur only once.
	 */
	void scheduleInit();
};
