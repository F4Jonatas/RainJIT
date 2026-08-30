/**
 * @file html.cpp
 * @brief HTML Lua module entry.
 *
 * Provides:
 * html.parse()
 *
 * Registers:
 * HtmlDocument
 * HtmlNode
 * HtmlNodeList
 */

#include "html.hpp"

#include "html_document.hpp"
#include "html_node.hpp"
#include "html_nodelist.hpp"

#include <Includes/rain.hpp>
#include <Utils/strings.hpp>
#include <gumbo.h>

namespace html {

	/* ============================================================
		Internal logging helper
	============================================================ */

	static void HtmlLog( lua_State *L, int level, const char *msg ) {
#ifdef __RAINMETERAPI_H__
		lua_getfield( L, LUA_REGISTRYINDEX, "__html_rain" );
		Rain *rain = static_cast<Rain *>( lua_touserdata( L, -1 ) );
		lua_pop( L, 1 );
		if ( rain && rain->rm )
			RmLog( rain->rm, level, utf8_to_wstring( msg ).c_str() );
#else
		(void)L;
		(void)level;
		(void)msg;
#endif
	}



	/* ============================================================
		html.parse(html_string)
	============================================================ */

	/**
	 * @brief Parse HTML string into HtmlDocument.
	 *
	 * @return HtmlDocument userdata on success, nil + errmsg on failure.
	 */
	static int html_parse( lua_State *L ) {
		size_t len;
		const char *htmlStr = luaL_checklstring( L, 1, &len );

		HtmlDocument *doc = static_cast<HtmlDocument *>( lua_newuserdata( L, sizeof( HtmlDocument ) ) );
		doc->output = nullptr;

		doc->output = gumbo_parse_with_options( &kGumboDefaultOptions, htmlStr, len );

		if ( !doc->output ) {
			HtmlLog( L, LOG_WARNING, "[RainJIT:HTML] html.parse() failed - gumbo returned nullptr" );
			lua_pop( L, 1 ); // remove userdata
			lua_pushnil( L );
			lua_pushstring( L, "html.parse() failed - gumbo returned nullptr" );
			return 2;
		}

		luaL_getmetatable( L, "HtmlDocument" );
		lua_setmetatable( L, -2 );
		return 1;
	}



	/* ============================================================
		luaopen_html
	============================================================ */

	static int luaopen_html( lua_State *L ) {
		/* Create metatables */
		CreateDocumentMeta( L );
		CreateNodeMeta( L );
		CreateNodeListMeta( L );

		/* Create module table */
		lua_newtable( L );

		lua_pushcfunction( L, html_parse );
		lua_setfield( L, -2, "parse" );

		return 1;
	}



	/* ============================================================
		RegisterModule
	============================================================ */

	void RegisterModule( lua_State *L, Rain *rain ) {
		// Store Rain* in the Lua registry so binding functions can log via RmLog.
		lua_pushlightuserdata( L, static_cast<void *>( rain ) );
		lua_setfield( L, LUA_REGISTRYINDEX, "__html_rain" );

		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );
		lua_pushcfunction( L, luaopen_html );
		lua_setfield( L, -2, "html" );
		lua_pop( L, 2 );
	}

} // namespace html
