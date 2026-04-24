/**
 * @file hotkey.cpp
 * @brief Hotkey detection implementation for RainJIT.
 * @license GPL v2.0 License
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <lua.hpp>

#include "hotkey.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>



// Forward declarations
static LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam );
static void ExecuteLuaCallback( hotkey::Context *ctx, const hotkey::Config &config, const std::vector<int> &pressedCombo, bool isPressed );
static int StringToVK( const std::string &str );



namespace hotkey {

	namespace internal {
		/// @brief Global context map: Rain* → Context* (protected by mutex)
		std::unordered_map<Rain *, Context *> g_contexts;
		std::recursive_mutex g_contextsMutex;

		/// @brief Global low-level keyboard hook handle and reference count
		static HHOOK g_keyboardHook = nullptr;
		static int g_hookRefCount = 0;
		static std::mutex g_hookMutex;

		// Optional: keep for debugging, but not used in logic
		static std::atomic<int> g_activeHookCalls{ 0 };


		/**
		 * @brief Check if skin window has focus
		 */
		bool SkinHasFocus( Rain *rain ) {
			if ( !rain || !rain->hwnd )
				return false;

			return GetForegroundWindow() == rain->hwnd;
		}



		// Helper to get skin name (for registry keys)
		std::wstring GetSkinName( Rain *rain ) {
			if ( !rain || !rain->rm )
				return L"";
			const wchar_t *config = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
			return config ? config : L"";
		}



		// Helper to get unique callback key in Lua registry
		std::string GetUniqueCallbackKey( Rain *rain, int configId ) {
			std::wstring skinName = internal::GetSkinName( rain );
			std::string skinNameUtf8 = wstring_to_utf8( skinName );

			// Remove invalid characters for registry key
			std::replace( skinNameUtf8.begin(), skinNameUtf8.end(), '\\', '_' );
			std::replace( skinNameUtf8.begin(), skinNameUtf8.end(), '/', '_' );
			std::replace( skinNameUtf8.begin(), skinNameUtf8.end(), '.', '_' );

			return "hotkey_callback_" + skinNameUtf8 + "_" + std::to_string( configId );
		}



		/**
		 * @brief Ensure the global keyboard hook is installed.
		 * Increments reference count and installs hook if it's the first user.
		 * @param rain Pointer to Rain instance (for logging only)
		 * @return true on success, false on error
		 */
		bool EnsureHook( Rain *rain ) {
			std::lock_guard<std::mutex> lock( g_hookMutex );
			if ( g_hookRefCount++ > 0 && g_keyboardHook != nullptr )
				return true;

			g_keyboardHook = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle( NULL ), 0 );

			if ( !g_keyboardHook ) {
				DWORD error = GetLastError();
				if ( rain && rain->rm ) {
#ifdef __RAINMETERAPI_H__
					std::wstring errorMsg = L"Failed to install global keyboard hook. Error code: " + std::to_wstring( error );
					RmLog( rain->rm, LOG_ERROR, errorMsg.c_str() );
#else
					// Fallback if Rainmeter API not available (should not happen)
#endif
				}
				g_hookRefCount--; // revert increment
				return false;
			}

			return true;
		}



		/**
		 * @brief Release the global keyboard hook.
		 * Decrements reference count and removes hook when it reaches zero.
		 */
		void ReleaseHook( bool force = false ) {
			std::lock_guard<std::mutex> lock( g_hookMutex );

			if ( !g_keyboardHook )
				return;

			if ( !force ) {
				// Runtime normal: decrement refcount
				if ( --g_hookRefCount > 0 )
					return;
			}

			// Shutdown or last user
			UnhookWindowsHookEx( g_keyboardHook );
			g_keyboardHook = nullptr;
			g_hookRefCount = 0;
		}
	} // namespace internal
} // namespace hotkey




static std::string GetCharFromVK( int vkCode ) {
	// Keyboard state (only relevant modifiers)
	BYTE keyboardState[256] = { 0 };

	if ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) {
		keyboardState[VK_SHIFT] = 0x80;
		keyboardState[VK_LSHIFT] = 0x80; // also mark specific ones
		keyboardState[VK_RSHIFT] = 0x80;
	}

	if ( GetAsyncKeyState( VK_CAPITAL ) & 0x0001 ) {
		keyboardState[VK_CAPITAL] = 0x01; // toggle state
	}

	if ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) {
		keyboardState[VK_CONTROL] = 0x80;
		keyboardState[VK_LCONTROL] = 0x80;
		keyboardState[VK_RCONTROL] = 0x80;
	}

	if ( GetAsyncKeyState( VK_MENU ) & 0x8000 ) {
		keyboardState[VK_MENU] = 0x80;
		keyboardState[VK_LMENU] = 0x80;
		keyboardState[VK_RMENU] = 0x80;
	}

	// Get scan code from virtual key code
	UINT scanCode = MapVirtualKey( vkCode, MAPVK_VK_TO_VSC );

	// Buffer to receive character(s) (up to 2 for surrogate pairs)
	wchar_t buffer[16] = { 0 };
	int ret = ToUnicode( vkCode, scanCode, keyboardState, buffer, 16, 0 );

	if ( ret > 0 ) {
		// Convert from wchar_t to UTF-8
		std::wstring wstr( buffer, ret );
		return wstring_to_utf8( wstr );
	}

	return std::string(); // key does not produce text
}



/**
 * @brief Convert string to virtual key code
 *
 * Supports VK_* constant names from Microsoft documentation.
 * Accepts formats: "VK_F12", "F12", "0x7B", or "123".
 *
 * @param str String to convert
 * @return Virtual key code, or 0 if invalid
 */
