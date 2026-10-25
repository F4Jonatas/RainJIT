/**
 * @file keyboard.hpp
 * @brief Keyboard hotkey types for RainJIT hotkey module.
 * @license GPL v2.0 License
 */

#pragma once

#include <Windows.h>
#include <lua.hpp>
#include <string>
#include <vector>


namespace hotkey {

	/// @brief Keyboard event trigger type.
	enum EventType {
		EVENT_PRESS   = 1,
		EVENT_RELEASE = 2,
		EVENT_BOTH    = 3
	};


	/// @brief Raw keyboard event for thread-safe delivery.
	struct HotkeyEvent {
		int              vkCode    = 0;
		bool             isPressed = false;
		int              configId  = -1;
		ULONGLONG        timestamp = 0;
		std::vector<int> pressedCombo;
	};


	/// @brief Configuration for a single keyboard hotkey.
	struct KeyboardConfig {
		std::vector<std::vector<int>> combinations;
		EventType eventType    = EVENT_BOTH;
		bool requireFocus      = true;
		bool allowInjected     = true;
		int  id                = -1;
		bool enabled           = true;
		bool isAllKeys         = false;

		bool IsAnyCombinationPressed() const {
			for ( const auto &combo : combinations ) {
				if ( combo.empty() ) continue;
				bool ok = true;
				for ( int k : combo )
					if ( !( GetAsyncKeyState( k ) & 0x8000 ) ) { ok = false; break; }
				if ( ok ) return true;
			}
			return false;
		}

		const std::vector<int> *GetPressedCombination() const {
			for ( const auto &combo : combinations ) {
				if ( combo.empty() ) continue;
				bool ok = true;
				for ( int k : combo )
					if ( !( GetAsyncKeyState( k ) & 0x8000 ) ) { ok = false; break; }
				if ( ok ) return &combo;
			}
			return nullptr;
		}
	};


	/// @brief Convert VK code to its string name. Thread-safe (returns by value).
	std::string GetVKNameFromCode( int vkCode );

	/// @brief Parse "VK_F12+VK_ALT" into a vector of VK codes.
	std::vector<int> ParseCombination( const std::string &str );

} // namespace hotkey
