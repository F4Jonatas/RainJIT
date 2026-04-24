/**
 * @file html_document.cpp
 * @brief HTML document implementation.
 */

#include "html_document.hpp"
#include "html_node.hpp"
#include "html_nodelist.hpp"
#include "html_selector.hpp"

#include <vector>

namespace html {

	/* ============================================================
		CORE
	============================================================ */

	HtmlDocument *CheckDocument( lua_State *L, int index ) {

		return (HtmlDocument *)luaL_checkudata( L, index, "HtmlDocument" );
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

		GumboNode *root = doc->output->root;

		PushNode( L, doc, root );

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

			lua_pushnil( L );
			return 1;
		}

		std::vector<GumboNode *> results;

		FindNodesGroup( doc->output->root, group, results );

		PushNodeList( L, doc, results );

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

		lua_newtable( L );

		lua_pushcfunction( L, document_root );

		lua_setfield( L, -2, "root" );

		lua_pushcfunction( L, document_find );

		lua_setfield( L, -2, "find" );

		lua_setfield( L, -2, "__index" );

		lua_pop( L, 1 );
	}

} // namespace html
