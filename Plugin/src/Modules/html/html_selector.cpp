/**
 * @file html_selector.cpp
 * @brief CSS-lite selector implementation.
 */

#include "html_selector.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace html {

	/* ============================================================
		UTILS
	============================================================ */

	static bool MatchClassToken( const char *classList, const std::string &className ) {

		const char *p = classList;

		size_t len = className.length();

		while ( *p ) {

			while ( *p == ' ' )
				p++;

			if ( !*p )
				break;

			const char *start = p;

			while ( *p && *p != ' ' )
				p++;

			size_t tokenLen = p - start;

			if ( tokenLen == len && strncmp( start, className.c_str(), len ) == 0 ) {

				return true;
			}
		}

		return false;
	}



	/* ============================================================
		PARSE SIMPLE
	============================================================ */

	bool ParseSimpleSelector( const char *selector, HtmlSelector &out ) {

		out.tag.clear();
		out.id.clear();
		out.classes.clear();

		out.attribute.enabled = false;
		out.attribute.name.clear();
		out.attribute.value.clear();
		out.attribute.op = ATTR_EXISTS;

		out.pseudo = PSEUDO_NONE;
		out.matchAll = false;

		if ( !selector || !selector[0] )
			return false;

		const char *p = selector;

		if ( *p == '*' ) {

			out.matchAll = true;
			p++;
		}

		while ( *p ) {

			if ( *p == '#' ) {

				p++;

				while ( *p && *p != '.' && *p != '[' && *p != ':' ) {

					out.id += *p++;
				}
			}

			else if ( *p == '.' ) {

				p++;

				std::string cls;

				while ( *p && *p != '.' && *p != '#' && *p != '[' && *p != ':' ) {

					cls += *p++;
				}

				if ( !cls.empty() )
					out.classes.push_back( cls );
			}

			else if ( *p == '[' ) {
				p++;
				out.attribute.enabled = true;
				out.attribute.op = ATTR_EXISTS;

				// Pula espaços iniciais
				while ( *p == ' ' )
					p++;

				// Lê nome do atributo
				while ( *p && *p != '=' && *p != ']' && *p != '^' && *p != '$' && *p != '*' && *p != ' ' )
					out.attribute.name += *p++;

				// Remove espaços depois do nome
				while ( *p == ' ' )
					p++;

				// Verifica operador
				if ( *p == '^' && *( p + 1 ) == '=' ) {
					out.attribute.op = ATTR_STARTS;
					p += 2;
				} else if ( *p == '$' && *( p + 1 ) == '=' ) {
					out.attribute.op = ATTR_ENDS;
					p += 2;
				} else if ( *p == '*' && *( p + 1 ) == '=' ) {
					out.attribute.op = ATTR_CONTAINS;
					p += 2;
				} else if ( *p == '=' ) {
					out.attribute.op = ATTR_EQUALS;
					p++;
				} else {
					// Sem operador: apenas verifica existência
					while ( *p && *p != ']' )
						p++;
					if ( *p == ']' )
						p++;
					continue;
				}

				// Pula espaços após operador
				while ( *p == ' ' )
					p++;

				// Lê valor, possivelmente entre aspas
				if ( *p == '"' || *p == '\'' ) {
					char quote = *p++;
					while ( *p && *p != quote )
						out.attribute.value += *p++;
					if ( *p == quote )
						p++;
				} else {
					while ( *p && *p != ']' && *p != ' ' )
						out.attribute.value += *p++;
				}

				// Pula espaços finais e fecha colchete
				while ( *p == ' ' )
					p++;
				if ( *p == ']' )
					p++;
			}

			else if ( *p == ':' ) {

				p++;

				std::string pseudo;

				while ( *p )
					pseudo += *p++;

				if ( pseudo == "first-child" )
					out.pseudo = PSEUDO_FIRST_CHILD;

				else if ( pseudo == "last-child" )
					out.pseudo = PSEUDO_LAST_CHILD;

				else if ( pseudo == "empty" )
					out.pseudo = PSEUDO_EMPTY;
			}

			else {

				while ( *p && *p != '.' && *p != '#' && *p != '[' && *p != ':' ) {

					out.tag += *p++;
				}
			}
		}

		return true;
	}



	/* ============================================================
		PARSE CHAIN
	============================================================ */

	bool ParseSelectorChain( const char *selector, SelectorChain &out ) {

		out.steps.clear();

		if ( !selector )
			return false;

		const char *p = selector;

		bool nextDirect = false;

		while ( *p ) {

			while ( *p == ' ' )
				p++;

			if ( *p == '>' ) {

				nextDirect = true;
				p++;
				continue;
			}

			if ( !*p )
				break;

			const char *start = p;

			while ( *p && *p != ' ' && *p != '>' ) {

				p++;
			}

			std::string part( start, p - start );

			SelectorStep step;

			if ( !ParseSimpleSelector( part.c_str(), step.selector ) )
				return false;

			step.directChild = nextDirect;

			out.steps.push_back( step );

			nextDirect = false;
		}

		return !out.steps.empty();
	}



	/* ============================================================
		PARSE GROUP
	============================================================ */

	bool ParseSelectorGroup( const char *selector, SelectorGroup &out ) {

		out.chains.clear();

		if ( !selector )
			return false;

		const char *p = selector;

		while ( *p ) {

			while ( *p == ' ' )
				p++;

			const char *start = p;

			while ( *p && *p != ',' )
				p++;

			std::string part( start, p - start );

			SelectorChain chain;

			if ( !ParseSelectorChain( part.c_str(), chain ) )
				return false;

			out.chains.push_back( chain );

			if ( *p == ',' )
				p++;
		}

		return !out.chains.empty();
	}



	/* ============================================================
		MATCH / FIND (mantém compatível)
	============================================================ */

	/**
	 * @brief Match selector against node.
	 */
	bool MatchSelector( GumboNode *node, const HtmlSelector &sel ) {

		if ( !node || node->type != GUMBO_NODE_ELEMENT )
			return false;

		GumboElement *el = &node->v.element;



		/* ============================================================
			TAG
		============================================================ */

		if ( !sel.matchAll && !sel.tag.empty() ) {

			const char *tag = gumbo_normalized_tagname( el->tag );

			if ( sel.tag != tag )
				return false;
		}



		/* ============================================================
			ID
		============================================================ */

		if ( !sel.id.empty() ) {

			GumboAttribute *attr = gumbo_get_attribute( &el->attributes, "id" );

			if ( !attr || sel.id != attr->value )
				return false;
		}



		/* ============================================================
			CLASSES
		============================================================ */

		if ( !sel.classes.empty() ) {

			GumboAttribute *attr = gumbo_get_attribute( &el->attributes, "class" );

			if ( !attr )
				return false;

			const char *classList = attr->value;

			for ( size_t i = 0; i < sel.classes.size(); ++i ) {

				if ( !MatchClassToken( classList, sel.classes[i] ) )
					return false;
			}
		}



		/* ============================================================
			ATTRIBUTE
		============================================================ */

		if ( sel.attribute.enabled ) {

			GumboAttribute *attr = gumbo_get_attribute( &el->attributes, sel.attribute.name.c_str() );

			if ( !attr )
				return false;

			const char *value = attr->value;

			const std::string &test = sel.attribute.value;

			switch ( sel.attribute.op ) {

			case ATTR_EXISTS:
				break;



			case ATTR_EQUALS:

				if ( test != value )
					return false;

				break;



			case ATTR_STARTS:

				if ( strncmp( value, test.c_str(), test.length() ) != 0 )
					return false;

				break;



			case ATTR_ENDS: {

				size_t len = strlen( value );

				size_t tlen = test.length();

				if ( len < tlen || strncmp( value + len - tlen, test.c_str(), tlen ) != 0 )
					return false;

				break;
			}



			case ATTR_CONTAINS:

				if ( !strstr( value, test.c_str() ) )
					return false;

				break;
			}
		}



		/* ============================================================
			PSEUDO
		============================================================ */

		switch ( sel.pseudo ) {

		case PSEUDO_NONE:
			break;



		case PSEUDO_FIRST_CHILD: {

			GumboNode *parent = node->parent;

			if ( !parent || parent->type != GUMBO_NODE_ELEMENT )
				return false;

			GumboVector *children = &parent->v.element.children;

			for ( unsigned int i = 0; i < children->length; ++i ) {

				GumboNode *child = (GumboNode *)children->data[i];

				if ( child->type == GUMBO_NODE_ELEMENT ) {

					if ( child != node )
						return false;

					break;
				}
			}

			break;
		}



		case PSEUDO_LAST_CHILD: {

			GumboNode *parent = node->parent;

			if ( !parent || parent->type != GUMBO_NODE_ELEMENT )
				return false;

			GumboVector *children = &parent->v.element.children;

			for ( int i = (int)children->length - 1; i >= 0; --i ) {

				GumboNode *child = (GumboNode *)children->data[i];

				if ( child->type == GUMBO_NODE_ELEMENT ) {

					if ( child != node )
						return false;

					break;
				}
			}

			break;
		}



		case PSEUDO_EMPTY: {

			GumboVector *children = &el->children;

			for ( unsigned int i = 0; i < children->length; ++i ) {

				GumboNode *child = (GumboNode *)children->data[i];

				if ( child->type == GUMBO_NODE_ELEMENT || child->type == GUMBO_NODE_TEXT )
					return false;
			}

			break;
		}
		}



		return true;
	}



	static void FindNodesChainInternal( GumboNode *node, const SelectorChain &chain, size_t stepIndex, std::vector<GumboNode *> &out ) {

		if ( !node || node->type != GUMBO_NODE_ELEMENT )
			return;

		const SelectorStep &step = chain.steps[stepIndex];

		bool matched = MatchSelector( node, step.selector );



		/* STEP MATCHED */

		if ( matched ) {

			/* LAST STEP */

			if ( stepIndex + 1 == chain.steps.size() ) {

				out.push_back( node );
			} else {

				/* Continue next step */

				GumboVector *children = &node->v.element.children;

				for ( unsigned int i = 0; i < children->length; ++i ) {

					GumboNode *child = (GumboNode *)children->data[i];

					FindNodesChainInternal( child, chain, stepIndex + 1, out );
				}
			}
		}



		/* CONTINUE SEARCH */

		if ( !step.directChild ) {

			GumboVector *children = &node->v.element.children;

			for ( unsigned int i = 0; i < children->length; ++i ) {

				GumboNode *child = (GumboNode *)children->data[i];

				FindNodesChainInternal( child, chain, stepIndex, out );
			}
		}
	}



	/* ============================================================
		PUBLIC FIND CHAIN
	============================================================ */

	void FindNodesChain( GumboNode *root, const SelectorChain &chain, std::vector<GumboNode *> &out ) {

		if ( !root || chain.steps.empty() )
			return;

		FindNodesChainInternal( root, chain, 0, out );
	}



	/* ============================================================
		PUBLIC FIND GROUP
	============================================================ */

	void FindNodesGroup( GumboNode *root, const SelectorGroup &group, std::vector<GumboNode *> &out ) {
		if ( !root || group.chains.empty() )
			return;

		std::vector<GumboNode *> temp;
		for ( size_t i = 0; i < group.chains.size(); ++i ) {
			FindNodesChain( root, group.chains[i], temp );
		}

		// Deduplica usando um conjunto de endereços
		std::unordered_set<GumboNode *> seen;
		for ( GumboNode *node : temp ) {
			if ( seen.find( node ) == seen.end() ) {
				seen.insert( node );
				out.push_back( node );
			}
		}
	}

} // namespace html
