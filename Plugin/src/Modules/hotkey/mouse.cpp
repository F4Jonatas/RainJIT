/**
 * @file mouse.cpp
 * @brief Mouse hook implementation for RainJIT hotkey module.
 * @license GPL v2.0 License
 */

#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <string>

#include "hotkey.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>


// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static bool ExecuteMouseCallback( hotkey::Context *ctx, const hotkey::MouseConfig &config, const hotkey::MouseEvent &ev );


// ---------------------------------------------------------------------------
// ExecuteMouseCallback — main thread only
// ---------------------------------------------------------------------------
static bool ExecuteMouseCallback( hotkey::Context *ctx, const hotkey::MouseConfig &config,
                                  const hotkey::MouseEvent &ev ) {
	if ( !ctx || !ctx->rain || !ctx->rain->L ) return true;

	lua_State  *L   = ctx->rain->L;
	int         top = lua_gettop( L );
	std::string key = hotkey::internal::GetMouseCallbackKey( ctx->rain, config.id );

	lua_getfield( L, LUA_REGISTRYINDEX, key.c_str() );
	if ( !lua_isfunction( L, -1 ) ) { lua_settop( L, top ); return true; }

	lua_newtable( L );
	lua_pushstring( L, hotkey::internal::MouseEventName( ev.eventFlag ) ); lua_setfield( L, -2, "type" );
	lua_pushstring( L, hotkey::internal::ButtonName( ev.buttonFlag ) );    lua_setfield( L, -2, "button" );
	lua_pushinteger( L, ev.pos.x );       lua_setfield( L, -2, "x" );
	lua_pushinteger( L, ev.pos.y );       lua_setfield( L, -2, "y" );
	lua_pushinteger( L, ev.delta );       lua_setfield( L, -2, "delta" );
	lua_pushboolean( L, ev.horizontal );  lua_setfield( L, -2, "horizontal" );
	lua_pushnumber ( L, static_cast<lua_Number>( ev.timestamp ) ); lua_setfield( L, -2, "timestamp" );
	lua_pushboolean( L, hotkey::internal::SkinHasFocus( ctx->rain ) ); lua_setfield( L, -2, "focus" );

	bool shouldBlock = false;
	if ( lua_pcall( L, 1, 1, 0 ) == LUA_OK ) {
		if ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) ) shouldBlock = true;
		lua_pop( L, 1 );
	} else {
		const char *err = lua_tostring( L, -1 );
		if ( err && *err && ctx->rain->rm ) {
			std::wstring msg = L"Hotkey mouse callback error: " + utf8_to_wstring( err );
			RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
		}
		lua_pop( L, 1 );
	}

	lua_settop( L, top );
	return !shouldBlock;
}


// ---------------------------------------------------------------------------
// LowLevelMouseProc
// ---------------------------------------------------------------------------
LRESULT CALLBACK LowLevelMouseProc( int nCode, WPARAM wParam, LPARAM lParam ) {
	if ( nCode < 0 ) return CallNextHookEx( nullptr, nCode, wParam, lParam );

	MSLLHOOKSTRUCT *ms = reinterpret_cast<MSLLHOOKSTRUCT *>( lParam );

	hotkey::MouseRawEvent raw;
	raw.message    = static_cast<UINT>( wParam );
	raw.pos        = ms->pt;
	raw.injected   = ( ms->flags & LLMHF_INJECTED ) != 0;
	raw.timestamp  = GetTickCount64();

	if ( wParam == WM_MOUSEWHEEL ) {
		raw.delta      = GET_WHEEL_DELTA_WPARAM( ms->mouseData );
		raw.horizontal = false;
	} else if ( wParam == WM_MOUSEHWHEEL ) {
		raw.delta      = GET_WHEEL_DELTA_WPARAM( ms->mouseData );
		raw.horizontal = true;
	}

	bool shouldBlock = false;

	{
		std::lock_guard<std::recursive_mutex> lock( hotkey::internal::g_contextsMutex );

		for ( auto &ctxPair : hotkey::internal::g_contexts ) {
			hotkey::Context *ctx = ctxPair.second;
			if ( !ctx || !ctx->hiddenWindow || ctx->mouseConfigs.empty() ) continue;

			bool anyInterested = false;
			for ( auto &cfgPair : ctx->mouseConfigs ) {
				if ( !cfgPair.second.enabled ) continue;
				if ( raw.injected && !cfgPair.second.allowInjected ) continue;
				anyInterested = true;
				break;
			}
			if ( !anyInterested ) continue;

			{ std::lock_guard<std::mutex> lk( ctx->mouseRawMutex ); ctx->mouseRawBuffer.push( raw ); ctx->hasPendingMouseEvents = true; }

			LRESULT r = SendMessage( ctx->hiddenWindow, WM_MOUSE_EVENT, 0, 0 );
			if ( r == 1 ) shouldBlock = true;
		}
	}

	return shouldBlock ? 1 : CallNextHookEx( nullptr, nCode, wParam, lParam );
}


