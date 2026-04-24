/**
 * @file hotkey.hpp
 * @brief Hotkey detection module for RainJIT.
 * @license GPL v2.0 License
 *
 * Provides Lua bindings for low-level keyboard hook with focus-aware callbacks.
 * Single global hook per Rain process with multiple key configurations.
 *
 * @module hotkey
 * @usage local hotkey = require("hotkey")
 *
 * @details
 * - Uses a single global WH_KEYBOARD_LL hook for all skins.
 * - Thread-safe event buffer between hook thread and main thread.
 * - Supports VK_* constant names from Microsoft documentation.
 * - Accepts combination strings (e.g., "VK_F12+VK_ALT") or table of combinations.
 * - Special value "all" captures every key press/release.
 * - Focus-aware filtering (optional).
 * - Automatic cleanup on skin reload/close.
 *
 * @example
 * @code{.lua}
 * local hotkey = require("hotkey")
 *
 * -- Single combination: F12 + Alt
 * local kb1 = hotkey.keyboard{
 *     vk = "VK_F12+VK_ALT",
 *     on = "press",
 *     focus = false,
 *     callback = function(event)
 *         print("Hotkey:", event.vk, event.code)
 *     end
 * }
 *
 * -- Multiple combinations for same callback
 * local kb2 = hotkey.keyboard{
 *     vk = {"VK_F12+VK_ALT", "VK_ALT+VK_F1"},
 *     on = "press",
 *     callback = function(event)
 *         print("Triggered:", event.keys )
 *     end
 * }
 * @endcode
 */

#pragma once

#include <Windows.h>
#include <lua.hpp>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>






struct Rain;

namespace hotkey {

	/// @brief Event types for hotkey triggering
	enum EventType {
		EVENT_PRESS = 1,
		EVENT_RELEASE = 2,
		EVENT_BOTH = 3
	};



	/// @brief Hotkey event structure for thread-safe buffering
	struct HotkeyEvent {
		int vkCode = 0; // Virtual key code that triggered the event
		bool isPressed = false; // True for press, false for release
		int configId = -1; // Configuration ID
		ULONGLONG timestamp = 0; // Event timestamp in milliseconds
		std::vector<int> pressedCombo; // The specific combination that was pressed
	};



	/// @brief Configuration for a single hotkey
	struct Config {
		std::vector<std::vector<int>> combinations; // List of key combinations (each combination is vector of VK codes)
		EventType eventType = EVENT_BOTH; // When to trigger callback
		bool requireFocus = true; // Only trigger when skin has focus
		int id = -1; // Unique identifier
		bool enabled = true; // Whether this config is active
		Rain *rain = nullptr; // Parent Rain instance
		bool isAllKeys = false; // True if capturing all keys ("all" mode)



		/**
		 * @brief Check if any of the combinations is currently pressed
		 *
		 * Iterates through all registered combinations and returns true if
		 * ALL keys in any combination are currently pressed.
		 *
		 * @return true if any combination matches current keyboard state
		 * @return false otherwise
		 */
		bool IsAnyCombinationPressed() const {
			if ( combinations.empty() )
				return false;

			for ( const auto &combo : combinations ) {
				if ( combo.empty() )
					continue;

				bool allPressed = true;
				for ( int key : combo ) {
					if ( ( GetAsyncKeyState( key ) & 0x8000 ) == 0 ) {
						allPressed = false;
						break;
					}
				}

				if ( allPressed )
					return true;
			}

			return false;
		}



		/**
		 * @brief Get the pressed combination (if any)
		 *
		 * Returns pointer to the first combination where all keys are pressed.
		 *
		 * @return Pointer to pressed combination vector, or nullptr if none
		 */
		const std::vector<int> *GetPressedCombination() const {
			for ( const auto &combo : combinations ) {
				if ( combo.empty() )
					continue;

				bool allPressed = true;
				for ( int key : combo ) {
					if ( ( GetAsyncKeyState( key ) & 0x8000 ) == 0 ) {
						allPressed = false;
						break;
					}
				}

				if ( allPressed )
					return &combo;
			}

			return nullptr;
		}
	};



	/// @brief Global context for hotkey management per Rain instance
	struct Context {
		std::unordered_map<int, Config> configs; // Active configurations by ID
		Rain *rain = nullptr; // Parent Rain instance
		int nextId = 1; // Next available configuration ID

		/// @brief Thread-safe event buffer
		std::queue<HotkeyEvent> eventBuffer;
		std::mutex eventMutex;
		bool hasPendingEvents = false;
	};



	/**
	 * @brief Convert VK code to string name (e.g., VK_F12 -> "VK_F12")
	 *
	 * Provides reverse mapping for virtual key codes to their constant names.
	 * Used for debugging, logging, and event reporting to Lua.
	 *
	 * @param vkCode Virtual key code (0-255)
	 * @return String representation (e.g., "VK_F12", "VK_0x41")
	 *
	 * @note Returns static buffer - not thread-safe for concurrent calls
	 */
	const char *GetVKNameFromCode( int vkCode );



	/**
	 * @brief Parse combination string like "VK_F12+VK_ALT" into vector of VK codes
	 *
	 * Splits string by '+' delimiter and converts each token to virtual key code.
	 * Supports all VK_* constants defined in Windows API.
	 *
	 * @param combinationStr String in format "VK_X+VK_Y" or single "VK_X"
	 * @return Vector of VK codes in order, empty on error
	 *
	 * @example
	 * @code
	 * auto combo = ParseCombination("VK_F12+VK_ALT+VK_SHIFT");
	 * // Returns {VK_F12, VK_MENU, VK_SHIFT}
	 * @endcode
	 */
	std::vector<int> ParseCombination( const std::string &combinationStr );



	/**
	 * @brief Register hotkey module in Lua package.preload
	 *
	 * Called during plugin initialization to make require("hotkey") available.
	 *
	 * @param L Lua state
	 * @param rain Pointer to Rain instance (passed as upvalue)
	 */
	void RegisterModule( lua_State *L, Rain *rain );



	/**
	 * @brief Lua module entry point (called by require("hotkey"))
	 *
	 * Creates and returns the hotkey module table with keyboard() function.
	 *
	 * @param L Lua state
	 * @return 1 (module table)
	 */
	extern "C" int luaopen_hotkey( lua_State *L );



	/**
	 * @brief Poll and process pending hotkey events
	 *
	 * Processes events from the thread-safe buffer and executes
	 * associated Lua callbacks. Must be called periodically (from Update).
	 *
	 * @param rain Pointer to Rain instance
	 * @return Number of events processed
	 */
	int ProcessMessages( Rain *rain );



	/**
	 * @brief Cleanup all hotkey resources for a Rain instance
	 *
	 * Removes keyboard hook (if last instance), clears configuration, and deletes context.
	 * Called automatically during plugin finalization.
	 *
	 * @param rain Pointer to Rain instance to cleanup
	 */
	void Cleanup( Rain *rain );
} // namespace hotkey
