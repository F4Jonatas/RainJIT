/**
 * @file html_node.hpp
 * @brief HTML node userdata definitions.
 *
 * Provides read-only DOM node access.
 *
 * Methods exposed to Lua:
 *
 * node:name()
 * node:text()
 * node:attr(name)
 * node:children()
 * node:parent()
 * node:find(selector)
 */

#pragma once

#include <gumbo.h>
#include <lua.hpp>
#include <string>

#include "html_nodelist.hpp"

namespace html {

	/* Forward declaration */
	typedef struct HtmlDocument HtmlDocument;

	/**
	 * @brief Represents a node wrapper.
	 *
	 * @note docRef is a Lua registry reference to the owning HtmlDocument
	 *       userdata. It keeps the document alive (prevents GC) for as long
	 *       as this node is reachable, avoiding use-after-free on owner->output.
	 */
	typedef struct HtmlNode {

		GumboNode *node;

		HtmlDocument *owner;

		/// @brief Lua registry reference to the owning HtmlDocument userdata.
		int docRef;

	} HtmlNode;


	/**
	 * @brief Shared recursive text extraction.
	 *
	 * Declared here so html_nodelist.cpp can use it without duplicating the
	 * implementation.
	 *
	 * @param node  Gumbo node to extract text from.
	 * @param out   Output string (appended to).
	 */
	void ExtractText( GumboNode *node, std::string &out );


	/**
	 * @brief Push node userdata onto the Lua stack.
	 *
	 * @param docRef Lua registry reference to the owning HtmlDocument.
	 *               The node takes ownership of this ref and releases it in __gc.
	 */
	void PushNode( lua_State *L, HtmlDocument *doc, GumboNode *node, int docRef );


	/**
	 * @brief Validate node userdata.
	 */
	HtmlNode *CheckNode( lua_State *L, int index );


	/**
	 * @brief Node garbage collector - releases docRef.
	 */
	int node_gc( lua_State *L );


	/**
	 * @brief Node name.
	 *
	 * Lua:
	 * node:name()
	 */
	int node_name( lua_State *L );


	/**
	 * @brief Extract node text.
	 *
	 * Lua:
	 * node:text()
	 */
	int node_text( lua_State *L );


	/**
	 * @brief Get children nodes.
	 *
	 * Lua:
	 * node:children()
	 */
	int node_children( lua_State *L );


	/**
	 * @brief Get parent node.
	 *
	 * Lua:
	 * node:parent()
	 */
	int node_parent( lua_State *L );


	/**
	 * @brief Get attribute value.
	 *
	 * Lua:
	 * node:attr(name)
	 */
	int node_attr( lua_State *L );


	/**
	 * @brief Find nodes using selector.
	 *
	 * Lua:
	 * node:find("div a")
	 */
	int node_find( lua_State *L );


	/**
	 * @brief Create node metatable.
	 */
	void CreateNodeMeta( lua_State *L );

} // namespace html