// ---------------------------------------------------------------------------
// ProcessMouseEvents — called from hotkey::ProcessMessages
// ---------------------------------------------------------------------------
namespace hotkey {

	bool ProcessMouseEvents( Context *ctx ) {
		if ( !ctx->hasPendingMouseEvents ) return false;

		std::queue<MouseRawEvent> rawQueue;
		{ std::lock_guard<std::mutex> lk( ctx->mouseRawMutex ); rawQueue.swap( ctx->mouseRawBuffer ); ctx->hasPendingMouseEvents = false; }

		bool anyBlock = false;

		static const uint32_t s_btnFlags[5] = { MBUTTON_LEFT, MBUTTON_RIGHT, MBUTTON_MIDDLE, MBUTTON_X1, MBUTTON_X2 };

		// Dispatch a logical event to all matching configs
		auto dispatch = [&]( uint32_t evFlag, uint32_t btnFlag, const MouseRawEvent &raw, int delta = 0, bool horiz = false ) {
			MouseEvent ev;
			ev.eventFlag  = evFlag;
			ev.buttonFlag = btnFlag;
			ev.pos        = raw.pos;
			ev.delta      = delta;
			ev.horizontal = horiz;
			ev.timestamp  = raw.timestamp;

			for ( auto &cfgPair : ctx->mouseConfigs ) {
				MouseConfig &cfg = cfgPair.second;
				if ( !cfg.enabled ) continue;
				if ( raw.injected && !cfg.allowInjected ) continue;
				if ( cfg.requireFocus && !internal::SkinHasFocus( ctx->rain ) ) continue;
				if ( !( cfg.eventFlags & evFlag ) ) continue;
				if ( btnFlag != MBUTTON_NONE && !( cfg.buttonFlags & btnFlag ) ) continue;
				ev.configId = cfg.id;
				bool passed = ExecuteMouseCallback( ctx, cfg, ev );
				if ( !passed ) anyBlock = true;
			}
		};

		while ( !rawQueue.empty() ) {
			const MouseRawEvent &raw = rawQueue.front();

			uint32_t buttonFlag = MBUTTON_NONE;
			bool isPress = false, isRelease = false, isMove = false, isScroll = false;

			switch ( raw.message ) {
				case WM_LBUTTONDOWN:  buttonFlag = MBUTTON_LEFT;   isPress   = true;  break;
				case WM_LBUTTONUP:    buttonFlag = MBUTTON_LEFT;   isRelease = true;  break;
				case WM_RBUTTONDOWN:  buttonFlag = MBUTTON_RIGHT;  isPress   = true;  break;
				case WM_RBUTTONUP:    buttonFlag = MBUTTON_RIGHT;  isRelease = true;  break;
				case WM_MBUTTONDOWN:  buttonFlag = MBUTTON_MIDDLE; isPress   = true;  break;
				case WM_MBUTTONUP:    buttonFlag = MBUTTON_MIDDLE; isRelease = true;  break;
				case WM_XBUTTONDOWN:  buttonFlag = MBUTTON_X1;     isPress   = true;  break;
				case WM_XBUTTONUP:    buttonFlag = MBUTTON_X1;     isRelease = true;  break;
				case WM_MOUSEMOVE:    isMove     = true;                               break;
				case WM_MOUSEWHEEL:
				case WM_MOUSEHWHEEL:  isScroll   = true;                               break;
				default: rawQueue.pop(); continue;
			}

			if ( isMove ) {
				// Cancel longpress if moved beyond tolerance
				for ( int bi = 0; bi < 5; bi++ ) {
					ButtonState &bs = ctx->buttonState[bi];
					if ( !bs.isDown || bs.longPressFired ) continue;
					int dx = raw.pos.x - bs.pressPos.x;
					int dy = raw.pos.y - bs.pressPos.y;
					if ( abs( dx ) > internal::LONGPRESS_TOLERANCE || abs( dy ) > internal::LONGPRESS_TOLERANCE ) {
						bs.isDown = false; bs.longPressFired = false;
					}
				}
				dispatch( ME_MOVE, MBUTTON_NONE, raw );
			}

			else if ( isScroll ) {
				dispatch( ME_SCROLL, MBUTTON_NONE, raw, raw.delta, raw.horizontal );
			}

			else if ( isPress && buttonFlag != MBUTTON_NONE ) {
				int bi = internal::ButtonIndex( buttonFlag );
				if ( bi >= 0 ) {
					ButtonState &bs   = ctx->buttonState[bi];
					bs.isDown         = true;
					bs.pressTime      = raw.timestamp;
					bs.pressPos       = raw.pos;
					bs.longPressFired = false;
				}
				dispatch( ME_PRESS, buttonFlag, raw );
			}

			else if ( isRelease && buttonFlag != MBUTTON_NONE ) {
				int bi = internal::ButtonIndex( buttonFlag );

				if ( bi >= 0 ) {
					ButtonState &bs = ctx->buttonState[bi];

					if ( bs.isDown && !bs.longPressFired ) {
						ULONGLONG elapsed = raw.timestamp - bs.pressTime;
						int dx = raw.pos.x - bs.pressPos.x;
						int dy = raw.pos.y - bs.pressPos.y;
						bool withinTolerance = ( abs( dx ) <= internal::LONGPRESS_TOLERANCE && abs( dy ) <= internal::LONGPRESS_TOLERANCE );

						// Find minimum holdTime among interested configs
						int holdTime = 500;
						for ( auto &cfgPair : ctx->mouseConfigs )
							if ( cfgPair.second.enabled && ( cfgPair.second.buttonFlags & buttonFlag ) )
								holdTime = std::min( holdTime, cfgPair.second.holdTime );

						if ( withinTolerance && elapsed < static_cast<ULONGLONG>( holdTime ) ) {
							// Determine doubleclick window
							int doubleTime = 500;
							for ( auto &cfgPair : ctx->mouseConfigs )
								if ( cfgPair.second.enabled && ( cfgPair.second.buttonFlags & buttonFlag ) )
									doubleTime = std::min( doubleTime, cfgPair.second.doubleTime );

							ULONGLONG sinceLast = raw.timestamp - bs.lastClickTime;
							int ldx = raw.pos.x - bs.lastClickPos.x;
							int ldy = raw.pos.y - bs.lastClickPos.y;
							bool nearLast = ( abs( ldx ) <= internal::LONGPRESS_TOLERANCE * 4 && abs( ldy ) <= internal::LONGPRESS_TOLERANCE * 4 );

							if ( sinceLast < static_cast<ULONGLONG>( doubleTime ) && nearLast ) {
								dispatch( ME_DOUBLECLICK, buttonFlag, raw );
								bs.lastClickTime = 0;
							} else {
								dispatch( ME_CLICK, buttonFlag, raw );
								bs.lastClickTime = raw.timestamp;
								bs.lastClickPos  = raw.pos;
							}
						}
					}

					bs.isDown = false; bs.longPressFired = false;
				}

				dispatch( ME_RELEASE, buttonFlag, raw );
			}

			// Check longpress for all held buttons on every event
			for ( int bi = 0; bi < 5; bi++ ) {
				ButtonState &bs = ctx->buttonState[bi];
				if ( !bs.isDown || bs.longPressFired ) continue;

				int holdTime = 500;
				for ( auto &cfgPair : ctx->mouseConfigs )
					if ( cfgPair.second.enabled && ( cfgPair.second.eventFlags & ME_LONGPRESS ) && ( cfgPair.second.buttonFlags & s_btnFlags[bi] ) )
						holdTime = std::min( holdTime, cfgPair.second.holdTime );

				if ( raw.timestamp - bs.pressTime >= static_cast<ULONGLONG>( holdTime ) ) {
					bs.longPressFired = true;
					dispatch( ME_LONGPRESS, s_btnFlags[bi], raw );
				}
			}

			rawQueue.pop();
		}

		return anyBlock;
	}

} // namespace hotkey


