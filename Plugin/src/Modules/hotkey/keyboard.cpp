/**
 * @file keyboard.cpp
 * @brief Keyboard hook implementation for RainJIT hotkey module.
 * @license GPL v2.0 License
 */

#define NOMINMAX

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include "hotkey.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>


// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static int         StringToVK    ( const std::string &str );
static std::string GetCharFromVK ( int vkCode );
static bool        ExecuteKeyboardCallback( hotkey::Context *ctx, const hotkey::KeyboardConfig &config, const std::vector<int> &combo, bool isPressed );


// ---------------------------------------------------------------------------
// GetCharFromVK — called only from main thread
// ---------------------------------------------------------------------------
static std::string GetCharFromVK( int vkCode ) {
	BYTE keyboardState[256] = {};
	if ( GetAsyncKeyState( VK_SHIFT )   & 0x8000 ) { keyboardState[VK_SHIFT]    = 0x80; keyboardState[VK_LSHIFT]   = 0x80; keyboardState[VK_RSHIFT]   = 0x80; }
	if ( GetAsyncKeyState( VK_CAPITAL ) & 0x0001 )   keyboardState[VK_CAPITAL]  = 0x01;
	if ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) { keyboardState[VK_CONTROL]  = 0x80; keyboardState[VK_LCONTROL] = 0x80; keyboardState[VK_RCONTROL] = 0x80; }
	if ( GetAsyncKeyState( VK_MENU )    & 0x8000 ) { keyboardState[VK_MENU]     = 0x80; keyboardState[VK_LMENU]    = 0x80; keyboardState[VK_RMENU]    = 0x80; }
	UINT    scanCode = MapVirtualKey( vkCode, MAPVK_VK_TO_VSC );
	wchar_t buffer[16] = {};
	int     ret = ToUnicode( vkCode, scanCode, keyboardState, buffer, 16, 0 );
	if ( ret > 0 ) return wstring_to_utf8( std::wstring( buffer, ret ) );
	return {};
}


