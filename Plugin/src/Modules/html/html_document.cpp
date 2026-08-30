/**
 * @file html_document.cpp
 * @brief HTML document implementation.
 */

#include "html_document.hpp"
#include "html_node.hpp"
#include "html_nodelist.hpp"
#include "html_selector.hpp"

#include <Includes/rain.hpp>
#include <Utils/strings.hpp>
#include <vector>

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
		CORE
	============================================================ */

	HtmlDocument *CheckDocument( lua_State *L, int index ) {

		return static_cast<HtmlDocument *>( luaL_checkudata( L, index, "HtmlDocument" ) );
	}



	/* ============================================================
		__tostring  (Bug 9)
	============================================================ */

	static int document_tostring( lua_State *L ) {

		HtmlDocument *doc = static_cast<HtmlDocument *>( lua_touserdata( L, 1 ) );

		if ( !doc || !doc->output ) {
			lua_pushstring( L, "html.document<invalid>" );
			return 1;
		}

		// Gumbo root is GUMBO_NODE_DOCUMENT; walk to find the first element child.
		const char *tagName = "?";
		GumboNode *root = doc->output->root;

		if ( root->type == GUMBO_NODE_DOCUMENT ) {
			for ( unsigned int i = 0; i < root->v.document.children.length; ++i ) {
				GumboNode *child = static_cast<GumboNode *>( root->v.document.children.data[i] );
				if ( child->type == GUMBO_NODE_ELEMENT ) {
					tagName = gumbo_normalized_tagname( child->v.element.tag );
					break;
				}
			}
		} else if ( root->type == GUMBO_NODE_ELEMENT ) {
			tagName = gumbo_normalized_tagname( root->v.element.tag );
		}

		lua_pushfstring( L, "html.document<%s>", tagName );
		return 1;
	}



	/* ============================================================
		doc:root()
	============================================================ */

	int document_root( lua_State *L ) {

		HtmlDocument *doc = CheckDocument( L, 1 );

		if ( !doc || !doc->output ) {
			lua_pushnil( L );
			return 1;
		}

		// Create a registry reference to this document userdata so the node
		// can keep it alive independently of the caller's variable scope.
		lua_pushvalue( L, 1 );
		int docRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNode( L, doc, doc->output->root, docRef );
		return 1;
	}



	/* ============================================================
		doc:find(selector)
	============================================================ */

	int document_find( lua_State *L ) {

		HtmlDocument *doc = CheckDocument( L, 1 );

		const char *selector = luaL_checkstring( L, 2 );

		if ( !doc || !doc->output ) {
			lua_pushnil( L );
			return 1;
		}

		SelectorGroup group;

		if ( !ParseSelectorGroup( selector, group ) ) {
			std::string msg = std::string( "[RainJIT:HTML] doc:find() - invalid selector: " ) + selector;
			HtmlLog( L, LOG_WARNING, msg.c_str() );
			lua_pushnil( L );
			return 1;
		}

		std::vector<GumboNode *> results;
		FindNodesGroup( doc->output->root, group, results );

		lua_pushvalue( L, 1 );
		int docRef = luaL_ref( L, LUA_REGISTRYINDEX );

		PushNodeList( L, doc, results, docRef );
		return 1;
	}



	/* ============================================================
		GC
	============================================================ */

	int document_gc( lua_State *L ) {

		HtmlDocument *doc = CheckDocument( L, 1 );

		if ( doc && doc->output ) {
			gumbo_destroy_output( &kGumboDefaultOptions, doc->output );
			doc->output = nullptr;
		}

		return 0;
	}



	/* ============================================================
		METATABLE
	============================================================ */

	void CreateDocumentMeta( lua_State *L ) {

		luaL_newmetatable( L, "HtmlDocument" );

		lua_pushcfunction( L, document_gc );
		lua_setfield( L, -2, "__gc" );

		lua_pushcfunction( L, document_tostring );
		lua_setfield( L, -2, "__tostring" );

		lua_newtable( L );

		lua_pushcfunction( L, document_root );
		lua_setfield( L, -2, "root" );

		lua_pushcfunction( L, document_find );
		lua_setfield( L, -2, "find" );

		lua_setfield( L, -2, "__index" );

		lua_pop( L, 1 );
	}

} // namespace html
