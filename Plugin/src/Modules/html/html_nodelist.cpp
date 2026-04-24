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

	void PushNodeList( lua_State *L, HtmlDocument *doc, const std::vector<GumboNode *> &nodes ) {

		HtmlNodeList *list = (HtmlNodeList *)lua_newuserdata( L, sizeof( HtmlNodeList ) );

		new ( &list->nodes ) std::vector<GumboNode *>( nodes );

		list->owner = doc;

		luaL_getmetatable( L, "HtmlNodeList" );

		lua_setmetatable( L, -2 );
	}


	HtmlNodeList *CheckNodeList( lua_State *L, int index ) {

		return (HtmlNodeList *)luaL_checkudata( L, index, "HtmlNodeList" );
	}



	/* ============================================================
		list:count()
	============================================================ */

	static int list_count( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		lua_pushinteger( L, (int)list->nodes.size() );

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

		PushNode( L, list->owner, list->nodes[0] );

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

		PushNode( L, list->owner, list->nodes.back() );

		return 1;
	}



	/* ============================================================
		list:eq(index)
	============================================================ */

	static int list_eq( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		int index = luaL_checkinteger( L, 2 );

		index -= 1;

		if ( index < 0 || index >= (int)list->nodes.size() ) {

			lua_pushnil( L );
			return 1;
		}

		PushNode( L, list->owner, list->nodes[index] );

		return 1;
	}



	/* ============================================================
		list:text()
	============================================================ */

	static void ExtractTextRecursive( GumboNode *node, std::string &out ) {

		if ( node->type == GUMBO_NODE_TEXT ) {

			out += node->v.text.text;

			return;
		}

		if ( node->type != GUMBO_NODE_ELEMENT )
			return;

		GumboVector *children = &node->v.element.children;

		for ( unsigned int i = 0; i < children->length; ++i ) {

			ExtractTextRecursive( (GumboNode *)children->data[i], out );
		}
	}


	static int list_text( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		std::string text;

		for ( size_t i = 0; i < list->nodes.size(); ++i ) {

			ExtractTextRecursive( list->nodes[i], text );
		}

		lua_pushlstring( L, text.c_str(), text.size() );

		return 1;
	}



	/* ============================================================
		list:attr(name)
	============================================================ */

	static int list_attr( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		const char *name = luaL_checkstring( L, 2 );

		for ( size_t i = 0; i < list->nodes.size(); ++i ) {

			GumboNode *node = list->nodes[i];

			if ( node->type != GUMBO_NODE_ELEMENT )
				continue;

			GumboAttribute *attr = gumbo_get_attribute( &node->v.element.attributes, name );

			if ( attr ) {

				lua_pushstring( L, attr->value );

				return 1;
			}
		}

		lua_pushnil( L );

		return 1;
	}



	/* ============================================================
		GC
	============================================================ */

	static int list_gc( lua_State *L ) {

		HtmlNodeList *list = CheckNodeList( L, 1 );

		list->nodes.~vector();

		return 0;
	}



	/* ============================================================
		METATABLE
	============================================================ */

	void CreateNodeListMeta( lua_State *L ) {

		luaL_newmetatable( L, "HtmlNodeList" );

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

		lua_pushcfunction( L, list_gc );
		lua_setfield( L, -2, "__gc" );

		lua_pop( L, 1 );
	}

} // namespace html
