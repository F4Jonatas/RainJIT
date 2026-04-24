/**
 * @file html_selector.hpp
 * @brief CSS-lite selector parsing and matching.
 */

#pragma once

#include <gumbo.h>
#include <string>
#include <vector>

namespace html {

	/* ============================================================
		ENUMS
	============================================================ */

	typedef enum AttrOperator {

		ATTR_EXISTS,
		ATTR_EQUALS,
		ATTR_STARTS,
		ATTR_ENDS,
		ATTR_CONTAINS

	} AttrOperator;



	typedef enum HtmlPseudoType {

		PSEUDO_NONE,

		PSEUDO_FIRST_CHILD,

		PSEUDO_LAST_CHILD,

		PSEUDO_EMPTY

	} HtmlPseudoType;



	/* ============================================================
		STRUCTS
	============================================================ */

	typedef struct HtmlAttributeSelector {

		std::string name;

		std::string value;

		AttrOperator op;

		bool enabled;

	} HtmlAttributeSelector;



	typedef struct HtmlSelector {

		std::string tag;

		std::string id;

		std::vector<std::string> classes;

		HtmlAttributeSelector attribute;

		HtmlPseudoType pseudo;

		bool matchAll;

	} HtmlSelector;



	typedef struct SelectorStep {

		HtmlSelector selector;

		bool directChild;

	} SelectorStep;



	typedef struct SelectorChain {

		std::vector<SelectorStep> steps;

	} SelectorChain;



	typedef struct SelectorGroup {

		std::vector<SelectorChain> chains;

	} SelectorGroup;



	/* ============================================================
		PARSER
	============================================================ */

	bool ParseSimpleSelector( const char *selector, HtmlSelector &out );


	bool ParseSelectorChain( const char *selector, SelectorChain &out );


	bool ParseSelectorGroup( const char *selector, SelectorGroup &out );



	/* ============================================================
		MATCHING
	============================================================ */

	bool MatchSelector( GumboNode *node, const HtmlSelector &sel );



	void FindNodesChain( GumboNode *root, const SelectorChain &chain, std::vector<GumboNode *> &out );


	void FindNodesGroup( GumboNode *root, const SelectorGroup &group, std::vector<GumboNode *> &out );

} // namespace html
