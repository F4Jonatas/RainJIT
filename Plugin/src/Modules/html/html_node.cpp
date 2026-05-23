/**
 * @file html_node.cpp
 * @brief HTML node implementation.
 */

#include <string>
#include <vector>

#include "html_document.hpp"
#include "html_node.hpp"
#include "html_nodelist.hpp"
#include "html_selector.hpp"

#include <Includes/rain.hpp>
#include <Utils/strings.hpp>

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
		TEXT EXTRACTION  (shared — used by html_nodelist.cpp too)
	============================================================ */

	void ExtractText( GumboNode *node, std::string &out ) {

		if ( node->type == GUMBO_NODE_TEXT ) {
			out += node->v.text.text;
			return;
		}

		if ( node->type != GUMBO_NODE_ELEMENT )
			return;

		GumboVector *children = &node->v.element.children;

		for ( unsigned int i = 0; i < children->length; ++i )
			ExtractText( static_cast<GumboNode *>( children->data[i] ), out );
	}



	/* ============================================================
		USERDATA CORE
	============================================================ */

	void PushNode( lua_State *L, HtmlDocument *doc, GumboNode *node, int docRef ) {

		HtmlNode *wrapper = static_cast<HtmlNode *>( lua_newuserdata( L, sizeof( HtmlNode ) ) );

		wrapper->node   = node;
		wrapper->owner  = doc;
		wrapper->docRef = docRef;

		luaL_getmetatable( L, "HtmlNode" );
		lua_setmetatable( L, -2 );
	}


	HtmlNode *CheckNode( lua_State *L, int index ) {

		return static_cast<HtmlNode *>( luaL_checkudata( L, index, "HtmlNode" ) );
	}



	/* ============================================================
		__gc  (Bug 7 — was missing; Bug 2 — releases docRef)
	============================================================ */

	int node_gc( lua_State *L ) {

		HtmlNode *wrapper = static_cast<HtmlNode *>( lua_touserdata( L, 1 ) );

		if ( wrapper && wrapper->docRef != LUA_NOREF ) {
			luaL_unref( L, LUA_REGISTRYINDEX, wrapper->docRef );
			wrapper->docRef = LUA_NOREF;
		}

		return 0;
	}



	/* ============================================================
		__tostring  (Bug 9)
	============================================================ */

	static int node_tostring( lua_State *L ) {

		HtmlNode *wrapper = static_cast<HtmlNode *>( lua_touserdata( L, 1 ) );

		if ( !wrapper || !wrapper->node ) {
			lua_pushstring( L, "html.node<invalid>" );
			return 1;
		}

		if ( wrapper->node->type == GUMBO_NODE_ELEMENT ) {
			const char *name = gumbo_normalized_tagname( wrapper->node->v.element.tag );
			lua_pushfstring( L, "html.node<%s>", ( name && *name ) ? name : "?" );
		} else if ( wrapper->node->type == GUMBO_NODE_TEXT ) {
			lua_pushstring( L, "html.node<text>" );
		} else {
			lua_pushstring( L, "html.node<other>" );
		}

		return 1;
	}



	/* ============================================================
		node:name()
	============================================================ */

	int node_name( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		if ( wrapper->node->type != GUMBO_NODE_ELEMENT ) {
			lua_pushnil( L );
			return 1;
		}

		const char *name = gumbo_normalized_tagname( wrapper->node->v.element.tag );

		lua_pushstring( L, name );
		return 1;
	}



	/* ============================================================
		node:text()
	============================================================ */

	int node_text( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		std::string text;
		ExtractText( wrapper->node, text );

		lua_pushlstring( L, text.c_str(), text.size() );
		return 1;
	}



	/* ============================================================
		node:children()
	============================================================ */

	int node_children( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		std::vector<GumboNode *> results;

		if ( wrapper->node->type == GUMBO_NODE_ELEMENT ) {

			GumboVector *children = &wrapper->node->v.element.children;

			for ( unsigned int i = 0; i < children->length; ++i ) {

				GumboNode *child = static_cast<GumboNode *>( children->data[i] );

				if ( child->type == GUMBO_NODE_ELEMENT )
					results.push_back( child );
			}
		}

		// Duplicate docRef so the nodelist gets its own independent reference.
		lua_rawgeti( L, LUA_REGISTRYINDEX, wrapper->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNodeList( L, wrapper->owner, results, newRef );
		return 1;
	}



	/* ============================================================
		node:parent()
	============================================================ */

	int node_parent( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		GumboNode *parent = wrapper->node->parent;

		if ( !parent ) {
			lua_pushnil( L );
			return 1;
		}

		lua_rawgeti( L, LUA_REGISTRYINDEX, wrapper->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNode( L, wrapper->owner, parent, newRef );
		return 1;
	}



	/* ============================================================
		node:attr(name)
	============================================================ */

	int node_attr( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		if ( wrapper->node->type != GUMBO_NODE_ELEMENT ) {
			lua_pushnil( L );
			return 1;
		}

		const char *name = luaL_checkstring( L, 2 );

		GumboAttribute *attr = gumbo_get_attribute( &wrapper->node->v.element.attributes, name );

		if ( !attr ) {
			lua_pushnil( L );
			return 1;
		}

		lua_pushstring( L, attr->value );
		return 1;
	}



	/* ============================================================
		node:find(selector)
	============================================================ */

	int node_find( lua_State *L ) {

		HtmlNode *wrapper = CheckNode( L, 1 );

		const char *selector = luaL_checkstring( L, 2 );

		SelectorGroup group;

		if ( !ParseSelectorGroup( selector, group ) ) {
			std::string msg = std::string( "[RainJIT:HTML] node:find() — invalid selector: " ) + selector;
			HtmlLog( L, LOG_WARNING, msg.c_str() );
			lua_pushnil( L );
			return 1;
		}

		std::vector<GumboNode *> results;
		FindNodesGroup( wrapper->node, group, results );

		lua_rawgeti( L, LUA_REGISTRYINDEX, wrapper->docRef );
		int newRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNodeList( L, wrapper->owner, results, newRef );
		return 1;
	}



	/* ============================================================
		METATABLE
	============================================================ */

	void CreateNodeMeta( lua_State *L ) {

		luaL_newmetatable( L, "HtmlNode" );

		lua_pushcfunction( L, node_gc );
		lua_setfield( L, -2, "__gc" );

		lua_pushcfunction( L, node_tostring );
		lua_setfield( L, -2, "__tostring" );

		lua_newtable( L );

		lua_pushcfunction( L, node_name );
		lua_setfield( L, -2, "name" );

		lua_pushcfunction( L, node_text );
		lua_setfield( L, -2, "text" );

		lua_pushcfunction( L, node_children );
		lua_setfield( L, -2, "children" );

		lua_pushcfunction( L, node_parent );
		lua_setfield( L, -2, "parent" );

		lua_pushcfunction( L, node_attr );
		lua_setfield( L, -2, "attr" );

		lua_pushcfunction( L, node_find );
		lua_setfield( L, -2, "find" );

		lua_setfield( L, -2, "__index" );

		lua_pop( L, 1 );
	}

} // namespace html
