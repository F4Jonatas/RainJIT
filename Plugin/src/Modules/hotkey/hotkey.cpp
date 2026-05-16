/**
 * @file hotkey.cpp
 * @brief Global state, WndProc, ProcessMessages, bindings glue and lifecycle.
 * @license GPL v2.0 License
 *
 * Owns:
 * - Global state definitions (g_contexts, hooks, live contexts)
 * - Hidden window class and WndProc
 * - ProcessMessages (dispatches to keyboard/mouse processors)
 * - luaopen_hotkey, RegisterModule, Cleanup
 */

#define NOMINMAX

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "hotkey.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>


// ---------------------------------------------------------------------------
// Forward declarations for processors and bindings (keyboard.cpp / mouse.cpp)
// ---------------------------------------------------------------------------
namespace hotkey {
	bool ProcessKeyboardEvents ( Context *ctx );
	bool ProcessMouseEvents    ( Context *ctx );
	int  hotkeyKeyboard        ( lua_State *L );
	int  hotkeyMouse           ( lua_State *L );
}

extern LRESULT CALLBACK LowLevelKeyboardProc ( int nCode, WPARAM wParam, LPARAM lParam );
extern LRESULT CALLBACK LowLevelMouseProc    ( int nCode, WPARAM wParam, LPARAM lParam );


// ---------------------------------------------------------------------------
// Hidden window WndProc — named static, no lambdas
// ---------------------------------------------------------------------------
static LRESULT CALLBACK HotkeyWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
	if ( msg == WM_CREATE ) {
		CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>( lParam );
		SetWindowLongPtrW( hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( cs->lpCreateParams ) );
		return 0;
	}

	if ( msg == WM_HOTKEY_EVENT || msg == WM_MOUSE_EVENT ) {
		hotkey::Context *ctx =
			reinterpret_cast<hotkey::Context *>( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
		if ( !ctx ) return 0;
		return hotkey::ProcessMessages( ctx->rain );
	}

	return DefWindowProcW( hwnd, msg, wParam, lParam );
}


// ---------------------------------------------------------------------------
// Global state definitions
// ---------------------------------------------------------------------------
namespace hotkey {
	namespace internal {

		std::unordered_map<Rain *, Context *> g_contexts;
		std::recursive_mutex                  g_contextsMutex;
		std::unordered_set<Context *>         g_liveContexts;

		HHOOK      g_keyboardHook      = nullptr;
		int        g_keyboardRefCount  = 0;
		std::mutex g_keyboardHookMutex;

		HHOOK      g_mouseHook         = nullptr;
		int        g_mouseRefCount     = 0;
		std::mutex g_mouseHookMutex;

		static constexpr const wchar_t *HOTKEY_WND_CLASS = L"RainJIT_HotkeyWindow";
		static ATOM       g_wndClassAtom  = 0;
		static std::mutex g_wndClassMutex;


		// Focus check
		bool SkinHasFocus( Rain *rain ) {
			if ( !rain || !rain->hwnd ) return false;
			HWND fg = GetForegroundWindow();
			return fg == rain->hwnd || IsChild( rain->hwnd, fg );
		}


		// Registry key helpers
		std::string GetUniqueCallbackKey( Rain *rain, int configId ) {
			if ( !rain || !rain->rm ) return "hotkey_kb_" + std::to_string( configId );
			const wchar_t *cfg  = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
			std::string    name = cfg ? wstring_to_utf8( cfg ) : "";
			std::replace( name.begin(), name.end(), '\\', '_' );
			std::replace( name.begin(), name.end(), '/', '_' );
			std::replace( name.begin(), name.end(), '.', '_' );
			return "hotkey_kb_" + name + "_" + std::to_string( configId );
		}

		std::string GetMouseCallbackKey( Rain *rain, int configId ) {
			if ( !rain || !rain->rm ) return "hotkey_ms_" + std::to_string( configId );
			const wchar_t *cfg  = RmReplaceVariables( rain->rm, L"#CURRENTCONFIG#" );
			std::string    name = cfg ? wstring_to_utf8( cfg ) : "";
			std::replace( name.begin(), name.end(), '\\', '_' );
			std::replace( name.begin(), name.end(), '/', '_' );
			std::replace( name.begin(), name.end(), '.', '_' );
			return "hotkey_ms_" + name + "_" + std::to_string( configId );
		}


		// Window class registration
		static bool EnsureWindowClass( HINSTANCE hInst ) {
			std::lock_guard<std::mutex> lock( g_wndClassMutex );
			if ( g_wndClassAtom != 0 ) return true;

			WNDCLASSEXW wc   = {};
			wc.cbSize        = sizeof( wc );
			wc.lpfnWndProc   = HotkeyWndProc;
			wc.hInstance     = hInst;
			wc.lpszClassName = HOTKEY_WND_CLASS;
			g_wndClassAtom   = RegisterClassExW( &wc );
			return g_wndClassAtom != 0;
		}


		// Hidden window lifecycle
		HWND CreateHiddenWindow( Context *ctx ) {
			HINSTANCE hInst = GetModuleHandle( nullptr );
			if ( !EnsureWindowClass( hInst ) ) return nullptr;
			return CreateWindowExW( 0, HOTKEY_WND_CLASS, L"RainJIT_Hotkey",
				0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, ctx );
		}

