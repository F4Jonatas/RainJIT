/**
 * @file tooltip.cpp
 * @brief Lua bindings for the Tooltip as a Lua module.
 * @license GPL v2.0 License
 *
 * Implements the Lua module for tooltip::Tooltip, allowing Lua scripts to
 * create and drive custom layered tooltips through `require("tooltip")`.
 *
 * @details
 * Each Tooltip instance is bound to a Lua userdata with the following methods:
 * - update(text [, options])
 * - destroy([fadeDuration])
 * - show()
 * - hide()
 * - move(x, y)
 * - isVisible()
 * - hwnd()
 *
 * Error handling: every C function does its C++ work in an inner helper that
 * reports failures through a trivially destructible ParseError. luaL_error is
 * raised only after the helper returned, so no C++ object with a destructor
 * is alive when Lua unwinds the stack.
 *
 * @module tooltip
 * @usage
 * local tooltip = require("tooltip")
 * local tt = tooltip("Hello", { title = "Info" })
 */

#include "tooltip.hpp"

#include <Includes/rain.hpp>
#include <lua.hpp>
#include <utils/strings.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace tooltip {



#define TOOLTIP_META "tooltip.meta"



	/// helpers
	static Tooltip *checkTooltip( lua_State *L, bool allowDestroyed = false ) {
		auto **ud = static_cast<Tooltip **>( luaL_checkudata( L, 1, TOOLTIP_META ) );
		luaL_argcheck( L, ud != nullptr && *ud != nullptr, 1, "Invalid tooltip" );

		Tooltip *t = *ud;

		if ( !allowDestroyed && !t->alive() )
			luaL_error( L, "tooltip has been destroyed" );

		return t;
	}



	/// @brief Writes a (possibly multi-line) warning to the Rainmeter log, one entry per line.
	static void logLines( Rain *rain, const std::string &warning ) {
		std::size_t pos = 0;

		while ( pos < warning.size() ) {
			std::size_t end = warning.find( '\n', pos );

			if ( end == std::string::npos )
				end = warning.size();

			if ( end > pos ) {
				const std::wstring message = L"[RainJIT:Tooltip] " + utf8_to_wstring( warning.substr( pos, end - pos ) );
				RmLog( rain->rm, LOG_WARNING, message.c_str() );
			}

			pos = end + 1;
		}
	}



	/// @brief Forwards a pending warning to the Rainmeter log.
	static void logWarning( Rain *rain, Tooltip *t ) {
		logLines( rain, t->takeWarning() );
	}



	/// @brief Reports option keys that are not recognized (usually a typo).
	static void logUnknown( Rain *rain, const std::string &unknown ) {
		if ( unknown.empty() )
			return;

		logLines( rain, "unknown option(s): " + unknown );
	}



	enum class TextArg {
		Keep,
		Set,
		Clear,
		Invalid
	};



	/**
	 * @brief Reads the text argument.
	 *
	 * - nil / none → Keep
	 * - string / number → Set
	 * - false → Clear
	 */
	static TextArg readText( lua_State *L, int idx, std::string &out ) {
		switch ( lua_type( L, idx ) ) {
		case LUA_TNONE:
		case LUA_TNIL:
			return TextArg::Keep;

		case LUA_TBOOLEAN:
			if ( lua_toboolean( L, idx ) )
				return TextArg::Invalid;

			out.clear();
			return TextArg::Clear;

		case LUA_TSTRING:
		case LUA_TNUMBER: {
			size_t len = 0;
			const char *s = lua_tolstring( L, idx, &len );
			out.assign( s, len );
			return TextArg::Set;
		}

		default:
			return TextArg::Invalid;
		}
	}



	static void createInner( lua_State *L, Rain *rain, Tooltip **slot, ParseError &err ) {
		std::string text;

		if ( readText( L, 2, text ) == TextArg::Invalid ) {
			err.set( "tooltip: text must be a string, number, nil or false" );
			return;
		}

		Options opts;
		Dirty dirty;
		std::string unknown;

		if ( lua_istable( L, 3 ) )
			opts.apply( L, 3, dirty, err, &unknown );
		else if ( !lua_isnoneornil( L, 3 ) )
			err.set( "tooltip: options must be a table" );

		if ( err )
			return;

		logUnknown( rain, unknown );

		auto tip = std::make_unique<Tooltip>();
		const bool opened = tip->open( std::move( text ), std::move( opts ) );

		logWarning( rain, tip.get() );

		if ( !opened ) {
			err.set( "tooltip: failed to create the tooltip window" );
			return;
		}

		*slot = tip.release();
	}



	static void updateInner( lua_State *L, Rain *rain, Tooltip *t, ParseError &err ) {
		Options next = t->Opts;
		std::string text = t->Text;
		std::string unknown;
		Dirty dirty;

		dirty.Show = true;

		switch ( readText( L, 2, text ) ) {
		case TextArg::Invalid:
			err.set( "tooltip: text must be a string, number, nil or false" );
			return;

		case TextArg::Set:
		case TextArg::Clear:
			if ( text != t->Text )
				dirty.Render = true;
			break;

		case TextArg::Keep:
			break;
		}

		if ( lua_istable( L, 3 ) )
			next.apply( L, 3, dirty, err, &unknown );
		else if ( !lua_isnoneornil( L, 3 ) )
			err.set( "tooltip: options must be a table" );

		if ( err )
			return;

		logUnknown( rain, unknown );

		const bool ok = t->commit( std::move( next ), std::move( text ), dirty );

		logWarning( rain, t );

		if ( !ok )
			err.set( "tooltip: update failed" );
	}



	/**
	 * @brief Lua: tooltip(text [, options])
	 *
	 * Creates a tooltip and shows it immediately.
	 *
	 * @param L Lua state.
	 * @return 1 userdata representing the Tooltip instance.
	 *
	 * @pre Measure instance in upvalue 1.
	 * @param[in] text Body text (string, number, nil or false). May be omitted
	 *                 when an image is used.
	 * @param[in] options Optional options table. May be omitted entirely.
	 *
	 * @note Backdrop options: `backdrop` ("acrylic", "mica", "micaAlt", "none" or
	 *       false), `backdropCorner` ("round", "small", "none", "default"), `dark`
	 *       (true, false or "auto") and `backdropTint` (0xRRGGBBAA, acrylic only).
	 *       Acrylic uses SetWindowCompositionAttribute (Windows 10 1803+); mica
	 *       needs Windows 11 22H2+. The material is visible only where
	 *       `backgroundColor` is translucent (low alpha).
	 *
	 * @note `fadeDuration` (ms) animates show, hide, the auto-hide timeout and
	 *       `destroy( fadeDuration )`. Unknown option keys are reported in the
	 *       Rainmeter log as a warning.
	 *
	 * @example
	 * @code{.lua}
	 * local tooltip = require("tooltip")
	 *
	 * -- no options needed
	 * local a = tooltip("Saved!")
	 *
	 * -- with options
	 * local b = tooltip("Saved!", { title = "Done", timeout = 2000 })
	 *
	 * -- acrylic, dark, translucent background
	 * local c = tooltip("Blur", {
	 *     backdrop        = "acrylic",
	 *     backdropCorner  = "round",
	 *     dark            = true,
	 *     backgroundColor = 0x20202040,
	 * })
	 * @endcode
	 */
	static int constructor( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		// Arguments are (module, text, options). Fix the stack at 3 slots first, so
		// omitted arguments are nil and the userdata pushed below never lands on
		// the options slot.
		lua_settop( L, 3 );

		// userdata first, so a failure below still leaves a GC-safe (null) object
		auto **ud = static_cast<Tooltip **>( lua_newuserdata( L, sizeof( Tooltip * ) ) );
		*ud = nullptr;

		luaL_getmetatable( L, TOOLTIP_META );
		lua_setmetatable( L, -2 );

		ParseError err;
		createInner( L, rain, ud, err );

		if ( err )
			return luaL_error( L, "%s", err.message );

		return 1;
	}



	/**
	 * @brief Lua: tt:update(text [, options])
	 *
	 * Updates text and/or options and makes the tooltip visible.
	 * Options are merged with the previous ones. The bitmap is redrawn only when
	 * something visual changed; a pure position change just moves the window.
	 *
	 * @param L Lua state.
	 * @return Always 0 (no return values).
	 *
	 * @param[in] text nil keeps the text, false clears it.
	 * @param[in] options Optional options table (false removes optional entries).
	 *
	 * @example
	 * @code{.lua}
	 * tt:update("New text")
	 * tt:update(nil, { x = 100, y = 100 })   -- move only
	 * tt:update(nil, { title = false })      -- remove the title
	 * @endcode
	 */
	static int tooltip_update( lua_State *L ) {
		auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		Tooltip *t = checkTooltip( L );

		ParseError err;
		updateInner( L, rain, t, err );

		if ( err )
			return luaL_error( L, "%s", err.message );

		return 0;
	}



	/**
	 * @brief Lua: tt:destroy([fadeDuration])
	 *
	 * Destroys the window and releases every resource. Idempotent.
	 * Without an argument the tooltip is destroyed at once; with a fade duration
	 * (milliseconds) it fades out first. Calling `show()` or `update()` during the
	 * fade cancels the destruction.
	 *
	 * @param L Lua state.
	 * @return Always 0 (no return values).
	 *
	 * @param[in] fadeDuration Optional fade-out time in milliseconds (nil, false or 0 = immediate).
	 *
	 * @example
	 * @code{.lua}
	 * tt:destroy()        -- immediate
	 * tt:destroy( 200 )   -- fades out for 200 ms, then destroys
	 * @endcode
	 */
	static int tooltip_destroy( lua_State *L ) {
		Tooltip *t = checkTooltip( L, true );

		double ms = 0.0;

		if ( lua_type( L, 2 ) == LUA_TNUMBER )
			ms = lua_tonumber( L, 2 );
		else if ( !lua_isnoneornil( L, 2 ) && !( lua_type( L, 2 ) == LUA_TBOOLEAN && !lua_toboolean( L, 2 ) ) )
			return luaL_argerror( L, 2, "number expected (fade duration in ms)" );

		t->close( static_cast<std::uint32_t>( ( std::clamp )( ms, 0.0, 60000.0 ) ) );
		return 0;
	}



	/**
	 * @brief Lua: tt:show()
	 *
	 * Shows the tooltip again and restarts the auto-hide timer.
	 */
	static int tooltip_show( lua_State *L ) {
		Tooltip *t = checkTooltip( L );
		t->show();
		return 0;
	}



	/**
	 * @brief Lua: tt:hide()
	 *
	 * Hides the tooltip without destroying it.
	 */
	static int tooltip_hide( lua_State *L ) {
		Tooltip *t = checkTooltip( L );
		t->hide();
		return 0;
	}



	/**
	 * @brief Lua: tt:move(x, y)
	 *
	 * Moves the tooltip to an absolute screen position without redrawing.
	 * Use `update(nil, { x = false, y = false })` to follow the cursor again.
	 *
	 * @param[in] x Screen X (number).
	 * @param[in] y Screen Y (number).
	 */
	static int tooltip_move( lua_State *L ) {
		Tooltip *t = checkTooltip( L );

		const int x = static_cast<int>( luaL_checknumber( L, 2 ) );
		const int y = static_cast<int>( luaL_checknumber( L, 3 ) );

		t->moveTo( x, y );
		return 0;
	}



	/**
	 * @brief Lua: tt:isVisible() → boolean
	 */
	static int tooltip_isVisible( lua_State *L ) {
		Tooltip *t = checkTooltip( L, true );
		lua_pushboolean( L, t->alive() && t->visible() );
		return 1;
	}



	/**
	 * @brief Lua: tt:hwnd() → number|nil
	 *
	 * @return The native window handle, or nil when the tooltip was destroyed.
	 */
	static int tooltip_hwnd( lua_State *L ) {
		Tooltip *t = checkTooltip( L, true );

		if ( !t->alive() ) {
			lua_pushnil( L );
			return 1;
		}

		lua_pushnumber( L, static_cast<lua_Number>( reinterpret_cast<uintptr_t>( t->Win.Handle ) ) );
		return 1;
	}



	/**
	 * @brief Garbage collector for the Tooltip Lua object.
	 *
	 * Destroys the window and frees the Tooltip, so no Finalize hook is needed.
	 */
	static int tooltip_gc( lua_State *L ) {
		auto **ud = static_cast<Tooltip **>( lua_touserdata( L, 1 ) );

		if ( ud && *ud ) {
			delete *ud;
			*ud = nullptr;
		}

		return 0;
	}



	static int tooltip_tostring( lua_State *L ) {
		auto **ud = static_cast<Tooltip **>( lua_touserdata( L, 1 ) );

		if ( ud && *ud && ( *ud )->alive() )
			lua_pushfstring( L, "tooltip<%p>", static_cast<void *>( ( *ud )->Win.Handle ) );
		else
			lua_pushstring( L, "tooltip<destroyed>" );

		return 1;
	}



	// clang-format off