// ---------------------------------------------------------------------------
// StringToVK
// ---------------------------------------------------------------------------
static int StringToVK( const std::string &str ) {
	if ( str.empty() ) return 0;
	std::string upper = str;
	std::transform( upper.begin(), upper.end(), upper.begin(), ::toupper );
	if ( upper.rfind( "VK_", 0 ) == 0 ) upper = upper.substr( 3 );

	static const std::unordered_map<std::string, int> s_map = {
		{ "LBUTTON",VK_LBUTTON },{ "RBUTTON",VK_RBUTTON },{ "MBUTTON",VK_MBUTTON },
		{ "XBUTTON1",VK_XBUTTON1 },{ "XBUTTON2",VK_XBUTTON2 },
		{ "ALT",VK_MENU },{ "CTRL",VK_CONTROL },
		{ "CANCEL",VK_CANCEL },{ "BACK",VK_BACK },{ "TAB",VK_TAB },
		{ "CLEAR",VK_CLEAR },{ "RETURN",VK_RETURN },{ "SHIFT",VK_SHIFT },
		{ "CONTROL",VK_CONTROL },{ "MENU",VK_MENU },{ "PAUSE",VK_PAUSE },
		{ "CAPITAL",VK_CAPITAL },{ "ESCAPE",VK_ESCAPE },{ "SPACE",VK_SPACE },
		{ "PRIOR",VK_PRIOR },{ "NEXT",VK_NEXT },{ "END",VK_END },{ "HOME",VK_HOME },
		{ "LEFT",VK_LEFT },{ "UP",VK_UP },{ "RIGHT",VK_RIGHT },{ "DOWN",VK_DOWN },
		{ "SELECT",VK_SELECT },{ "PRINT",VK_PRINT },{ "EXECUTE",VK_EXECUTE },
		{ "SNAPSHOT",VK_SNAPSHOT },{ "INSERT",VK_INSERT },{ "DELETE",VK_DELETE },{ "HELP",VK_HELP },
		{ "A",'A' },{ "B",'B' },{ "C",'C' },{ "D",'D' },{ "E",'E' },{ "F",'F' },{ "G",'G' },
		{ "H",'H' },{ "I",'I' },{ "J",'J' },{ "K",'K' },{ "L",'L' },{ "M",'M' },{ "N",'N' },
		{ "O",'O' },{ "P",'P' },{ "Q",'Q' },{ "R",'R' },{ "S",'S' },{ "T",'T' },{ "U",'U' },
		{ "V",'V' },{ "W",'W' },{ "X",'X' },{ "Y",'Y' },{ "Z",'Z' },
		{ "0",'0' },{ "1",'1' },{ "2",'2' },{ "3",'3' },{ "4",'4' },
		{ "5",'5' },{ "6",'6' },{ "7",'7' },{ "8",'8' },{ "9",'9' },
		{ "LWIN",VK_LWIN },{ "RWIN",VK_RWIN },{ "APPS",VK_APPS },{ "SLEEP",VK_SLEEP },
		{ "NUMPAD0",VK_NUMPAD0 },{ "NUMPAD1",VK_NUMPAD1 },{ "NUMPAD2",VK_NUMPAD2 },
		{ "NUMPAD3",VK_NUMPAD3 },{ "NUMPAD4",VK_NUMPAD4 },{ "NUMPAD5",VK_NUMPAD5 },
		{ "NUMPAD6",VK_NUMPAD6 },{ "NUMPAD7",VK_NUMPAD7 },{ "NUMPAD8",VK_NUMPAD8 },
		{ "NUMPAD9",VK_NUMPAD9 },{ "MULTIPLY",VK_MULTIPLY },{ "ADD",VK_ADD },
		{ "SEPARATOR",VK_SEPARATOR },{ "SUBTRACT",VK_SUBTRACT },
		{ "DECIMAL",VK_DECIMAL },{ "DIVIDE",VK_DIVIDE },
		{ "F1",VK_F1 },{ "F2",VK_F2 },{ "F3",VK_F3 },{ "F4",VK_F4 },{ "F5",VK_F5 },
		{ "F6",VK_F6 },{ "F7",VK_F7 },{ "F8",VK_F8 },{ "F9",VK_F9 },{ "F10",VK_F10 },
		{ "F11",VK_F11 },{ "F12",VK_F12 },{ "F13",VK_F13 },{ "F14",VK_F14 },{ "F15",VK_F15 },
		{ "F16",VK_F16 },{ "F17",VK_F17 },{ "F18",VK_F18 },{ "F19",VK_F19 },{ "F20",VK_F20 },
		{ "F21",VK_F21 },{ "F22",VK_F22 },{ "F23",VK_F23 },{ "F24",VK_F24 },
		{ "NUMLOCK",VK_NUMLOCK },{ "SCROLL",VK_SCROLL },
		{ "LSHIFT",VK_LSHIFT },{ "RSHIFT",VK_RSHIFT },
		{ "LCONTROL",VK_LCONTROL },{ "RCONTROL",VK_RCONTROL },
		{ "LMENU",VK_LMENU },{ "RMENU",VK_RMENU },
		{ "BROWSER_BACK",VK_BROWSER_BACK },{ "BROWSER_FORWARD",VK_BROWSER_FORWARD },
		{ "BROWSER_REFRESH",VK_BROWSER_REFRESH },{ "BROWSER_STOP",VK_BROWSER_STOP },
		{ "BROWSER_SEARCH",VK_BROWSER_SEARCH },{ "BROWSER_FAVORITES",VK_BROWSER_FAVORITES },
		{ "BROWSER_HOME",VK_BROWSER_HOME },
		{ "VOLUME_MUTE",VK_VOLUME_MUTE },{ "VOLUME_DOWN",VK_VOLUME_DOWN },{ "VOLUME_UP",VK_VOLUME_UP },
		{ "MEDIA_NEXT_TRACK",VK_MEDIA_NEXT_TRACK },{ "MEDIA_PREV_TRACK",VK_MEDIA_PREV_TRACK },
		{ "MEDIA_STOP",VK_MEDIA_STOP },{ "MEDIA_PLAY_PAUSE",VK_MEDIA_PLAY_PAUSE },
		{ "LAUNCH_MAIL",VK_LAUNCH_MAIL },{ "LAUNCH_MEDIA_SELECT",VK_LAUNCH_MEDIA_SELECT },
		{ "LAUNCH_APP1",VK_LAUNCH_APP1 },{ "LAUNCH_APP2",VK_LAUNCH_APP2 },
		{ "OEM_1",VK_OEM_1 },{ "OEM_PLUS",VK_OEM_PLUS },{ "OEM_COMMA",VK_OEM_COMMA },
		{ "OEM_MINUS",VK_OEM_MINUS },{ "OEM_PERIOD",VK_OEM_PERIOD },
		{ "OEM_2",VK_OEM_2 },{ "OEM_3",VK_OEM_3 },{ "OEM_4",VK_OEM_4 },
		{ "OEM_5",VK_OEM_5 },{ "OEM_6",VK_OEM_6 },{ "OEM_7",VK_OEM_7 },
		{ "OEM_8",VK_OEM_8 },{ "OEM_102",VK_OEM_102 },
	};

	auto it = s_map.find( upper );
	if ( it != s_map.end() ) return it->second;
	if ( upper.length() > 2 && upper.rfind( "0X", 0 ) == 0 ) {
		try { return std::stoi( upper.substr(2), nullptr, 16 ); } catch ( ... ) {}
	}
	try {
		int vk = std::stoi( upper );
		if ( vk >= VK_LBUTTON && vk <= VK_OEM_CLEAR ) return vk;
	} catch ( ... ) {}
	return 0;
}


