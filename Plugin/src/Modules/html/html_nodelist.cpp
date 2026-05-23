/**
 * @file html_nodelist.cpp
 * @brief HtmlNodeList implementation.
 */

#include "html_nodelist.hpp"
#include "html_node.hpp"

#include <string>

namespace html {

	/* ============================================================
		CORE
	============================================================ */

	void PushNodeList( lua_State *L, HtmlDocument *doc, const std::vector<GumboNode *> &nodes, int docRef ) {

		HtmlNodeList *list = static_cast<HtmlNodeList *>( lua_newuserdata( L, sizeof( HtmlNodeList ) ) );

		new ( &list->nodes ) std::vector<GumboNode *>( nodes );

		list->owner  = doc;
		list->docRef = docRef;

		luaL_getmetatable( L, "HtmlNodeList" );
		lua_setmetatable( L, -2 );
	}


	HtmlNodeList *CheckNodeList( lua_State *L, int index ) {

		return static_cast<HtmlNodeList *>( luaL_checkudata( L, index, "HtmlNodeList" ) );
	}



	/* ============================================================
		__tostring  (Bug 9)
	============================================================ */

	static int list_tostring( lua_State *L ) {

		HtmlNodeList *list = static_cast<HtmlNodeList *>( lua_touserdata( L, 1 ) );

		if ( !list ) {
			lua_pushstring( L, "html.nodeList[?]" );
			return 1;
		}

		lua_pushfstring( L, "html.nodeList[%d]", static_cast<int>( list->nodes.size() ) );
		return 1;
	}



	/* ============================================================
		list:count()
	============================================================ */

	static int list_count( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		lua_pushinteger( L, static_cast<int>( list->nodes.size() ) );
		return 1;
	}



	/* ============================================================
		list:first()
	============================================================ */

	static int list_first( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		if ( list->nodes.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		lua_rawgeti( L, LUA_REGISTRYINDEX, list->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNode( L, list->owner, list->nodes[0], newRef );
		return 1;
	}



	/* ============================================================
		list:last()
	============================================================ */

	static int list_last( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		if ( list->nodes.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		lua_rawgeti( L, LUA_REGISTRYINDEX, list->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNode( L, list->owner, list->nodes.back(), newRef );
		return 1;
	}



	/* ============================================================
		list:eq(index)  — 1-based
	============================================================ */

	static int list_eq( lua_State *L ) {

		HtmlNodeList *list  = CheckNodeList( L, 1 );
		int           index = static_cast<int>( luaL_checkinteger( L, 2 ) ) - 1;

		if ( index < 0 || index >= static_cast<int>( list->nodes.size() ) ) {
			lua_pushnil( L );
			return 1;
		}

		lua_rawgeti( L, LUA_REGISTRYINDEX, list->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNode( L, list->owner, list->nodes[index], newRef );
		return 1;
	}



	/* ============================================================
		list:text()  — uses shared ExtractText from html_node.cpp
	============================================================ */

	static int list_text( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		std::string text;

		for ( size_t i = 0; i < list->nodes.size(); ++i )
			ExtractText( list->nodes[i], text );

		lua_pushlstring( L, text.c_str(), text.size() );
		return 1;
	}



	/* ============================================================
		list:attr(name)
		Returns the attribute from nodes[0] (the first matched element),
		consistent with jQuery's .attr() behaviour on a collection.
		Returns nil if the list is empty or the first node lacks the attribute.
	============================================================ */

	static int list_attr( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		if ( list->nodes.empty() ) {
			lua_pushnil( L );
			return 1;
		}

		const char *name = luaL_checkstring( L, 2 );

		GumboNode *node = list->nodes[0];

		if ( node->type != GUMBO_NODE_ELEMENT ) {
			lua_pushnil( L );
			return 1;
		}

		GumboAttribute *attr = gumbo_get_attribute( &node->v.element.attributes, name );

		if ( !attr ) {
			lua_pushnil( L );
			return 1;
		}

		lua_pushstring( L, attr->value );
		return 1;
	}



	/* ============================================================
		GC  (Bug 2 — releases docRef)
	============================================================ */

	static int list_gc( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		list->nodes.~vector();

		if ( list->docRef != LUA_NOREF ) {
			luaL_unref( L, LUA_REGISTRYINDEX, list->docRef );
			list->docRef = LUA_NOREF;
		}

		return 0;
	}



	/* ============================================================
		METATABLE
	============================================================ */

	void CreateNodeListMeta( lua_State *L ) {

		luaL_newmetatable( L, "HtmlNodeList" );

		lua_pushcfunction( L, list_tostring );
		lua_setfield( L, -2, "__tostring" );

		lua_pushcfunction( L, list_gc );
		lua_setfield( L, -2, "__gc" );

		lua_newtable( L );

		lua_pushcfunction( L, list_count );
		lua_setfield( L, -2, "count" );

		lua_pushcfunction( L, list_first );
		lua_setfield( L, -2, "first" );

		lua_pushcfunction( L, list_last );
		lua_setfield( L, -2, "last" );

		lua_pushcfunction( L, list_eq );
		lua_setfield( L, -2, "eq" );

		lua_pushcfunction( L, list_text );
		lua_setfield( L, -2, "text" );

		lua_pushcfunction( L, list_attr );
		lua_setfield( L, -2, "attr" );

		lua_setfield( L, -2, "__index" );

		lua_pop( L, 1 );
	}

} // namespace html