static const luaL_Reg tooltip_methods[] = {
	{ "update",     tooltip_update     },
	{ "destroy",    tooltip_destroy    },
	{ "show",       tooltip_show       },
	{ "hide",       tooltip_hide       },
	{ "move",       tooltip_move       },
	{ "isVisible",  tooltip_isVisible  },
	{ "hwnd",       tooltip_hwnd       },
	{ "__gc",       tooltip_gc         },
	{ "__tostring", tooltip_tostring   },
	{ nullptr,      nullptr            }
};
	// clang-format on



} // namespace tooltip




/**
 * @brief Lua module entry point for Tooltip.
 *
 * Called when Lua executes `require("tooltip")`.
 * Kept as `extern "C"` for semantic compatibility with Lua's module loader.
 *
 * @param L Lua state.
 * @return 1 (module table with __call metamethod).
 *
 * @note Requires a Rain* pointer as upvalue 1.
 */
extern "C" int luaopen_tooltip( lua_State *L ) {
	auto *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

	luaL_newmetatable( L, TOOLTIP_META );

	/* methods + metamethods, all sharing the Rain* upvalue */
	lua_pushlightuserdata( L, rain );
	luaL_setfuncs( L, tooltip::tooltip_methods, 1 );

	/* enable method lookup: userdata → metatable */
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	lua_pop( L, 1 );

	/* module table */
	lua_newtable( L );

	lua_newtable( L );
	lua_pushlightuserdata( L, rain );
	lua_pushcclosure( L, tooltip::constructor, 1 );
	lua_setfield( L, -2, "__call" );

	lua_setmetatable( L, -2 );

	return 1;
}



namespace tooltip {

	/**
	 * @brief Register the tooltip Lua module into package.preload.
	 *
	 * Consistent with depot::RegisterModule, xml::RegisterModule and
	 * html::RegisterModule.
	 *
	 * @param L    Lua state.
	 * @param rain Rainmeter measure context (passed as upvalue to luaopen_tooltip).
	 */
	void RegisterModule( lua_State *L, Rain *rain ) {
		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_tooltip, 1 );

		lua_setfield( L, -2, "tooltip" );

		lua_pop( L, 2 );
	}

} // namespace tooltip