// ---------------------------------------------------------------------------
// GetVKNameFromCode
// ---------------------------------------------------------------------------
std::string hotkey::GetVKNameFromCode( int vkCode ) {
	static const std::map<int, std::string> s_names = {
		{ VK_LBUTTON,"VK_LBUTTON" },{ VK_RBUTTON,"VK_RBUTTON" },{ VK_CANCEL,"VK_CANCEL" },
		{ VK_MBUTTON,"VK_MBUTTON" },{ VK_XBUTTON1,"VK_XBUTTON1" },{ VK_XBUTTON2,"VK_XBUTTON2" },
		{ VK_BACK,"VK_BACK" },{ VK_TAB,"VK_TAB" },{ VK_CLEAR,"VK_CLEAR" },
		{ VK_RETURN,"VK_RETURN" },{ VK_SHIFT,"VK_SHIFT" },{ VK_CONTROL,"VK_CONTROL" },
		{ VK_MENU,"VK_MENU" },{ VK_PAUSE,"VK_PAUSE" },{ VK_CAPITAL,"VK_CAPITAL" },
		{ VK_ESCAPE,"VK_ESCAPE" },{ VK_SPACE,"VK_SPACE" },
		{ VK_PRIOR,"VK_PRIOR" },{ VK_NEXT,"VK_NEXT" },{ VK_END,"VK_END" },{ VK_HOME,"VK_HOME" },
		{ VK_LEFT,"VK_LEFT" },{ VK_UP,"VK_UP" },{ VK_RIGHT,"VK_RIGHT" },{ VK_DOWN,"VK_DOWN" },
		{ VK_SELECT,"VK_SELECT" },{ VK_PRINT,"VK_PRINT" },{ VK_EXECUTE,"VK_EXECUTE" },
		{ VK_SNAPSHOT,"VK_SNAPSHOT" },{ VK_INSERT,"VK_INSERT" },{ VK_DELETE,"VK_DELETE" },
		{ VK_HELP,"VK_HELP" },
		{ VK_LWIN,"VK_LWIN" },{ VK_RWIN,"VK_RWIN" },{ VK_APPS,"VK_APPS" },{ VK_SLEEP,"VK_SLEEP" },
		{ VK_NUMPAD0,"VK_NUMPAD0" },{ VK_NUMPAD1,"VK_NUMPAD1" },{ VK_NUMPAD2,"VK_NUMPAD2" },
		{ VK_NUMPAD3,"VK_NUMPAD3" },{ VK_NUMPAD4,"VK_NUMPAD4" },{ VK_NUMPAD5,"VK_NUMPAD5" },
		{ VK_NUMPAD6,"VK_NUMPAD6" },{ VK_NUMPAD7,"VK_NUMPAD7" },{ VK_NUMPAD8,"VK_NUMPAD8" },
		{ VK_NUMPAD9,"VK_NUMPAD9" },{ VK_MULTIPLY,"VK_MULTIPLY" },{ VK_ADD,"VK_ADD" },
		{ VK_SEPARATOR,"VK_SEPARATOR" },{ VK_SUBTRACT,"VK_SUBTRACT" },
		{ VK_DECIMAL,"VK_DECIMAL" },{ VK_DIVIDE,"VK_DIVIDE" },
		{ VK_F1,"VK_F1" },{ VK_F2,"VK_F2" },{ VK_F3,"VK_F3" },{ VK_F4,"VK_F4" },
		{ VK_F5,"VK_F5" },{ VK_F6,"VK_F6" },{ VK_F7,"VK_F7" },{ VK_F8,"VK_F8" },
		{ VK_F9,"VK_F9" },{ VK_F10,"VK_F10" },{ VK_F11,"VK_F11" },{ VK_F12,"VK_F12" },
		{ VK_F13,"VK_F13" },{ VK_F14,"VK_F14" },{ VK_F15,"VK_F15" },{ VK_F16,"VK_F16" },
		{ VK_F17,"VK_F17" },{ VK_F18,"VK_F18" },{ VK_F19,"VK_F19" },{ VK_F20,"VK_F20" },
		{ VK_F21,"VK_F21" },{ VK_F22,"VK_F22" },{ VK_F23,"VK_F23" },{ VK_F24,"VK_F24" },
		{ VK_NUMLOCK,"VK_NUMLOCK" },{ VK_SCROLL,"VK_SCROLL" },
		{ VK_LSHIFT,"VK_LSHIFT" },{ VK_RSHIFT,"VK_RSHIFT" },
		{ VK_LCONTROL,"VK_LCONTROL" },{ VK_RCONTROL,"VK_RCONTROL" },
		{ VK_LMENU,"VK_LMENU" },{ VK_RMENU,"VK_RMENU" },
		{ VK_BROWSER_BACK,"VK_BROWSER_BACK" },{ VK_BROWSER_FORWARD,"VK_BROWSER_FORWARD" },
		{ VK_BROWSER_REFRESH,"VK_BROWSER_REFRESH" },{ VK_BROWSER_STOP,"VK_BROWSER_STOP" },
		{ VK_BROWSER_SEARCH,"VK_BROWSER_SEARCH" },{ VK_BROWSER_FAVORITES,"VK_BROWSER_FAVORITES" },
		{ VK_BROWSER_HOME,"VK_BROWSER_HOME" },
		{ VK_VOLUME_MUTE,"VK_VOLUME_MUTE" },{ VK_VOLUME_DOWN,"VK_VOLUME_DOWN" },{ VK_VOLUME_UP,"VK_VOLUME_UP" },
		{ VK_MEDIA_NEXT_TRACK,"VK_MEDIA_NEXT_TRACK" },{ VK_MEDIA_PREV_TRACK,"VK_MEDIA_PREV_TRACK" },
		{ VK_MEDIA_STOP,"VK_MEDIA_STOP" },{ VK_MEDIA_PLAY_PAUSE,"VK_MEDIA_PLAY_PAUSE" },
		{ VK_LAUNCH_MAIL,"VK_LAUNCH_MAIL" },{ VK_LAUNCH_MEDIA_SELECT,"VK_LAUNCH_MEDIA_SELECT" },
		{ VK_LAUNCH_APP1,"VK_LAUNCH_APP1" },{ VK_LAUNCH_APP2,"VK_LAUNCH_APP2" },
		{ VK_OEM_1,"VK_OEM_1" },{ VK_OEM_PLUS,"VK_OEM_PLUS" },{ VK_OEM_COMMA,"VK_OEM_COMMA" },
		{ VK_OEM_MINUS,"VK_OEM_MINUS" },{ VK_OEM_PERIOD,"VK_OEM_PERIOD" },
		{ VK_OEM_2,"VK_OEM_2" },{ VK_OEM_3,"VK_OEM_3" },{ VK_OEM_4,"VK_OEM_4" },
		{ VK_OEM_5,"VK_OEM_5" },{ VK_OEM_6,"VK_OEM_6" },{ VK_OEM_7,"VK_OEM_7" },
		{ VK_OEM_8,"VK_OEM_8" },{ VK_OEM_102,"VK_OEM_102" },
	};

	auto it = s_names.find( vkCode );
	if ( it != s_names.end() ) return it->second;
	if ( vkCode >= 'A' && vkCode <= 'Z' ) { char b[8]; snprintf( b, 8, "VK_%c", vkCode ); return b; }
	if ( vkCode >= '0' && vkCode <= '9' ) { char b[8]; snprintf( b, 8, "VK_%c", vkCode ); return b; }
	char b[16]; snprintf( b, 16, "VK_0x%02X", vkCode );
	return b;
}


