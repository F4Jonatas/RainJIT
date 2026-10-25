/**
 * @file mouse.hpp
 * @brief Mouse hook types for RainJIT hotkey module.
 * @license GPL v2.0 License
 */

#pragma once

#include <Windows.h>
#include <cstdint>


namespace hotkey {

	/// @brief Mouse button bitmask.
	/// Prefixed with MBUTTON_ to avoid collision with Windows MB_* defines.
	enum MouseButtonFlags : uint32_t {
		MBUTTON_NONE   = 0,
		MBUTTON_LEFT   = 1 << 0,
		MBUTTON_RIGHT  = 1 << 1,
		MBUTTON_MIDDLE = 1 << 2,
		MBUTTON_X1     = 1 << 3,
		MBUTTON_X2     = 1 << 4,
		MBUTTON_ALL    = 0x1F,
	};


	/// @brief Mouse event type bitmask.
	enum MouseEventFlags : uint32_t {
		ME_NONE        = 0,
		ME_PRESS       = 1 << 0,
		ME_RELEASE     = 1 << 1,
		ME_CLICK       = 1 << 2,
		ME_DOUBLECLICK = 1 << 3,
		ME_LONGPRESS   = 1 << 4,
		ME_MOVE        = 1 << 5,
		ME_SCROLL      = 1 << 6,
		ME_ALL         = 0x7F,
	};


	/// @brief Raw mouse event dispatched from the hook to the hidden window.
	struct MouseRawEvent {
		UINT      message    = 0;
		POINT     pos        = {};
		int       delta      = 0;
		bool      horizontal = false;
		bool      injected   = false;
		ULONGLONG timestamp  = 0;
	};


	/// @brief Processed mouse event delivered to the Lua callback.
	struct MouseEvent {
		uint32_t  eventFlag  = 0;
		uint32_t  buttonFlag = 0;
		POINT     pos        = {};
		int       delta      = 0;
		bool      horizontal = false;
		int       configId   = -1;
		ULONGLONG timestamp  = 0;
	};


	/// @brief Per-button tracking state for click / doubleclick / longpress detection.
	struct ButtonState {
		bool      isDown         = false;
		ULONGLONG pressTime      = 0;
		POINT     pressPos       = { 0, 0 };
		bool      longPressFired = false;
		ULONGLONG lastClickTime  = 0;
		POINT     lastClickPos   = { 0, 0 };
	};


	/// @brief Configuration for a single mouse listener.
	struct MouseConfig {
		uint32_t buttonFlags   = MBUTTON_ALL;
		uint32_t eventFlags    = ME_ALL;
		bool     requireFocus  = true;
		bool     allowInjected = true;
		int      holdTime      = 500;
		int      doubleTime    = 500;
		int      id            = -1;
		bool     enabled       = true;
	};

} // namespace hotkey
