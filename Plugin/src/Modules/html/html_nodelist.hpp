/**
 * @file html_nodelist.hpp
 * @brief HtmlNodeList userdata definitions.
 */

#pragma once

#include <gumbo.h>
#include <lua.hpp>
#include <vector>

namespace html {

	typedef struct HtmlDocument HtmlDocument;

	/**
	 * @brief Represents a list of HTML nodes.
	 *
	 * @note docRef is a Lua registry reference to the owning HtmlDocument
	 *       userdata, preventing premature GC of the document while this
	 *       list is alive.
	 */
	typedef struct HtmlNodeList {

		std::vector<GumboNode *> nodes;

		HtmlDocument *owner;

		/// @brief Lua registry reference to the owning HtmlDocument userdata.
		int docRef;

	} HtmlNodeList;


	/**
	 * @brief Push nodelist userdata onto the Lua stack.
	 *
	 * @param docRef Lua registry reference to the owning HtmlDocument.
	 *               The list takes ownership of this ref and releases it in __gc.
	 */
	void PushNodeList( lua_State *L, HtmlDocument *doc, const std::vector<GumboNode *> &nodes, int docRef );


	HtmlNodeList *CheckNodeList( lua_State *L, int index );


	void CreateNodeListMeta( lua_State *L );

} // namespace html