// ---------------------------------------------------------------------------
// Lua object methods — upvalue layout: [1]=ctx [2]=id
// ---------------------------------------------------------------------------

static int ms_disable( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->mouseConfigs.find( id );
		if ( it != ctx->mouseConfigs.end() ) {
			it->second.enabled = false;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int ms_enable( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->mouseConfigs.find( id );
		if ( it != ctx->mouseConfigs.end() ) {
			it->second.enabled = true;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int ms_isEnabled( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );
	bool  en  = false;

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->mouseConfigs.find( id );
		if ( it != ctx->mouseConfigs.end() ) en = it->second.enabled;
	}
	lua_pushboolean( L, en );
	return 1;
}


static int ms_remove( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->mouseConfigs.find( id );
		if ( it != ctx->mouseConfigs.end() ) {
			ctx->mouseConfigs.erase( it );
			std::string key = hotkey::internal::GetMouseCallbackKey( ctx->rain, id );
			lua_pushnil( L );
			lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
			if ( ctx->mouseConfigs.empty() ) hotkey::internal::ReleaseMouseHook();
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}


static int ms_gc( lua_State *L ) {
	auto *ctx = static_cast<hotkey::Context *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
	int   id  = static_cast<int>( lua_tointeger( L, lua_upvalueindex( 2 ) ) );

	if ( ctx && hotkey::internal::g_liveContexts.count( ctx ) ) {
		auto it = ctx->mouseConfigs.find( id );
		if ( it != ctx->mouseConfigs.end() ) {
			ctx->mouseConfigs.erase( it );
			std::string key = hotkey::internal::GetMouseCallbackKey( ctx->rain, id );
			lua_pushnil( L );
			lua_setfield( L, LUA_REGISTRYINDEX, key.c_str() );
			if ( ctx->mouseConfigs.empty() ) hotkey::internal::ReleaseMouseHook();
		}
	}
	return 0;
}


// clang-format off
static const luaL_Reg ms_methods[] = {
	{ "disable",   ms_disable   },
	{ "enable",    ms_enable    },
	{ "isEnabled", ms_isEnabled },
	{ "remove",    ms_remove    },
	{ NULL, NULL }
};
// clang-format on


// ---------------------------------------------------------------------------
// hotkeyMouse — Lua binding entry point
// ---------------------------------------------------------------------------
namespace hotkey {

	int hotkeyMouse( lua_State *L ) {
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

		MouseConfig config;
		config.id            = ctx->nextId++;
		config.enabled       = true;
		config.requireFocus  = true;
		config.allowInjected = true;
		config.holdTime      = 500;
		config.doubleTime    = 500;
		config.buttonFlags   = MBUTTON_ALL;
		config.eventFlags    = ME_ALL;

		auto parseButton = []( const char *s ) -> uint32_t {
			if ( !s ) return MBUTTON_NONE;
			if ( strcmp( s, "all"    ) == 0 ) return MBUTTON_ALL;
			if ( strcmp( s, "left"   ) == 0 ) return MBUTTON_LEFT;
			if ( strcmp( s, "right"  ) == 0 ) return MBUTTON_RIGHT;
			if ( strcmp( s, "middle" ) == 0 ) return MBUTTON_MIDDLE;
			if ( strcmp( s, "x1"     ) == 0 ) return MBUTTON_X1;
			if ( strcmp( s, "x2"     ) == 0 ) return MBUTTON_X2;
			return MBUTTON_NONE;
		};

		auto parseEvent = []( const char *s ) -> uint32_t {
			if ( !s ) return ME_NONE;
			if ( strcmp( s, "all"         ) == 0 ) return ME_ALL;
			if ( strcmp( s, "press"       ) == 0 ) return ME_PRESS;
			if ( strcmp( s, "release"     ) == 0 ) return ME_RELEASE;
			if ( strcmp( s, "click"       ) == 0 ) return ME_CLICK;
			if ( strcmp( s, "doubleclick" ) == 0 ) return ME_DOUBLECLICK;
			if ( strcmp( s, "longpress"   ) == 0 ) return ME_LONGPRESS;
			if ( strcmp( s, "move"        ) == 0 ) return ME_MOVE;
			if ( strcmp( s, "scroll"      ) == 0 ) return ME_SCROLL;
			return ME_NONE;
		};

		// button
		lua_getfield( L, 1, "button" );
		if ( lua_isstring( L, -1 ) ) {
			config.buttonFlags = parseButton( lua_tostring( L, -1 ) );
		} else if ( lua_istable( L, -1 ) ) {
			config.buttonFlags = MBUTTON_NONE;
			int len = static_cast<int>( lua_objlen( L, -1 ) );
			for ( int i = 1; i <= len; i++ ) {
				lua_rawgeti( L, -1, i );
				if ( lua_isstring( L, -1 ) ) config.buttonFlags |= parseButton( lua_tostring( L, -1 ) );
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		// on
		lua_getfield( L, 1, "on" );
		if ( lua_isstring( L, -1 ) ) {
			config.eventFlags = parseEvent( lua_tostring( L, -1 ) );
		} else if ( lua_istable( L, -1 ) ) {
			config.eventFlags = ME_NONE;
			int len = static_cast<int>( lua_objlen( L, -1 ) );
			for ( int i = 1; i <= len; i++ ) {
				lua_rawgeti( L, -1, i );
				if ( lua_isstring( L, -1 ) ) config.eventFlags |= parseEvent( lua_tostring( L, -1 ) );
				lua_pop( L, 1 );
			}
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

		// holdTime
		lua_getfield( L, 1, "holdTime" );
		if ( lua_isnumber( L, -1 ) ) config.holdTime = static_cast<int>( lua_tointeger( L, -1 ) );
		lua_pop( L, 1 );

		// doubleTime
		lua_getfield( L, 1, "doubleTime" );
		if ( lua_isnumber( L, -1 ) ) config.doubleTime = static_cast<int>( lua_tointeger( L, -1 ) );
		lua_pop( L, 1 );

		// callback
		lua_getfield( L, 1, "callback" );
		if ( !lua_isfunction( L, -1 ) ) { lua_pop( L, 1 ); lua_pushnil( L ); lua_pushstring( L, "callback is required" ); return 2; }
		std::string regKey = internal::GetMouseCallbackKey( rain, config.id );
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, regKey.c_str() );
		lua_pop( L, 1 );

		ctx->mouseConfigs[config.id] = config;

		if ( !internal::EnsureMouseHook( rain ) ) {
			ctx->mouseConfigs.erase( config.id );
			lua_pushnil( L ); lua_pushstring( L, "Failed to install mouse hook" ); return 2;
		}

		// Build mouse object
		lua_newtable( L );

		for ( const luaL_Reg *reg = ms_methods; reg->name; ++reg ) {
			lua_pushlightuserdata( L, ctx );
			lua_pushinteger( L, config.id );
			lua_pushcclosure( L, reg->func, 2 );
			lua_setfield( L, -2, reg->name );
		}

		// Metatable with __gc
		lua_newtable( L );
		lua_pushlightuserdata( L, ctx );
		lua_pushinteger( L, config.id );
		lua_pushcclosure( L, ms_gc, 2 );
		lua_setfield( L, -2, "__gc" );
		lua_setmetatable( L, -2 );

		return 1;
	}

} // namespace hotkey
