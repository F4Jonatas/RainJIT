/**
 * @file hotkey.hpp
 * @brief Global types and public API for RainJIT hotkey module.
 * @license GPL v2.0 License
 *
 * Aggregates keyboard and mouse types into a shared Context.
 * Include this file from any translation unit that needs access to the hotkey module.
 *
 * @module hotkey
 * @usage local hotkey = require("hotkey")
 *
 * @example
 * @code{.lua}
 * local hotkey = require("hotkey")
 *
 * local kb = hotkey.keyboard{
 *     vk            = { "VK_VOLUME_UP", "VK_VOLUME_DOWN" },
 *     on            = "press",
 *     allowInjected = true,
 *     callback      = function(event) return false end
 * }
 *
 * local mb = hotkey.mouse{
 *     button   = "left",
 *     on       = { "click", "longpress" },
 *     holdTime = 600,
 *     focus    = false,
 *     callback = function(event)
 *         print(event.type, event.x, event.y)
 *     end
 * }
 * @endcode
 */

#pragma once

#include <Windows.h>
#include <lua.hpp>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "keyboard.hpp"
#include "mouse.hpp"


struct Rain;


/// @brief Keyboard event delivery message. (WM_APP+4)
#define WM_HOTKEY_EVENT ( WM_APP + 4 )

/// @brief Mouse event delivery message. (WM_APP+5)
#define WM_MOUSE_EVENT  ( WM_APP + 5 )


namespace hotkey {

	// =========================================================================
	// Context — one per Rain instance
	// =========================================================================

	struct Context {
		// Keyboard
		std::unordered_map<int, KeyboardConfig> configs;
		std::queue<HotkeyEvent>                 eventBuffer;
		std::mutex                              eventMutex;
		bool                                    hasPendingEvents = false;

		// Mouse
		std::unordered_map<int, MouseConfig>    mouseConfigs;
		std::queue<MouseRawEvent>               mouseRawBuffer;
		std::mutex                              mouseRawMutex;
		bool                                    hasPendingMouseEvents = false;

		/// @brief Per-button tracking state (0=left, 1=right, 2=middle, 3=x1, 4=x2).
		ButtonState buttonState[5] = {};

		Rain *rain         = nullptr;
		int   nextId       = 1;
		HWND  hiddenWindow = nullptr;
	};


	// =========================================================================
	// Internal — shared state between keyboard.cpp, mouse.cpp, hotkey.cpp
	// =========================================================================

	namespace internal {

		extern std::unordered_map<Rain *, Context *> g_contexts;
		extern std::recursive_mutex                  g_contextsMutex;
		extern std::unordered_set<Context *>         g_liveContexts;

		extern HHOOK      g_keyboardHook;
		extern int        g_keyboardRefCount;
		extern std::mutex g_keyboardHookMutex;

		extern HHOOK      g_mouseHook;
		extern int        g_mouseRefCount;
		extern std::mutex g_mouseHookMutex;

		static constexpr int LONGPRESS_TOLERANCE = 2;

		bool        SkinHasFocus      ( Rain *rain );
		std::string GetUniqueCallbackKey ( Rain *rain, int configId );
		std::string GetMouseCallbackKey  ( Rain *rain, int configId );

		HWND CreateHiddenWindow  ( Context *ctx );
		void DestroyHiddenWindow ( HWND &hwnd );

		bool EnsureKeyboardHook ( Rain *rain );
		void ReleaseKeyboardHook( bool force = false );

		bool EnsureMouseHook    ( Rain *rain );
		void ReleaseMouseHook   ( bool force = false );

		// Button helpers
		inline int ButtonIndex( uint32_t flag ) {
			switch ( flag ) {
				case MBUTTON_LEFT:   return 0;
				case MBUTTON_RIGHT:  return 1;
				case MBUTTON_MIDDLE: return 2;
				case MBUTTON_X1:     return 3;
				case MBUTTON_X2:     return 4;
				default:             return -1;
			}
		}

		inline const char *ButtonName( uint32_t flag ) {
			switch ( flag ) {
				case MBUTTON_LEFT:   return "left";
				case MBUTTON_RIGHT:  return "right";
				case MBUTTON_MIDDLE: return "middle";
				case MBUTTON_X1:     return "x1";
				case MBUTTON_X2:     return "x2";
				default:             return "unknown";
			}
		}

		inline const char *MouseEventName( uint32_t flag ) {
			switch ( flag ) {
				case ME_PRESS:       return "press";
				case ME_RELEASE:     return "release";
				case ME_CLICK:       return "click";
				case ME_DOUBLECLICK: return "doubleclick";
				case ME_LONGPRESS:   return "longpress";
				case ME_MOVE:        return "move";
				case ME_SCROLL:      return "scroll";
				default:             return "unknown";
			}
		}

	} // namespace internal


	// =========================================================================
	// Public API
	// =========================================================================

	/// @brief Register hotkey module in Lua package.preload.
	void RegisterModule( lua_State *L, Rain *rain );

	/// @brief Lua module entry point (called by require("hotkey")).
	extern "C" int luaopen_hotkey( lua_State *L );

	/// @brief Process pending keyboard and mouse events, run Lua callbacks.
	/// Returns 1 if any callback blocked, 0 otherwise.
	int ProcessMessages( Rain *rain );

	/// @brief Cleanup all resources for a Rain instance.
	void Cleanup( Rain *rain );

} // namespace hotkey