// ---------------------------------------------------------------------------
// ParseCombination
// ---------------------------------------------------------------------------
std::vector<int> hotkey::ParseCombination( const std::string &str ) {
	std::vector<int> result;
	if ( str.empty() ) return result;

	auto trim = []( std::string s ) {
		s.erase( 0, s.find_first_not_of( " \t" ) );
		size_t e = s.find_last_not_of( " \t" );
		if ( e != std::string::npos ) s.erase( e + 1 );
		return s;
	};

	if ( str.find( '+' ) == std::string::npos ) {
		int vk = StringToVK( trim( str ) );
		if ( vk ) result.push_back( vk );
		return result;
	}

	std::stringstream ss( str );
	std::string token;
	while ( std::getline( ss, token, '+' ) ) {
		token = trim( token );
		if ( token.empty() ) continue;
		int vk = StringToVK( token );
		if ( vk == 0 ) return {};
		result.push_back( vk );
	}
	return result;
}


// ---------------------------------------------------------------------------
// ExecuteKeyboardCallback — main thread only
// ---------------------------------------------------------------------------
static bool ExecuteKeyboardCallback( hotkey::Context *ctx, const hotkey::KeyboardConfig &config,
                                     const std::vector<int> &combo, bool isPressed ) {
	if ( !ctx || !ctx->rain || !ctx->rain->L ) return true;

	lua_State  *L   = ctx->rain->L;
	int         top = lua_gettop( L );
	std::string key = hotkey::internal::GetUniqueCallbackKey( ctx->rain, config.id );

	lua_getfield( L, LUA_REGISTRYINDEX, key.c_str() );
	if ( !lua_isfunction( L, -1 ) ) { lua_settop( L, top ); return true; }

	lua_newtable( L );

	std::string charStr = combo.empty() ? "" : GetCharFromVK( combo.back() );
	lua_pushlstring( L, charStr.data(), charStr.size() );
	lua_setfield( L, -2, "char" );

	lua_pushinteger( L, combo.empty() ? 0 : combo.back() );
	lua_setfield( L, -2, "code" );

	lua_pushstring( L, isPressed ? "press" : "release" );
	lua_setfield( L, -2, "type" );

	lua_newtable( L );
	int idx = 1;
	for ( int k : combo ) {
		std::string n = hotkey::GetVKNameFromCode( k );
		lua_pushlstring( L, n.c_str(), n.size() );
		lua_rawseti( L, -2, idx++ );
	}
	lua_setfield( L, -2, "keys" );

	std::string vkName = combo.empty() ? "" : hotkey::GetVKNameFromCode( combo.back() );
	lua_pushlstring( L, vkName.c_str(), vkName.size() );
	lua_setfield( L, -2, "vk" );

	lua_pushboolean( L, ( GetKeyState( VK_CAPITAL ) & 0x0001 ) != 0 ); lua_setfield( L, -2, "capslock" );
	lua_pushboolean( L, ( GetKeyState( VK_NUMLOCK ) & 0x0001 ) != 0 ); lua_setfield( L, -2, "numlock" );
	lua_pushboolean( L, ( GetKeyState( VK_SCROLL  ) & 0x0001 ) != 0 ); lua_setfield( L, -2, "scrolllock" );

	auto anyDown = []( int a, int b, int c ) {
		return ( GetAsyncKeyState(a) | GetAsyncKeyState(b) | GetAsyncKeyState(c) ) & 0x8000;
	};
	lua_pushboolean( L, anyDown( VK_LCONTROL, VK_RCONTROL, VK_CONTROL ) != 0 ); lua_setfield( L, -2, "ctrl" );
	lua_pushboolean( L, anyDown( VK_LMENU, VK_RMENU, VK_MENU )         != 0 ); lua_setfield( L, -2, "alt" );
	lua_pushboolean( L, anyDown( VK_LSHIFT, VK_RSHIFT, VK_SHIFT )      != 0 ); lua_setfield( L, -2, "shift" );

	lua_pushnumber ( L, static_cast<lua_Number>( GetTickCount64() ) ); lua_setfield( L, -2, "timestamp" );
	lua_pushboolean( L, hotkey::internal::SkinHasFocus( ctx->rain ) ); lua_setfield( L, -2, "focus" );

	bool shouldBlock = false;
	if ( lua_pcall( L, 1, 1, 0 ) == LUA_OK ) {
		if ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) ) shouldBlock = true;
		lua_pop( L, 1 );
	} else {
		const char *err = lua_tostring( L, -1 );
		if ( err && *err && ctx->rain->rm ) {
			std::wstring msg = L"Hotkey keyboard callback error: " + utf8_to_wstring( err );
			RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
		}
		lua_pop( L, 1 );
	}

	lua_settop( L, top );
	return !shouldBlock;
}