		void DestroyHiddenWindow( HWND &hwnd ) {
			if ( hwnd ) { DestroyWindow( hwnd ); hwnd = nullptr; }
		}


		// Keyboard hook lifecycle
		bool EnsureKeyboardHook( Rain *rain ) {
			std::lock_guard<std::mutex> lock( g_keyboardHookMutex );
			if ( g_keyboardRefCount > 0 && g_keyboardHook ) { g_keyboardRefCount++; return true; }
			g_keyboardHook = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle( nullptr ), 0 );
			if ( !g_keyboardHook ) {
				if ( rain && rain->rm ) RmLog( rain->rm, LOG_ERROR, L"Hotkey: Failed to install keyboard hook." );
				return false;
			}
			g_keyboardRefCount = 1;
			return true;
		}

		void ReleaseKeyboardHook( bool force ) {
			std::lock_guard<std::mutex> lock( g_keyboardHookMutex );
			if ( !g_keyboardHook ) return;
			if ( !force && --g_keyboardRefCount > 0 ) return;
			UnhookWindowsHookEx( g_keyboardHook );
			g_keyboardHook = nullptr; g_keyboardRefCount = 0;
		}


		// Mouse hook lifecycle
		bool EnsureMouseHook( Rain *rain ) {
			std::lock_guard<std::mutex> lock( g_mouseHookMutex );
			if ( g_mouseRefCount > 0 && g_mouseHook ) { g_mouseRefCount++; return true; }
			g_mouseHook = SetWindowsHookEx( WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle( nullptr ), 0 );
			if ( !g_mouseHook ) {
				if ( rain && rain->rm ) RmLog( rain->rm, LOG_ERROR, L"Hotkey: Failed to install mouse hook." );
				return false;
			}
			g_mouseRefCount = 1;
			return true;
		}

		void ReleaseMouseHook( bool force ) {
			std::lock_guard<std::mutex> lock( g_mouseHookMutex );
			if ( !g_mouseHook ) return;
			if ( !force && --g_mouseRefCount > 0 ) return;
			UnhookWindowsHookEx( g_mouseHook );
			g_mouseHook = nullptr; g_mouseRefCount = 0;
		}

	} // namespace internal
} // namespace hotkey


// ---------------------------------------------------------------------------
// Module table functions — registered via luaL_Reg
// ---------------------------------------------------------------------------

static int hotkey_keyboard( lua_State *L ) {
	return hotkey::hotkeyKeyboard( L );
}

static int hotkey_mouse( lua_State *L ) {
	return hotkey::hotkeyMouse( L );
}

// clang-format off
static const luaL_Reg hotkey_module[] = {
	{ "keyboard", hotkey_keyboard },
	{ "mouse",    hotkey_mouse    },
	{ NULL, NULL }
};
// clang-format on


// ---------------------------------------------------------------------------
// ProcessMessages / luaopen_hotkey / RegisterModule / Cleanup
// ---------------------------------------------------------------------------
namespace hotkey {

	int ProcessMessages( Rain *rain ) {
		std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );

		auto it = internal::g_contexts.find( rain );
		if ( it == internal::g_contexts.end() ) return 0;

		Context *ctx      = it->second;
		bool     anyBlock = false;

		if ( ProcessKeyboardEvents( ctx ) ) anyBlock = true;
		if ( ProcessMouseEvents   ( ctx ) ) anyBlock = true;

		return anyBlock ? 1 : 0;
	}


	extern "C" int luaopen_hotkey( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		lua_newtable( L );

		for ( const luaL_Reg *reg = hotkey_module; reg->name; ++reg ) {
			lua_pushlightuserdata( L, rain );
			lua_pushcclosure( L, reg->func, 1 );
			lua_setfield( L, -2, reg->name );
		}

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
		bool releaseKb    = false;
		bool releaseMouse = false;

		{
			std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );

			auto it = internal::g_contexts.find( rain );
			if ( it == internal::g_contexts.end() ) return;

			Context *ctx = it->second;

			if ( rain->L ) {
				for ( auto &p : ctx->configs ) {
					std::string k = internal::GetUniqueCallbackKey( rain, p.first );
					lua_pushnil( rain->L );
					lua_setfield( rain->L, LUA_REGISTRYINDEX, k.c_str() );
				}
				for ( auto &p : ctx->mouseConfigs ) {
					std::string k = internal::GetMouseCallbackKey( rain, p.first );
					lua_pushnil( rain->L );
					lua_setfield( rain->L, LUA_REGISTRYINDEX, k.c_str() );
				}
			}

			internal::g_liveContexts.erase( ctx );
			internal::DestroyHiddenWindow( ctx->hiddenWindow );
			delete ctx;
			internal::g_contexts.erase( it );

			if ( internal::g_contexts.empty() ) {
				releaseKb    = true;
				releaseMouse = true;
			}
		}

		if ( releaseKb    ) internal::ReleaseKeyboardHook( true );
		if ( releaseMouse ) internal::ReleaseMouseHook( true );
	}

} // namespace hotkey
