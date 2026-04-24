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
	 */
	typedef struct HtmlNodeList {

		std::vector<GumboNode *> nodes;

		HtmlDocument *owner;

	} HtmlNodeList;


	void PushNodeList( lua_State *L, HtmlDocument *doc, const std::vector<GumboNode *> &nodes );


	HtmlNodeList *CheckNodeList( lua_State *L, int index );


	void CreateNodeListMeta( lua_State *L );

} // namespace html