// ---------------------------------------------------------------------------
// LowLevelKeyboardProc
// ---------------------------------------------------------------------------
LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam ) {
	if ( nCode < 0 ) return CallNextHookEx( nullptr, nCode, wParam, lParam );

	KBDLLHOOKSTRUCT *kb         = reinterpret_cast<KBDLLHOOKSTRUCT *>( lParam );
	bool             isPressed  = ( wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN );
	bool             isInjected = ( kb->flags & LLKHF_INJECTED ) != 0;
	int              vkCode     = static_cast<int>( kb->vkCode );
	bool             shouldBlock = false;

	{
		std::lock_guard<std::recursive_mutex> lock( hotkey::internal::g_contextsMutex );

		for ( auto &ctxPair : hotkey::internal::g_contexts ) {
			hotkey::Context *ctx = ctxPair.second;
			if ( !ctx || !ctx->hiddenWindow ) continue;

			for ( auto &cfgPair : ctx->configs ) {
				hotkey::KeyboardConfig &config = cfgPair.second;
				if ( !config.enabled ) continue;
				if ( isInjected && !config.allowInjected ) continue;
				if ( config.requireFocus && !hotkey::internal::SkinHasFocus( ctx->rain ) ) continue;

				bool             matches = false;
				std::vector<int> matchedCombo;

				if ( config.isAllKeys ) {
					matches = true; matchedCombo = { vkCode };
				} else {
					for ( const auto &combo : config.combinations ) {
						if ( std::find( combo.begin(), combo.end(), vkCode ) == combo.end() ) continue;
						bool ok = true;
						for ( int code : combo ) {
							if ( code == vkCode ) continue;
							if ( !( GetAsyncKeyState( code ) & 0x8000 ) ) { ok = false; break; }
						}
						if ( ok ) { matches = true; matchedCombo = combo; break; }
					}
				}
				if ( !matches ) continue;

				bool trigger = ( isPressed  && ( config.eventType == hotkey::EVENT_PRESS  || config.eventType == hotkey::EVENT_BOTH ) )
				            || ( !isPressed && ( config.eventType == hotkey::EVENT_RELEASE || config.eventType == hotkey::EVENT_BOTH ) );
				if ( !trigger ) continue;

				hotkey::HotkeyEvent ev;
				ev.vkCode       = vkCode;
				ev.isPressed    = isPressed;
				ev.configId     = config.id;
				ev.timestamp    = GetTickCount64();
				ev.pressedCombo = matchedCombo;

				{ std::lock_guard<std::mutex> lk( ctx->eventMutex ); ctx->eventBuffer.push( ev ); ctx->hasPendingEvents = true; }

				LRESULT r = SendMessage( ctx->hiddenWindow, WM_HOTKEY_EVENT, 0, 0 );
				if ( r == 1 ) shouldBlock = true;
			}
		}
	}

	return shouldBlock ? 1 : CallNextHookEx( nullptr, nCode, wParam, lParam );
}


