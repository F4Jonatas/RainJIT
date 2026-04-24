/**
 * @file html_document.hpp
 * @brief HTML document userdata definitions.
 */

#pragma once

#include <gumbo.h>
#include <lua.hpp>

namespace html {

	/* Forward declaration */
	typedef struct HtmlNode HtmlNode;

	/**
	 * @brief Represents a parsed HTML document.
	 */
	typedef struct HtmlDocument {

		GumboOutput *output;

	} HtmlDocument;


	/**
	 * @brief Validate document userdata.
	 */
	HtmlDocument *CheckDocument( lua_State *L, int index );


	/**
	 * @brief Get root node.
	 *
	 * Lua:
	 * doc:root()
	 */
	int document_root( lua_State *L );


	/**
	 * @brief Find nodes from document root.
	 *
	 * Lua:
	 * doc:find("div a")
	 */
	int document_find( lua_State *L );


	/**
	 * @brief Document garbage collector.
	 */
	int document_gc( lua_State *L );


	/**
	 * @brief Create document metatable.
	 */
	void CreateDocumentMeta( lua_State *L );

} // namespace html
