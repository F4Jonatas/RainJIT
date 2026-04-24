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

namespace html {

	/* ============================================================
		TEXT EXTRACTION
	============================================================ */

	/**
	 * @brief Recursive text extraction.
	 */
	static void ExtractText( GumboNode *node, std::string &out ) {

		if ( node->type == GUMBO_NODE_TEXT ) {

			out += node->v.text.text;

			return;
		}

		if ( node->type != GUMBO_NODE_ELEMENT )
			return;

		GumboVector *children = &node->v.element.children;

		for ( unsigned int i = 0; i < children->length; ++i ) {

			ExtractText( (GumboNode *)children->data[i], out );
		}
	}



	/* ============================================================
		USERDATA CORE
	============================================================ */

	void PushNode( lua_State *L, HtmlDocument *doc, GumboNode *node ) {

		HtmlNode *wrapper = (HtmlNode *)lua_newuserdata( L, sizeof( HtmlNode ) );

		wrapper->node = node;

		wrapper->owner = doc;

		luaL_getmetatable( L, "HtmlNode" );

		lua_setmetatable( L, -2 );
	}


	HtmlNode *CheckNode( lua_State *L, int index ) {

		return (HtmlNode *)luaL_checkudata( L, index, "HtmlNode" );
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

				GumboNode *child = (GumboNode *)children->data[i];

				if ( child->type == GUMBO_NODE_ELEMENT ) {

					results.push_back( child );
				}
			}
		}

		PushNodeList( L, wrapper->owner, results );

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

		PushNode( L, wrapper->owner, parent );

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

			lua_pushnil( L );
			return 1;
		}

		std::vector<GumboNode *> results;

		FindNodesGroup( wrapper->node, group, results );

		PushNodeList( L, wrapper->owner, results );

		return 1;
	}



	/* ============================================================
		METATABLE
	============================================================ */

	void CreateNodeMeta( lua_State *L ) {

		luaL_newmetatable( L, "HtmlNode" );

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