// ---------------------------------------------------------------------------
// ProcessKeyboardEvents — called from hotkey::ProcessMessages
// ---------------------------------------------------------------------------
namespace hotkey {

	bool ProcessKeyboardEvents( Context *ctx ) {
		if ( !ctx->hasPendingEvents ) return false;

		std::queue<HotkeyEvent> toProcess;
		{ std::lock_guard<std::mutex> lk( ctx->eventMutex ); toProcess.swap( ctx->eventBuffer ); ctx->hasPendingEvents = false; }

		bool anyBlock = false;
		while ( !toProcess.empty() ) {
			const HotkeyEvent &ev  = toProcess.front();
			auto               cit = ctx->configs.find( ev.configId );
			if ( cit != ctx->configs.end() && cit->second.enabled ) {
				bool passed = ExecuteKeyboardCallback( ctx, cit->second, ev.pressedCombo, ev.isPressed );
				if ( !passed ) anyBlock = true;
			}
			toProcess.pop();
		}
		return anyBlock;
	}

} // namespace hotkey


// ---------------------------------------------------------------------------
// Lua object methods — upvalue layout: [1]=ctx [2]=id
// ---------------------------------------------------------------------------

static int kb_disable( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->configs.find( id );
		if ( it != ctx->configs.end() ) {
			it->second.enabled = false;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int kb_enable( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->configs.find( id );
		if ( it != ctx->configs.end() ) {
			it->second.enabled = true;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int kb_isEnabled( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );
	bool  en  = false;

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->configs.find( id );
		if ( it != ctx->configs.end() ) en = it->second.enabled;
	}
	lua_pushboolean( L, en );
	return 1;
}


static int kb_remove( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->configs.find( id );
		if ( it != ctx->configs.end() ) {
			ctx->configs.erase( it );
			std::string key = hotkey::internal::GetUniqueCallbackKey( ctx->rain, id );
			lua_pushnil( L );
			lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
			if ( ctx->configs.empty() ) hotkey::internal::ReleaseKeyboardHook();
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int kb_gc( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->configs.find( id );
		if ( it != ctx->configs.end() ) {
			ctx->configs.erase( it );
			std::string key = hotkey::internal::GetUniqueCallbackKey( ctx->rain, id );
			lua_pushnil( L );
			lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
			if ( ctx->configs.empty() ) hotkey::internal::ReleaseKeyboardHook();
		}
	}
	return 0;
}


// clang-format off
static const luaL_Reg kb_methods[] = {
	{ "disable",   kb_disable   },
	{ "enable",    kb_enable    },
	{ "isEnabled", kb_isEnabled },
	{ "remove",    kb_remove    },
	{ NULL, NULL }
};
// clang-format on


// ---------------------------------------------------------------------------
// hotkeyKeyboard — Lua binding entry point
// ---------------------------------------------------------------------------
namespace hotkey {

	int hotkeyKeyboard( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		if ( !rain ) { lua_pushnil( L ); lua_pushstring( L, "Rain instance not found" ); return 2; }

		std::lock_guard<std::recursive_mutex> lock( internal::g_contextsMutex );

		Context *ctx = nullptr;
		auto     it  = internal::g_contexts.find( rain );
		if ( it == internal::g_contexts.end() ) {
			ctx = new Context(); ctx->rain = rain;
			internal::g_contexts[rain] = ctx;
			internal::g_liveContexts.insert( ctx );
		} else { ctx = it->second; }

		if ( !ctx->hiddenWindow ) {
			ctx->hiddenWindow = internal::CreateHiddenWindow( ctx );
			if ( !ctx->hiddenWindow ) { lua_pushnil( L ); lua_pushstring( L, "Failed to create dispatch window" ); return 2; }
		}

		luaL_checktype( L, 1, LUA_TTABLE );

		KeyboardConfig config;
		config.id            = ctx->nextId++;
		config.enabled       = true;
		config.eventType     = EVENT_BOTH;
		config.requireFocus  = true;
		config.allowInjected = true;
		config.isAllKeys     = false;

		// vk
		lua_getfield( L, 1, "vk" );
		if ( lua_isstring( L, -1 ) ) {
			const char *s = lua_tostring( L, -1 );
			if ( s && strcmp( s, "all" ) == 0 ) {
				config.isAllKeys = true;
				config.combinations.push_back( { 0 } );
			} else {
				auto combo = ParseCombination( s ? s : "" );
				if ( combo.empty() ) { lua_pop( L, 1 ); lua_pushnil( L ); lua_pushstring( L, "Invalid combination string" ); return 2; }
				config.combinations.push_back( combo );
			}
			lua_pop( L, 1 );
		} else if ( lua_istable( L, -1 ) ) {
			int  len = static_cast<int>( lua_objlen( L, -1 ) );
			bool hasValid = false;
			for ( int i = 1; i <= len; i++ ) {
				lua_rawgeti( L, -1, i );
				if ( lua_isstring( L, -1 ) ) {
					auto combo = ParseCombination( lua_tostring( L, -1 ) );
					if ( !combo.empty() ) { config.combinations.push_back( combo ); hasValid = true; }
				}
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
			if ( !hasValid ) { lua_pushnil( L ); lua_pushstring( L, "No valid combinations" ); return 2; }
		} else {
			lua_pop( L, 1 );
			lua_pushnil( L ); lua_pushstring( L, "vk must be string, table, or \"all\"" ); return 2;
		}

		// on
		lua_getfield( L, 1, "on" );
		if ( lua_isstring( L, -1 ) ) {
			const char *s = lua_tostring( L, -1 );
			if      ( strcmp( s, "press"   ) == 0 ) config.eventType = EVENT_PRESS;
			else if ( strcmp( s, "release" ) == 0 ) config.eventType = EVENT_RELEASE;
		}
		lua_pop( L, 1 );

		// focus
		lua_getfield( L, 1, "focus" );
		if ( !lua_isnil( L, -1 ) ) config.requireFocus = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// allowInjected
		lua_getfield( L, 1, "allowInjected" );
		if ( !lua_isnil( L, -1 ) ) config.allowInjected = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// callback
		lua_getfield( L, 1, "callback" );
		if ( !lua_isfunction( L, -1 ) ) { lua_pop( L, 1 ); lua_pushnil( L ); lua_pushstring( L, "callback is required" ); return 2; }
		std::string regKey = internal::GetUniqueCallbackKey( rain, config.id );
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, regKey.c_str() );
		lua_pop( L, 1 );

		ctx->configs[config.id] = config;

		if ( !internal::EnsureKeyboardHook( rain ) ) {
			ctx->configs.erase( config.id );
			lua_pushnil( L ); lua_pushstring( L, "Failed to install keyboard hook" ); return 2;
		}

		// Build keyboard object
		lua_newtable( L );

		for ( const luaL_Reg *reg = kb_methods; reg->name; ++reg ) {
			lua_pushlightuserdata( L, ctx );
			lua_pushinteger( L, config.id );
			lua_pushcclosure( L, reg->func, 2 );
			lua_setfield( L, -2, reg->name );
		}

		// Metatable with __gc
		lua_newtable( L );
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure( L, kb_gc, 2 );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );

		return 1;
	}

} // namespace hotkey