static int StringToVK( const std::string &str ) {
	if ( str.empty() )
		return 0;

	// Remove "VK_" prefix if present (case insensitive)
	std::string upper = str;
	std::transform( upper.begin(), upper.end(), upper.begin(), ::toupper );

	// If it starts with "VK_", remove the prefix
	if ( upper.rfind( "VK_", 0 ) == 0 )
		upper = upper.substr( 3 );

	// Complete mapping based on Microsoft documentation
	static const std::unordered_map<std::string, int> g_virtualKeys = { // Mouse buttons
																																			{ "LBUTTON", VK_LBUTTON },
																																			{ "RBUTTON", VK_RBUTTON },
																																			{ "MBUTTON", VK_MBUTTON },
																																			{ "XBUTTON1", VK_XBUTTON1 },
																																			{ "XBUTTON2", VK_XBUTTON2 },

																																			// Control keys
																																			{ "ALT", VK_MENU },
																																			{ "CTRL", VK_CONTROL }, // Aliases
																																			{ "CANCEL", VK_CANCEL },
																																			{ "BACK", VK_BACK },
																																			{ "TAB", VK_TAB },
																																			{ "CLEAR", VK_CLEAR },
																																			{ "RETURN", VK_RETURN },
																																			{ "SHIFT", VK_SHIFT },
																																			{ "CONTROL", VK_CONTROL },
																																			{ "MENU", VK_MENU },
																																			{ "PAUSE", VK_PAUSE },
																																			{ "CAPITAL", VK_CAPITAL },
																																			{ "ESCAPE", VK_ESCAPE },
																																			{ "SPACE", VK_SPACE },

																																			// Navigation keys
																																			{ "PRIOR", VK_PRIOR },
																																			{ "NEXT", VK_NEXT },
																																			{ "END", VK_END },
																																			{ "HOME", VK_HOME },
																																			{ "LEFT", VK_LEFT },
																																			{ "UP", VK_UP },
																																			{ "RIGHT", VK_RIGHT },
																																			{ "DOWN", VK_DOWN },
																																			{ "SELECT", VK_SELECT },
																																			{ "PRINT", VK_PRINT },
																																			{ "EXECUTE", VK_EXECUTE },
																																			{ "SNAPSHOT", VK_SNAPSHOT },
																																			{ "INSERT", VK_INSERT },
																																			{ "DELETE", VK_DELETE },
																																			{ "HELP", VK_HELP },

																																			// Letter keys (A-Z)
																																			{ "A", 'A' },
																																			{ "B", 'B' },
																																			{ "C", 'C' },
																																			{ "D", 'D' },
																																			{ "E", 'E' },
																																			{ "F", 'F' },
																																			{ "G", 'G' },
																																			{ "H", 'H' },
																																			{ "I", 'I' },
																																			{ "J", 'J' },
																																			{ "K", 'K' },
																																			{ "L", 'L' },
																																			{ "M", 'M' },
																																			{ "N", 'N' },
																																			{ "O", 'O' },
																																			{ "P", 'P' },
																																			{ "Q", 'Q' },
																																			{ "R", 'R' },
																																			{ "S", 'S' },
																																			{ "T", 'T' },
																																			{ "U", 'U' },
																																			{ "V", 'V' },
																																			{ "W", 'W' },
																																			{ "X", 'X' },
																																			{ "Y", 'Y' },
																																			{ "Z", 'Z' },

																																			// Number keys (0-9)
																																			{ "0", '0' },
																																			{ "1", '1' },
																																			{ "2", '2' },
																																			{ "3", '3' },
																																			{ "4", '4' },
																																			{ "5", '5' },
																																			{ "6", '6' },
																																			{ "7", '7' },
																																			{ "8", '8' },
																																			{ "9", '9' },

																																			// Windows keys
																																			{ "LWIN", VK_LWIN },
																																			{ "RWIN", VK_RWIN },
																																			{ "APPS", VK_APPS },
																																			{ "SLEEP", VK_SLEEP },

																																			// Numpad keys
																																			{ "NUMPAD0", VK_NUMPAD0 },
																																			{ "NUMPAD1", VK_NUMPAD1 },
																																			{ "NUMPAD2", VK_NUMPAD2 },
																																			{ "NUMPAD3", VK_NUMPAD3 },
																																			{ "NUMPAD4", VK_NUMPAD4 },
																																			{ "NUMPAD5", VK_NUMPAD5 },
																																			{ "NUMPAD6", VK_NUMPAD6 },
																																			{ "NUMPAD7", VK_NUMPAD7 },
																																			{ "NUMPAD8", VK_NUMPAD8 },
																																			{ "NUMPAD9", VK_NUMPAD9 },
																																			{ "MULTIPLY", VK_MULTIPLY },
																																			{ "ADD", VK_ADD },
																																			{ "SEPARATOR", VK_SEPARATOR },
																																			{ "SUBTRACT", VK_SUBTRACT },
																																			{ "DECIMAL", VK_DECIMAL },
																																			{ "DIVIDE", VK_DIVIDE },

																																			// Function keys (F1-F24)
																																			{ "F1", VK_F1 },
																																			{ "F2", VK_F2 },
																																			{ "F3", VK_F3 },
																																			{ "F4", VK_F4 },
																																			{ "F5", VK_F5 },
																																			{ "F6", VK_F6 },
																																			{ "F7", VK_F7 },
																																			{ "F8", VK_F8 },
																																			{ "F9", VK_F9 },
																																			{ "F10", VK_F10 },
																																			{ "F11", VK_F11 },
																																			{ "F12", VK_F12 },
																																			{ "F13", VK_F13 },
																																			{ "F14", VK_F14 },
																																			{ "F15", VK_F15 },
																																			{ "F16", VK_F16 },
																																			{ "F17", VK_F17 },
																																			{ "F18", VK_F18 },
																																			{ "F19", VK_F19 },
																																			{ "F20", VK_F20 },
																																			{ "F21", VK_F21 },
																																			{ "F22", VK_F22 },
																																			{ "F23", VK_F23 },
																																			{ "F24", VK_F24 },

																																			// Lock keys
																																			{ "NUMLOCK", VK_NUMLOCK },
																																			{ "SCROLL", VK_SCROLL },

																																			// Specific modifier keys
																																			{ "LSHIFT", VK_LSHIFT },
																																			{ "RSHIFT", VK_RSHIFT },
																																			{ "LCONTROL", VK_LCONTROL },
																																			{ "RCONTROL", VK_RCONTROL },
																																			{ "LMENU", VK_LMENU },
																																			{ "RMENU", VK_RMENU },

																																			// Media keys
																																			{ "BROWSER_BACK", VK_BROWSER_BACK },
																																			{ "BROWSER_FORWARD", VK_BROWSER_FORWARD },
																																			{ "BROWSER_REFRESH", VK_BROWSER_REFRESH },
																																			{ "BROWSER_STOP", VK_BROWSER_STOP },
																																			{ "BROWSER_SEARCH", VK_BROWSER_SEARCH },
																																			{ "BROWSER_FAVORITES", VK_BROWSER_FAVORITES },
																																			{ "BROWSER_HOME", VK_BROWSER_HOME },
																																			{ "VOLUME_MUTE", VK_VOLUME_MUTE },
																																			{ "VOLUME_DOWN", VK_VOLUME_DOWN },
																																			{ "VOLUME_UP", VK_VOLUME_UP },
																																			{ "MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK },
																																			{ "MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK },
																																			{ "MEDIA_STOP", VK_MEDIA_STOP },
																																			{ "MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE },
																																			{ "LAUNCH_MAIL", VK_LAUNCH_MAIL },
																																			{ "LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT },
																																			{ "LAUNCH_APP1", VK_LAUNCH_APP1 },
																																			{ "LAUNCH_APP2", VK_LAUNCH_APP2 },

																																			// OEM keys
																																			{ "OEM_1", VK_OEM_1 },
																																			{ "OEM_PLUS", VK_OEM_PLUS },
																																			{ "OEM_COMMA", VK_OEM_COMMA },
																																			{ "OEM_MINUS", VK_OEM_MINUS },
																																			{ "OEM_PERIOD", VK_OEM_PERIOD },
																																			{ "OEM_2", VK_OEM_2 },
																																			{ "OEM_3", VK_OEM_3 },
																																			{ "OEM_4", VK_OEM_4 },
																																			{ "OEM_5", VK_OEM_5 },
																																			{ "OEM_6", VK_OEM_6 },
																																			{ "OEM_7", VK_OEM_7 },
																																			{ "OEM_8", VK_OEM_8 },
																																			{ "OEM_102", VK_OEM_102 },

																																			// Gamepad keys (Windows 10+)
																																			{ "GAMEPAD_A", VK_GAMEPAD_A },
																																			{ "GAMEPAD_B", VK_GAMEPAD_B },
																																			{ "GAMEPAD_X", VK_GAMEPAD_X },
																																			{ "GAMEPAD_Y", VK_GAMEPAD_Y },
																																			{ "GAMEPAD_RIGHT_SHOULDER", VK_GAMEPAD_RIGHT_SHOULDER },
																																			{ "GAMEPAD_LEFT_SHOULDER", VK_GAMEPAD_LEFT_SHOULDER },
																																			{ "GAMEPAD_LEFT_TRIGGER", VK_GAMEPAD_LEFT_TRIGGER },
																																			{ "GAMEPAD_RIGHT_TRIGGER", VK_GAMEPAD_RIGHT_TRIGGER },
																																			{ "GAMEPAD_DPAD_UP", VK_GAMEPAD_DPAD_UP },
																																			{ "GAMEPAD_DPAD_DOWN", VK_GAMEPAD_DPAD_DOWN },
																																			{ "GAMEPAD_DPAD_LEFT", VK_GAMEPAD_DPAD_LEFT },
																																			{ "GAMEPAD_DPAD_RIGHT", VK_GAMEPAD_DPAD_RIGHT },
																																			{ "GAMEPAD_MENU", VK_GAMEPAD_MENU },
																																			{ "GAMEPAD_VIEW", VK_GAMEPAD_VIEW },
																																			{ "GAMEPAD_LEFT_THUMBSTICK_BUTTON", VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON },
																																			{ "GAMEPAD_RIGHT_THUMBSTICK_BUTTON", VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON } };

	auto it = g_virtualKeys.find( upper );
	if ( it != g_virtualKeys.end() )
		return it->second;

	// Try to parse as hex (e.g., "0x41" or "41")
	if ( upper.length() > 2 && upper.rfind( "0X", 0 ) == 0 ) {
		try {
			return std::stoi( upper.substr( 2 ), nullptr, 16 );
		} catch ( ... ) {
			return 0;
		}
	}

	// Try to parse as decimal
	try {
		int vk = std::stoi( upper );
		if ( vk >= VK_LBUTTON && vk <= VK_OEM_CLEAR )
			return vk;
	} catch ( ... ) {
		// Not a number
	}

	return 0;
}



/**
 * @brief Get VK_* constant name from virtual key code
 */
const char *hotkey::GetVKNameFromCode( int vkCode ) {
	static const std::map<int, std::string> g_vkCodeToName = { { VK_LBUTTON, "VK_LBUTTON" },
																														 { VK_RBUTTON, "VK_RBUTTON" },
																														 { VK_CANCEL, "VK_CANCEL" },
																														 { VK_MBUTTON, "VK_MBUTTON" },
																														 { VK_XBUTTON1, "VK_XBUTTON1" },
																														 { VK_XBUTTON2, "VK_XBUTTON2" },
																														 { VK_BACK, "VK_BACK" },
																														 { VK_TAB, "VK_TAB" },
																														 { VK_CLEAR, "VK_CLEAR" },
																														 { VK_RETURN, "VK_RETURN" },
																														 { VK_SHIFT, "VK_SHIFT" },
																														 { VK_CONTROL, "VK_CONTROL" },
																														 { VK_MENU, "VK_MENU" },
																														 { VK_PAUSE, "VK_PAUSE" },
																														 { VK_CAPITAL, "VK_CAPITAL" },
																														 { VK_ESCAPE, "VK_ESCAPE" },
																														 { VK_SPACE, "VK_SPACE" },
																														 { VK_PRIOR, "VK_PRIOR" },
																														 { VK_NEXT, "VK_NEXT" },
																														 { VK_END, "VK_END" },
																														 { VK_HOME, "VK_HOME" },
																														 { VK_LEFT, "VK_LEFT" },
																														 { VK_UP, "VK_UP" },
																														 { VK_RIGHT, "VK_RIGHT" },
																														 { VK_DOWN, "VK_DOWN" },
																														 { VK_SELECT, "VK_SELECT" },
																														 { VK_PRINT, "VK_PRINT" },
																														 { VK_EXECUTE, "VK_EXECUTE" },
																														 { VK_SNAPSHOT, "VK_SNAPSHOT" },
																														 { VK_INSERT, "VK_INSERT" },
																														 { VK_DELETE, "VK_DELETE" },
																														 { VK_HELP, "VK_HELP" },
																														 { VK_LWIN, "VK_LWIN" },
																														 { VK_RWIN, "VK_RWIN" },
																														 { VK_APPS, "VK_APPS" },
																														 { VK_SLEEP, "VK_SLEEP" },
																														 { VK_NUMPAD0, "VK_NUMPAD0" },
																														 { VK_NUMPAD1, "VK_NUMPAD1" },
																														 { VK_NUMPAD2, "VK_NUMPAD2" },
																														 { VK_NUMPAD3, "VK_NUMPAD3" },
																														 { VK_NUMPAD4, "VK_NUMPAD4" },
																														 { VK_NUMPAD5, "VK_NUMPAD5" },
																														 { VK_NUMPAD6, "VK_NUMPAD6" },
																														 { VK_NUMPAD7, "VK_NUMPAD7" },
																														 { VK_NUMPAD8, "VK_NUMPAD8" },
																														 { VK_NUMPAD9, "VK_NUMPAD9" },
																														 { VK_MULTIPLY, "VK_MULTIPLY" },
																														 { VK_ADD, "VK_ADD" },
																														 { VK_SEPARATOR, "VK_SEPARATOR" },
																														 { VK_SUBTRACT, "VK_SUBTRACT" },
																														 { VK_DECIMAL, "VK_DECIMAL" },
																														 { VK_DIVIDE, "VK_DIVIDE" },
																														 { VK_F1, "VK_F1" },
																														 { VK_F2, "VK_F2" },
																														 { VK_F3, "VK_F3" },
																														 { VK_F4, "VK_F4" },
																														 { VK_F5, "VK_F5" },
																														 { VK_F6, "VK_F6" },
																														 { VK_F7, "VK_F7" },
																														 { VK_F8, "VK_F8" },
																														 { VK_F9, "VK_F9" },
																														 { VK_F10, "VK_F10" },
																														 { VK_F11, "VK_F11" },
																														 { VK_F12, "VK_F12" },
																														 { VK_F13, "VK_F13" },
																														 { VK_F14, "VK_F14" },
																														 { VK_F15, "VK_F15" },
																														 { VK_F16, "VK_F16" },
																														 { VK_F17, "VK_F17" },
																														 { VK_F18, "VK_F18" },
																														 { VK_F19, "VK_F19" },
																														 { VK_F20, "VK_F20" },
																														 { VK_F21, "VK_F21" },
																														 { VK_F22, "VK_F22" },
																														 { VK_F23, "VK_F23" },
																														 { VK_F24, "VK_F24" },
																														 { VK_NUMLOCK, "VK_NUMLOCK" },
																														 { VK_SCROLL, "VK_SCROLL" },
																														 { VK_LSHIFT, "VK_LSHIFT" },
																														 { VK_RSHIFT, "VK_RSHIFT" },
																														 { VK_LCONTROL, "VK_LCONTROL" },
																														 { VK_RCONTROL, "VK_RCONTROL" },
																														 { VK_LMENU, "VK_LMENU" },
																														 { VK_RMENU, "VK_RMENU" },
																														 { VK_BROWSER_BACK, "VK_BROWSER_BACK" },
																														 { VK_BROWSER_FORWARD, "VK_BROWSER_FORWARD" },
																														 { VK_BROWSER_REFRESH, "VK_BROWSER_REFRESH" },
																														 { VK_BROWSER_STOP, "VK_BROWSER_STOP" },
																														 { VK_BROWSER_SEARCH, "VK_BROWSER_SEARCH" },
																														 { VK_BROWSER_FAVORITES, "VK_BROWSER_FAVORITES" },
																														 { VK_BROWSER_HOME, "VK_BROWSER_HOME" },
																														 { VK_VOLUME_MUTE, "VK_VOLUME_MUTE" },
																														 { VK_VOLUME_DOWN, "VK_VOLUME_DOWN" },
																														 { VK_VOLUME_UP, "VK_VOLUME_UP" },
																														 { VK_MEDIA_NEXT_TRACK, "VK_MEDIA_NEXT_TRACK" },
																														 { VK_MEDIA_PREV_TRACK, "VK_MEDIA_PREV_TRACK" },
																														 { VK_MEDIA_STOP, "VK_MEDIA_STOP" },
																														 { VK_MEDIA_PLAY_PAUSE, "VK_MEDIA_PLAY_PAUSE" },
																														 { VK_LAUNCH_MAIL, "VK_LAUNCH_MAIL" },
																														 { VK_LAUNCH_MEDIA_SELECT, "VK_LAUNCH_MEDIA_SELECT" },
																														 { VK_LAUNCH_APP1, "VK_LAUNCH_APP1" },
																														 { VK_LAUNCH_APP2, "VK_LAUNCH_APP2" },
																														 { VK_OEM_1, "VK_OEM_1" },
																														 { VK_OEM_PLUS, "VK_OEM_PLUS" },
																														 { VK_OEM_COMMA, "VK_OEM_COMMA" },
																														 { VK_OEM_MINUS, "VK_OEM_MINUS" },
																														 { VK_OEM_PERIOD, "VK_OEM_PERIOD" },
																														 { VK_OEM_2, "VK_OEM_2" },
																														 { VK_OEM_3, "VK_OEM_3" },
																														 { VK_OEM_4, "VK_OEM_4" },
																														 { VK_OEM_5, "VK_OEM_5" },
																														 { VK_OEM_6, "VK_OEM_6" },
																														 { VK_OEM_7, "VK_OEM_7" },
																														 { VK_OEM_8, "VK_OEM_8" },
																														 { VK_OEM_102, "VK_OEM_102" } };

	auto it = g_vkCodeToName.find( vkCode );
	if ( it != g_vkCodeToName.end() ) {
		static std::string result;
		result = it->second;
		return result.c_str();
	}

	// For letters A-Z
	if ( vkCode >= 'A' && vkCode <= 'Z' ) {
		static char letterStr[10];
		sprintf_s( letterStr, "VK_%c", vkCode );
		return letterStr;
	}

	// For numbers 0-9
	if ( vkCode >= '0' && vkCode <= '9' ) {
		static char numStr[10];
		sprintf_s( numStr, "VK_%c", vkCode );
		return numStr;
	}

	// Fallback to hex code
	static char hexStr[16];
	sprintf_s( hexStr, "VK_0x%02X", vkCode );
	return hexStr;
}



/**
 * @brief Parse combination string like "VK_F12+VK_ALT" into vector of VK codes
 */
std::vector<int> hotkey::ParseCombination( const std::string &combinationStr ) {
	std::vector<int> result;

	if ( combinationStr.empty() )
		return result;

	bool hasPlus = combinationStr.find( '+' ) != std::string::npos;

	if ( !hasPlus ) {
		// Single key
		std::string trimmed = combinationStr;
		trimmed.erase( 0, trimmed.find_first_not_of( " \t" ) );
		trimmed.erase( trimmed.find_last_not_of( " \t" ) + 1 );
		if ( trimmed.empty() )
			return result;

		int vk = StringToVK( trimmed );
		if ( vk == 0 )
			return result;

		result.push_back( vk );
		return result;
	}

	// Combination with '+'
	std::stringstream ss( combinationStr );
	std::string token;

	while ( std::getline( ss, token, '+' ) ) {
		token.erase( 0, token.find_first_not_of( " \t" ) );
		token.erase( token.find_last_not_of( " \t" ) + 1 );
		if ( token.empty() )
			continue;

		int vk = StringToVK( token );
		if ( vk == 0 )
			return std::vector<int>(); // fail

		result.push_back( vk );
	}

	return result;
}




/**
 * @brief Execute Lua callback for hotkey event
 */
static void ExecuteLuaCallback( hotkey::Context *ctx, const hotkey::Config &config, const std::vector<int> &pressedCombo, bool isPressed ) {
	if ( !ctx || !ctx->rain || !ctx->rain->L )
		return;

	lua_State *L = ctx->rain->L;
	int stackTop = lua_gettop( L );

	// Get callback from registry
	lua_getfield( L, LUA_REGISTRYINDEX, hotkey::internal::GetUniqueCallbackKey( ctx->rain, config.id ).c_str() );

	if ( !lua_isfunction( L, -1 ) ) {
		lua_pop( L, 1 );
		return;
	}

	// Create event table
	lua_newtable( L );

	// char press
	std::string charStr;
	if ( !pressedCombo.empty() )
		charStr = GetCharFromVK( pressedCombo.back() );

	lua_pushlstring( L, charStr.data(), charStr.size() );
	lua_setfield( L, -2, "char" );

	// code: VK of triggering key
	int vkCode = pressedCombo.empty() ? 0 : pressedCombo.back();
	lua_pushinteger( L, vkCode );
	lua_setfield( L, -2, "code" );

	// type
	lua_pushstring( L, isPressed ? "press" : "release" );
	lua_setfield( L, -2, "type" );

	// keys table
	lua_newtable( L );
	int index = 1;
	for ( int key : pressedCombo ) {
		const char *name = hotkey::GetVKNameFromCode( key );
		lua_pushstring( L, name );
		lua_rawseti( L, -2, index++ );
	}
	lua_setfield( L, -2, "keys" );

	// Lock states
	bool capsLock = ( GetKeyState( VK_CAPITAL ) & 0x0001 ) != 0;
	lua_pushboolean( L, capsLock );
	lua_setfield( L, -2, "capslock" );

	bool numLock = ( GetKeyState( VK_NUMLOCK ) & 0x0001 ) != 0;
	lua_pushboolean( L, numLock );
	lua_setfield( L, -2, "numlock" );

	bool scrollLock = ( GetKeyState( VK_SCROLL ) & 0x0001 ) != 0;
	lua_pushboolean( L, scrollLock );
	lua_setfield( L, -2, "scrolllock" );

	// Modifier states (any)
	bool ctrlPressed = ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) != 0;
	lua_pushboolean( L, ctrlPressed );
	lua_setfield( L, -2, "ctrl" );

	bool altPressed = ( GetAsyncKeyState( VK_LMENU ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_RMENU ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_MENU ) & 0x8000 ) != 0;
	lua_pushboolean( L, altPressed );
	lua_setfield( L, -2, "alt" );

	bool shiftPressed = ( GetAsyncKeyState( VK_LSHIFT ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_RSHIFT ) & 0x8000 ) != 0 || ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) != 0;
	lua_pushboolean( L, shiftPressed );
	lua_setfield( L, -2, "shift" );

	// timestamp
	LARGE_INTEGER perfCounter, perfFreq;
	QueryPerformanceCounter( &perfCounter );
	QueryPerformanceFrequency( &perfFreq );
	double timestamp = (double)perfCounter.QuadPart / (double)perfFreq.QuadPart;
	lua_pushnumber( L, timestamp );
	lua_setfield( L, -2, "timestamp" );

	// vk (triggering key)
	const char *vkName = pressedCombo.empty() ? "" : hotkey::GetVKNameFromCode( pressedCombo.back() );
	lua_pushstring( L, vkName );
	lua_setfield( L, -2, "vk" );

	// focus
	bool hasFocus = hotkey::internal::SkinHasFocus( ctx->rain );
	lua_pushboolean( L, hasFocus );
	lua_setfield( L, -2, "focus" );

	// Call the callback
	if ( lua_pcall( L, 1, 0, 0 ) != LUA_OK ) {
		const char *err = lua_tostring( L, -1 );
		if ( err && *err )
			luaL_error( ctx->rain->L, "Hotkey callback error: %s", err );
		lua_pop( L, 1 );
	}

	lua_settop( L, stackTop );
}



/**
 * @brief Low-level keyboard hook procedure (global)
 */
static LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam ) {
	// Increment active hook counter (optional, for debugging)
	hotkey::internal::g_activeHookCalls.fetch_add( 1, std::memory_order_acquire );

	// RAII guard to ensure decrement even on early return
	struct HookGuard {
		~HookGuard() {
			hotkey::internal::g_activeHookCalls.fetch_sub( 1, std::memory_order_release );
		}
	} guard;

	if ( nCode >= 0 ) {
		KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>( lParam );

		// Skip injected events
		if ( kb->flags & LLKHF_INJECTED )
			return CallNextHookEx( NULL, nCode, wParam, lParam );

		bool isPressed = ( wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN );
		int vkCode = kb->vkCode;

		// Protect context map access
		std::lock_guard<std::recursive_mutex> lock( hotkey::internal::g_contextsMutex );

		for ( auto &pair : hotkey::internal::g_contexts ) {
			hotkey::Context *ctx = pair.second;
			if ( !ctx || !ctx->rain )
				continue;

			for ( auto &configPair : ctx->configs ) {
				hotkey::Config &config = configPair.second;
				if ( !config.enabled )
					continue;

				// Special "all keys" mode
				if ( config.isAllKeys ) {
					bool shouldTrigger = false;

					if ( isPressed && ( config.eventType == hotkey::EVENT_PRESS || config.eventType == hotkey::EVENT_BOTH ) )
						shouldTrigger = true;

					else if ( !isPressed && ( config.eventType == hotkey::EVENT_RELEASE || config.eventType == hotkey::EVENT_BOTH ) )
						shouldTrigger = true;

					if ( !shouldTrigger )
						continue;

					if ( config.requireFocus && !hotkey::internal::SkinHasFocus( ctx->rain ) )
						continue;

					std::vector<int> singleKeyCombo = { vkCode };

					hotkey::HotkeyEvent event;
					event.vkCode = vkCode;
					event.isPressed = isPressed;
					event.configId = config.id;
					event.timestamp = GetTickCount64();
					event.pressedCombo = singleKeyCombo;

					{
						std::lock_guard<std::mutex> lock( ctx->eventMutex );
						ctx->eventBuffer.push( event );
						ctx->hasPendingEvents = true;
					}

					hotkey::ProcessMessages( ctx->rain );
					continue;
				}

				// Normal combination logic
				bool combinationMatches = false;
				const std::vector<int> *matchedCombo = nullptr;

				if ( isPressed ) {
					for ( const auto &combo : config.combinations ) {
						if ( std::find( combo.begin(), combo.end(), vkCode ) == combo.end() )
							continue;

						bool allOthersPressed = true;

						for ( int code : combo ) {
							if ( code == vkCode )
								continue;

							if ( ( GetAsyncKeyState( code ) & 0x8000 ) == 0 ) {
								allOthersPressed = false;
								break;
							}
						}

						if ( allOthersPressed ) {
							combinationMatches = true;
							matchedCombo = &combo;
							break;
						}
					}
				}

				else {
					for ( const auto &combo : config.combinations ) {
						if ( std::find( combo.begin(), combo.end(), vkCode ) == combo.end() )
							continue;

						bool wereAllPressed = true;

						for ( int code : combo ) {
							if ( code == vkCode )
								continue;

							if ( ( GetAsyncKeyState( code ) & 0x8000 ) == 0 ) {
								wereAllPressed = false;
								break;
							}
						}

						if ( wereAllPressed ) {
							combinationMatches = true;
							matchedCombo = &combo;
							break;
						}
					}
				}

				if ( !combinationMatches || !matchedCombo )
					continue;

				if ( config.requireFocus && !hotkey::internal::SkinHasFocus( ctx->rain ) )
					continue;

				bool shouldTrigger = false;

				if ( isPressed && ( config.eventType == hotkey::EVENT_PRESS || config.eventType == hotkey::EVENT_BOTH ) )
					shouldTrigger = true;

				else if ( !isPressed && ( config.eventType == hotkey::EVENT_RELEASE || config.eventType == hotkey::EVENT_BOTH ) )
					shouldTrigger = true;

				if ( !shouldTrigger )
					continue;

				hotkey::HotkeyEvent event;
				event.vkCode = vkCode;
				event.isPressed = isPressed;
				event.configId = config.id;
				event.timestamp = GetTickCount64();
				event.pressedCombo = *matchedCombo;

				{
					std::lock_guard<std::mutex> lock( ctx->eventMutex );
					ctx->eventBuffer.push( event );
					ctx->hasPendingEvents = true;
				}

				hotkey::ProcessMessages( ctx->rain );
			}
		}
	}

	return CallNextHookEx( NULL, nCode, wParam, lParam );
}



namespace hotkey {

	int ProcessMessages( Rain *rain ) {
		std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );
		auto it = internal::g_contexts.find( rain );

		if ( it == internal::g_contexts.end() )
			return 0;

		Context *ctx = it->second;
		if ( !ctx->hasPendingEvents )
			return 0;

		std::queue<HotkeyEvent> eventsToProcess;
		int eventCount = 0;

		{
			std::lock_guard<std::mutex> lock( ctx->eventMutex );
			eventsToProcess.swap( ctx->eventBuffer );
			ctx->hasPendingEvents = false;
			eventCount = (int)eventsToProcess.size();
		}

		while ( !eventsToProcess.empty() ) {
			const HotkeyEvent &event = eventsToProcess.front();
			auto configIt = ctx->configs.find( event.configId );

			if ( configIt != ctx->configs.end() && configIt->second.enabled )
				ExecuteLuaCallback( ctx, configIt->second, event.pressedCombo, event.isPressed );

			eventsToProcess.pop();
		}

		return eventCount;
	}




	/**
	 * @brief Lua: hotkey.keyboard(config) -> keyboard object
	 */
	static int hotkeyKeyboard( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		if ( !rain ) {
			lua_pushnil( L );
			lua_pushstring( L, "Rain instance not found" );
			return 2;
		}

		// Get or create context for this Rain instance
		std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );
		Context *ctx = nullptr;
		auto it = internal::g_contexts.find( rain );
		if ( it == internal::g_contexts.end() ) {
			ctx = new Context();
			ctx->rain = rain;
			internal::g_contexts[rain] = ctx;
		} else {
			ctx = it->second;
		}

		// Parse configuration table
		luaL_checktype( L, 1, LUA_TTABLE );

		Config config;
		config.id = ctx->nextId++;
		config.enabled = true;
		config.eventType = hotkey::EVENT_BOTH;
		config.requireFocus = true;
		config.rain = rain;
		config.isAllKeys = false;

		// Parse vk field
		lua_getfield( L, 1, "vk" );

		// Check for "all" special value
		if ( lua_isstring( L, -1 ) ) {
			const char *vkStr = lua_tostring( L, -1 );

			if ( vkStr && strcmp( vkStr, "all" ) == 0 ) {
				// All keys mode
				lua_pop( L, 1 ); // pop vk

				// Use a dummy combination (will be ignored, but we need something)
				std::vector<int> allKeysCombo;
				allKeysCombo.push_back( 0 ); // 0 = special placeholder
				config.combinations.push_back( allKeysCombo );
				config.isAllKeys = true;
			}

			else if ( lua_isstring( L, -1 ) ) {
				// Single combination string
				const char *comboStr = lua_tostring( L, -1 );
				std::vector<int> combo = ParseCombination( comboStr );

				if ( combo.empty() ) {
					lua_pop( L, 1 );
					lua_pushnil( L );
					lua_pushstring( L, "Invalid combination string" );
					return 2;
				}

				config.combinations.push_back( combo );
				lua_pop( L, 1 );
			}
		}

		else if ( lua_istable( L, -1 ) ) {
			// Table of combination strings
			size_t rawLen = lua_objlen( L, -1 );
			if ( rawLen == 0 ) {
				lua_pop( L, 1 );
				lua_pushstring( L, "Empty combinations table" );
				return 2;
			}

			bool hasValidCombination = false;

			// Eliminate C4018 / C4267 / C4244
			int len = static_cast<int>( rawLen );


			for ( int i = 1; i <= len; i++ ) {
				lua_rawgeti( L, -1, i );
				if ( lua_isstring( L, -1 ) ) {
					const char *comboStr = lua_tostring( L, -1 );
					std::vector<int> combo = ParseCombination( comboStr );
					if ( !combo.empty() ) {
						config.combinations.push_back( combo );
						hasValidCombination = true;
					} else {
						if ( ctx->rain && ctx->rain->rm ) {
#ifdef __RAINMETERAPI_H__
							std::wstring msg = L"Hotkey: Skipping invalid combination: '";
							msg += utf8_to_wstring( comboStr );
							msg += L"'";
							RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
#endif
						}
					}
				}

				else {
					if ( ctx->rain && ctx->rain->rm ) {
#ifdef __RAINMETERAPI_H__
						RmLog( ctx->rain->rm, LOG_WARNING, L"Skipping non-string entry in combinations table" );
#endif
					}
				}
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 ); // pop vk table

			if ( !hasValidCombination ) {
				lua_pushnil( L );
				lua_pushstring( L, "No valid combinations in table" );
				return 2;
			}
		}

		else {
			lua_pop( L, 1 );
			lua_pushnil( L );
			lua_pushstring( L, "vk field must be string, table, or \"all\"" );
			return 2;
		}

		// Validate we have at least one combination (except for "all" which already set isAllKeys)
		if ( !config.isAllKeys && config.combinations.empty() ) {
			lua_pushnil( L );
			lua_pushstring( L, "No valid combinations specified" );
			return 2;
		}

		// Parse 'on' field (optional)
		lua_getfield( L, 1, "on" );
		if ( lua_isstring( L, -1 ) ) {
			const char *onStr = lua_tostring( L, -1 );
			if ( onStr ) {
				if ( strcmp( onStr, "press" ) == 0 )
					config.eventType = hotkey::EVENT_PRESS;
				else if ( strcmp( onStr, "release" ) == 0 )
					config.eventType = hotkey::EVENT_RELEASE;
				else if ( strcmp( onStr, "both" ) == 0 )
					config.eventType = hotkey::EVENT_BOTH;
				else {
					if ( ctx->rain && ctx->rain->rm ) {
#ifdef __RAINMETERAPI_H__
						std::wstring msg = L"Hotkey: Invalid 'on' value: '";
						msg += utf8_to_wstring( onStr );
						msg += L"'. Using default: 'both'";
						RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
#endif
					}
				}
			}
		}
		lua_pop( L, 1 );

		// Parse 'focus' field (optional)
		lua_getfield( L, 1, "focus" );
		if ( !lua_isnil( L, -1 ) )
			config.requireFocus = lua_toboolean( L, -1 );
		lua_pop( L, 1 );

		// Get callback function (required)
		lua_getfield( L, 1, "callback" );
		if ( !lua_isfunction( L, -1 ) ) {
			lua_pushnil( L );
			lua_pushstring( L, "callback function is required" );
			return 2;
		}

		// Store callback in registry
		std::string registryKey = internal::GetUniqueCallbackKey( ctx->rain, config.id );
		lua_pushvalue( L, -1 ); // copy function
		lua_setfield( L, LUA_REGISTRYINDEX, registryKey.c_str() );
		lua_pop( L, 1 ); // pop callback

		// Store configuration
		ctx->configs[config.id] = config;

		// Install global hook if necessary
		if ( !internal::EnsureHook( ctx->rain ) ) {
			ctx->configs.erase( config.id );
			lua_pushnil( L );
			lua_pushstring( L, "Failed to install global keyboard hook" );
			return 2;
		}



		// Create Lua keyboard object
		lua_newtable( L );

		// Method: disable()
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					Context *ctx = (Context *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					int id = (int)lua_tointeger( L, lua_upvalueindex( 2 ) );
					if ( ctx ) {
						auto it = ctx->configs.find( id );
						if ( it != ctx->configs.end() ) {
							it->second.enabled = false;
							lua_pushboolean( L, true );
							return 1;
						}
					}
					lua_pushboolean( L, false );
					return 1;
				},
				2 );
		lua_setfield( L, -2, "disable" );


		// Method: enable()
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					Context *ctx = (Context *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					int id = (int)lua_tointeger( L, lua_upvalueindex( 2 ) );
					if ( ctx ) {
						auto it = ctx->configs.find( id );
						if ( it != ctx->configs.end() ) {
							it->second.enabled = true;
							lua_pushboolean( L, true );
							return 1;
						}
					}
					lua_pushboolean( L, false );
					return 1;
				},
				2 );
		lua_setfield( L, -2, "enable" );


		// Method: remove()
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					Context *ctx = (Context *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					int id = (int)lua_tointeger( L, lua_upvalueindex( 2 ) );

					if ( ctx ) {
						auto it = ctx->configs.find( id );
						if ( it != ctx->configs.end() ) {
							ctx->configs.erase( it );

							// Remove callback from registry
							std::string registryKey = internal::GetUniqueCallbackKey( ctx->rain, id );
							lua_pushnil( L );
							lua_setfield( L, LUA_REGISTRYINDEX, registryKey.c_str() );

							// If no more configs in this context, release the global hook
							if ( ctx->configs.empty() ) {
								internal::ReleaseHook();
							}

							lua_pushboolean( L, true );
							return 1;
						}
					}
					lua_pushboolean( L, false );
					return 1;
				},
				2 );
		lua_setfield( L, -2, "remove" );


		// Method: isEnabled()
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					Context *ctx = (Context *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					int id = (int)lua_tointeger( L, lua_upvalueindex( 2 ) );
					bool enabled = false;
					if ( ctx ) {
						auto it = ctx->configs.find( id );
						if ( it != ctx->configs.end() )
							enabled = it->second.enabled;
					}
					lua_pushboolean( L, enabled );
					return 1;
				},
				2 );
		lua_setfield( L, -2, "isEnabled" );


		// Metatable with __gc for automatic cleanup
		lua_newtable( L );
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure(
				L,
				[]( lua_State *L ) -> int {
					Context *ctx = (Context *)lua_touserdata( L, lua_upvalueindex( 1 ) );
					int id = (int)lua_tointeger( L, lua_upvalueindex( 2 ) );

					if ( ctx ) {
						auto it = ctx->configs.find( id );
						if ( it != ctx->configs.end() ) {
							ctx->configs.erase( it );

							// Remove callback from registry
							std::string registryKey = internal::GetUniqueCallbackKey( ctx->rain, id );
							lua_pushnil( L );
							lua_setfield( L, LUA_REGISTRYINDEX, registryKey.c_str() );

							if ( ctx->configs.empty() )
								internal::ReleaseHook();
						}
					}

					return 0;
				},
				2 );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );

		return 1;
	}




	extern "C" int luaopen_hotkey( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		lua_newtable( L );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, hotkeyKeyboard, 1 );
		lua_setfield( L, -2, "keyboard" );
		return 1;
	}



	void RegisterModule( lua_State *L, Rain *rain ) {
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_hotkey, 1 );
		lua_setfield( L, -2, "hotkey" );
		lua_pop( L, 2 );
	}



	void Cleanup( Rain *rain ) {
		bool shouldReleaseHook = false;

		{
			std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );

			auto it = internal::g_contexts.find( rain );
			if ( it != internal::g_contexts.end() ) {
				Context *ctx = it->second;

				if ( rain && rain->L ) {
					lua_State *L = rain->L;
					for ( auto &pair : ctx->configs ) {
						std::string registryKey = internal::GetUniqueCallbackKey( rain, pair.first );
						lua_pushnil( L );
						lua_setfield( L, LUA_REGISTRYINDEX, registryKey.c_str() );
					}
				}

				delete ctx;
				internal::g_contexts.erase( it );

				if ( internal::g_contexts.empty() )
					shouldReleaseHook = true;
			}
		}

		if ( shouldReleaseHook )
			internal::ReleaseHook( true );
	}

} // namespace hotkey
