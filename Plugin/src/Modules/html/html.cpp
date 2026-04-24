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

#include <gumbo.h>

namespace html {

	/* ============================================================
		html.parse(html_string)
	============================================================ */

	/**
	 * @brief Parse HTML string into HtmlDocument.
	 */
	static int html_parse( lua_State *L ) {

		size_t len;

		const char *htmlStr = luaL_checklstring( L, 1, &len );

		HtmlDocument *doc = (HtmlDocument *)lua_newuserdata( L, sizeof( HtmlDocument ) );

		doc->output = gumbo_parse_with_options( &kGumboDefaultOptions, htmlStr, len );

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

	void RegisterModule( lua_State *L ) {

		lua_getglobal( L, "package" );

		lua_getfield( L, -1, "preload" );

		lua_pushcfunction( L, luaopen_html );

		lua_setfield( L, -2, "html" );

		lua_pop( L, 2 );
	}

} // namespace html
