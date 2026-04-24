/**
 * @file xml.cpp
 * @brief Implementation of the xml Lua module for Rainmeter.
 * @license GPL v2.0 License
 */

#include "xml.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

using namespace xml;

// ---------------------------------------------------------------------------
// Metatable name constants
// ---------------------------------------------------------------------------

static constexpr const char *MT_DOCUMENT = "xml.document";
static constexpr const char *MT_NODE = "xml.node";
static constexpr const char *MT_NODESET = "xml.nodeSet";

// ---------------------------------------------------------------------------
// FixVoidTags
// ---------------------------------------------------------------------------

/**
 * @brief Fix HTML5 void elements so they are valid self-closing XHTML tags.
 *
 * pugixml is a strict XML parser. HTML5 void elements (`<img>`, `<br>`,
 * `<input>`, etc.) are written without a closing slash in standard HTML,
 * which makes pugixml either reject the document or silently treat the next
 * sibling as a child. This helper adds the required `/>` before the closing
 * `>` of each void tag.
 *
 * ### Algorithm
 * Scans the input byte-by-byte (no regex, no heap allocation per tag):
 *  1. Text before `<` is copied verbatim.
 *  2. Non-opening tokens (`</`, `<!--`, `<?`) are copied verbatim.
 *  3. For opening tags the tag name is read and lowercased for lookup.
 *  4. If the tag name is a known HTML5 void element and the tag does **not**
 *     already end with `/>`, the trailing `>` is replaced by `/>`.
 *
 * ### Handled void elements
 * `area`, `base`, `br`, `col`, `embed`, `hr`, `img`, `input`,
 * `link`, `meta`, `param`, `source`, `track`, `wbr`
 *
 * ### Edge cases
 * - Already self-closed tags (`<br/>`, `<br />`) are left untouched.
 * - Malformed input with no closing `>` is copied verbatim from the `<`.
 * - Attributes containing `>` inside quoted values may confuse the scanner
 *   (rare in sanitised feed content).
 *
 * @param[in]  html  Raw HTML string (UTF-8).
 * @return           Fixed HTML string with self-closed void elements.
 *
 * @note This is an internal helper; not exposed to Lua.
 */
static std::string FixVoidTags( const std::string &html ) {
	// Static set — built once, shared across all calls.
	// clang-format off
	static const std::unordered_set<std::string> kVoidTags = {
		"area", "base", "br",   "col",   "embed", "hr",   "img",
		"input","link", "meta", "param", "source","track","wbr"
	};
	// clang-format on

	std::string result;
	result.reserve( html.size() + 32 );

	const size_t len = html.size();
	size_t pos = 0;

	while ( pos < len ) {
		// Find next '<'
		const size_t lt = html.find( '<', pos );
		if ( lt == std::string::npos ) {
			result.append( html, pos, len - pos );
			break;
		}

		// Copy text before '<'
		result.append( html, pos, lt - pos );

		const size_t after_lt = lt + 1;
		if ( after_lt >= len ) {
			result += '<';
			break;
		}

		const char next = html[after_lt];

		// Not an opening element tag: </  <!  <?
		// Copy '<' and let the outer loop advance past it naturally.
		if ( next == '/' || next == '!' || next == '?' ) {
			result += '<';
			pos = after_lt;
			continue;
		}

		// Read the tag name: stops at whitespace, '/', or '>'
		size_t i = after_lt;
		while ( i < len && html[i] != '>' && html[i] != '/' && !std::isspace( static_cast<unsigned char>( html[i] ) ) ) {
			++i;
		}

		// Build lowercase tag name for the set lookup
		std::string tag_name( html, after_lt, i - after_lt );
		for ( char &c : tag_name )
			c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

		// Locate the closing '>' of this tag
		const size_t gt = html.find( '>', i );
		if ( gt == std::string::npos ) {
			// Unclosed/malformed tag — copy remainder as-is
			result.append( html, lt, len - lt );
			pos = len;
			break;
		}

		const bool is_void = kVoidTags.count( tag_name ) > 0;
		const bool already_self_closed = ( gt > 0 && html[gt - 1] == '/' );

		if ( is_void && !already_self_closed ) {
			// Copy from '<' up to (not including) '>', then append '/>'
			result.append( html, lt, gt - lt );
			result += "/>";
		} else {
			// Copy verbatim including '>'
			result.append( html, lt, gt - lt + 1 );
		}

		pos = gt + 1;
	}

	return result;
}

// ---------------------------------------------------------------------------
// node_type_string
// ---------------------------------------------------------------------------

/**
 * @brief Convert a `pugi::xml_node_type` enum value to a Lua-friendly string.
 *
 * @param[in] type  pugixml node type.
 * @return          Human-readable string.
 */
static std::string node_type_string( pugi::xml_node_type type ) {
	switch ( type ) {
	case pugi::node_null:
		return "null";
	case pugi::node_document:
		return "document";
	case pugi::node_element:
		return "element";
	case pugi::node_pcdata:
		return "text";
	case pugi::node_cdata:
		return "cdata";
	case pugi::node_comment:
		return "comment";
	case pugi::node_pi:
		return "pi";
	case pugi::node_declaration:
		return "declaration";
	case pugi::node_doctype:
		return "doctype";
	default:
		return "unknown";
	}
}

// ---------------------------------------------------------------------------
// Forward declarations of push helpers (defined after Lua bindings)
// ---------------------------------------------------------------------------

static void push_document( lua_State *L, std::shared_ptr<Document> doc );
static void push_node( lua_State *L, const Node &node );
static void push_nodeset( lua_State *L, const NodeSet &set );

// ---------------------------------------------------------------------------
// Type-checked userdata accessors
//
// These use luaL_checkudata, which looks up the metatable name in the Lua
// registry. This only works correctly after luaL_newmetatable has been called
// for that name (done once inside luaopen_xml → register_metatables).
// ---------------------------------------------------------------------------

/**
 * @brief Extract and validate a `xml.document` userdata from the Lua stack.
 * @param[in] L    Lua state.
 * @param[in] idx  Stack index.
 * @return         Pointer to the `shared_ptr<Document>` stored in userdata.
 * @throws         Lua error if the value is not a `xml.document`.
 */
static std::shared_ptr<Document> *check_document( lua_State *L, int idx ) {
	return static_cast<std::shared_ptr<Document> *>( luaL_checkudata( L, idx, MT_DOCUMENT ) );
}

/**
 * @brief Extract and validate a `xml.node` userdata from the Lua stack.
 * @param[in] L    Lua state.
 * @param[in] idx  Stack index.
 * @return         Pointer to the `Node` stored in userdata.
 * @throws         Lua error if the value is not a `xml.node`.
 */
static Node *check_node( lua_State *L, int idx ) {
	return static_cast<Node *>( luaL_checkudata( L, idx, MT_NODE ) );
}

/**
 * @brief Extract and validate a `xml.nodeSet` userdata from the Lua stack.
 * @param[in] L    Lua state.
 * @param[in] idx  Stack index.
 * @return         Pointer to the `NodeSet` stored in userdata.
 * @throws         Lua error if the value is not a `xml.nodeSet`.
 */
static NodeSet *check_nodeset( lua_State *L, int idx ) {
	return static_cast<NodeSet *>( luaL_checkudata( L, idx, MT_NODESET ) );
}

/**
 * @brief Read a `ParseOptions` struct from a Lua table at the given stack index.
 *
 * If the value at `idx` is not a table (e.g. absent or `nil`), all options
 * remain at their defaults.
 *
 * @param[in] L    Lua state.
 * @param[in] idx  Stack index of the options table (may be absent).
 * @return         Populated `ParseOptions`.
 */
static ParseOptions parse_options_from_table( lua_State *L, int idx ) {
	ParseOptions opts;
	if ( lua_istable( L, idx ) ) {
		lua_getfield( L, idx, "cache" );
		if ( lua_isboolean( L, -1 ) )
			opts.cache = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		lua_getfield( L, idx, "parse_full" );
		if ( lua_isboolean( L, -1 ) )
			opts.parse_full = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}
	return opts;
}

// ---------------------------------------------------------------------------
// Document implementation
// ---------------------------------------------------------------------------

Document::Document() :
	doc( std::make_unique<pugi::xml_document>() ) {
}

Document::Document( pugi::xml_document &&d ) :
	doc( std::make_unique<pugi::xml_document>( std::move( d ) ) ) {
}

Document::Document( Document &&other ) noexcept :
	doc( std::move( other.doc ) ),
	xpath_cache( std::move( other.xpath_cache ) ),
	cache_enabled( other.cache_enabled )
// cache_mutex is default-constructed; std::mutex is not movable.
{
}

Document &Document::operator=( Document &&other ) noexcept {
	if ( this != &other ) {
		doc = std::move( other.doc );
		xpath_cache = std::move( other.xpath_cache );
		cache_enabled = other.cache_enabled;
	}
	return *this;
}

Node Document::root() const {
	return Node( doc->document_element(), const_cast<Document *>( this )->shared_from_this() );
}

NodeSet Document::select( const std::string &xpath ) {
	pugi::xpath_node_set set;
	if ( cache_enabled ) {
		std::lock_guard<std::mutex> lock( cache_mutex );
		auto it = xpath_cache.find( xpath );
		if ( it == xpath_cache.end() ) {
			pugi::xpath_query query( xpath.c_str() );
			if ( !query )
				return NodeSet();
			it = xpath_cache.emplace( xpath, std::move( query ) ).first;
		}
		set = it->second.evaluate_node_set( *doc );
	} else {
		set = doc->select_nodes( xpath.c_str() );
	}
	return NodeSet( set, shared_from_this() );
}

Node Document::select_single( const std::string &xpath ) {
	pugi::xpath_node xn;
	if ( cache_enabled ) {
		std::lock_guard<std::mutex> lock( cache_mutex );
		auto it = xpath_cache.find( xpath );
		if ( it == xpath_cache.end() ) {
			pugi::xpath_query query( xpath.c_str() );
			if ( !query )
				return Node();
			it = xpath_cache.emplace( xpath, std::move( query ) ).first;
		}
		xn = it->second.evaluate_node( *doc );
	} else {
		xn = doc->select_node( xpath.c_str() );
	}
	return Node( xn.node(), shared_from_this() );
}

// ---------------------------------------------------------------------------
// Node implementation
// ---------------------------------------------------------------------------

std::string Node::type() const {
	if ( doc.expired() )
		return "null";
	return node_type_string( node.type() );
}

std::string Node::name() const {
	return node.name();
}
std::string Node::value() const {
	return node.value();
}

std::string Node::text() const {
	if ( !valid() )
		return "";
	return node.text().as_string();
}

std::string Node::attribute( const std::string &name ) const {
	if ( !valid() )
		return "";
	pugi::xml_attribute attr = node.attribute( name.c_str() );
	return attr ? attr.value() : "";
}

std::vector<std::pair<std::string, std::string>> Node::attributes() const {
	std::vector<std::pair<std::string, std::string>> result;
	if ( !valid() )
		return result;
	for ( pugi::xml_attribute a = node.first_attribute(); a; a = a.next_attribute() )
		result.emplace_back( a.name(), a.value() );
	return result;
}

Node Node::child( const std::string &name ) const {
	auto d = doc.lock();
	if ( !d )
		return Node();
	return Node( node.child( name.c_str() ), d );
}

std::vector<Node> Node::children( const std::string &name ) const {
	std::vector<Node> result;
	auto d = doc.lock();
	if ( !d )
		return result;

	if ( name.empty() ) {
		for ( pugi::xml_node c = node.first_child(); c; c = c.next_sibling() )
			result.emplace_back( c, d );
	} else {
		for ( pugi::xml_node c = node.child( name.c_str() ); c; c = c.next_sibling( name.c_str() ) )
			result.emplace_back( c, d );
	}
	return result;
}

NodeSet Node::select( const std::string &xpath ) const {
	auto d = doc.lock();
	if ( !d )
		return NodeSet();
	return NodeSet( node.select_nodes( xpath.c_str() ), d );
}

Node Node::select_single( const std::string &xpath ) const {
	auto d = doc.lock();
	if ( !d )
		return Node();
	pugi::xpath_node xn = node.select_node( xpath.c_str() );
	return Node( xn.node(), d );
}

std::string Node::inner_xml() const {
	if ( !valid() )
		return "";
	std::ostringstream oss;
	for ( pugi::xml_node c = node.first_child(); c; c = c.next_sibling() )
		c.print( oss, "", pugi::format_raw );
	return oss.str();
}

std::string Node::outer_xml() const {
	if ( !valid() )
		return "";
	std::ostringstream oss;
	node.print( oss, "", pugi::format_raw );
	return oss.str();
}

// ---------------------------------------------------------------------------
// NodeSet implementation
// ---------------------------------------------------------------------------

NodeSet::NodeSet( pugi::xpath_node_set set, std::shared_ptr<Document> d ) :
	doc( std::move( d ) ) {
	for ( const auto &xn : set ) {
		if ( xn.node() )
			nodes.emplace_back( xn.node(), doc );
	}
}

Node NodeSet::get( int index ) const {
	if ( index < 1 || static_cast<size_t>( index ) > nodes.size() )
		return Node();
	return nodes[index - 1];
}

// ---------------------------------------------------------------------------
// Lua bindings — module functions
// ---------------------------------------------------------------------------

/**
 * @brief Lua binding: `xml.parse(str [, options])` → `doc` | `nil, errmsg`
 *
 * Parses `str` as a strict XML document using `pugi::parse_default` (or
 * `pugi::parse_full` if `options.parse_full` is true).
 *
 * @par Stack
 * - `[1]` string  — XML source text (required).
 * - `[2]` table   — ParseOptions (optional).
 *
 * @return 1 (`xml.document` userdata) on success.
 * @return 2 (`nil`, error string) on parse failure.
 */
static int l_parse( lua_State *L ) {
	size_t len;
	const char *str = luaL_checklstring( L, 1, &len );
	ParseOptions opts = parse_options_from_table( L, 2 );

	unsigned int flags = opts.parse_full ? pugi::parse_full : pugi::parse_default;

	auto doc = std::make_shared<Document>();
	pugi::xml_parse_result result = doc->doc->load_buffer( str, len, flags );
	if ( !result ) {
		lua_pushnil( L );
		lua_pushfstring( L, "XML parse error at offset %d: %s", static_cast<int>( result.offset ), result.description() );
		return 2;
	}

	doc->set_cache_enabled( opts.cache );
	push_document( L, std::move( doc ) );
	return 1;
}

/**
 * @brief Lua binding: `xml.parse_html(str [, options])` → `doc` | `nil, errmsg`
 *
 * Parses `str` as an HTML document using tolerant pugixml flags:
 * `parse_default | parse_embed_pcdata | parse_ws_pcdata`.
 * These flags allow looser structure (e.g. mixed text and element children)
 * without requiring the strict single-root rule of XML.
 *
 * For **fragments** (snippets extracted from a node via `node:html()`),
 * use `node:html()` instead — it additionally applies `parse_fragment`
 * and fixes HTML5 void tags.
 *
 * @par Stack
 * - `[1]` string  — HTML source text (required).
 * - `[2]` table   — ParseOptions (optional).
 *
 * @return 1 (`xml.document` userdata) on success.
 * @return 2 (`nil`, error string) on parse failure.
 */
static int l_parse_html( lua_State *L ) {
	size_t len;
	const char *str = luaL_checklstring( L, 1, &len );
	ParseOptions opts = parse_options_from_table( L, 2 );

	unsigned int flags = pugi::parse_default | pugi::parse_embed_pcdata | pugi::parse_ws_pcdata;
	if ( opts.parse_full )
		flags |= pugi::parse_full;

	auto doc = std::make_shared<Document>();
	pugi::xml_parse_result result = doc->doc->load_buffer( str, len, flags );
	if ( !result ) {
		lua_pushnil( L );
		lua_pushfstring( L, "HTML parse error at offset %d: %s", static_cast<int>( result.offset ), result.description() );
		return 2;
	}

	doc->set_cache_enabled( opts.cache );
	push_document( L, std::move( doc ) );
	return 1;
}

// ---------------------------------------------------------------------------
// Lua bindings — Document methods
// ---------------------------------------------------------------------------

/// @brief Lua: `doc:root()` → Node
static int doc_root( lua_State *L ) {
	push_node( L, ( *check_document( L, 1 ) )->root() );
	return 1;
}

/// @brief Lua: `doc:select(xpath)` → NodeSet
static int doc_select( lua_State *L ) {
	const char *xpath = luaL_checkstring( L, 2 );
	push_nodeset( L, ( *check_document( L, 1 ) )->select( xpath ) );
	return 1;
}

/// @brief Lua: `doc:select_single(xpath)` → Node
static int doc_select_single( lua_State *L ) {
	const char *xpath = luaL_checkstring( L, 2 );
	push_node( L, ( *check_document( L, 1 ) )->select_single( xpath ) );
	return 1;
}

// ---------------------------------------------------------------------------
// Lua bindings — Node methods
// ---------------------------------------------------------------------------

/// @brief Lua: `node:type()` → string
static int node_type( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->type().c_str() );
	return 1;
}

/// @brief Lua: `node:name()` → string
static int node_name( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->name().c_str() );
	return 1;
}

/// @brief Lua: `node:value()` → string
static int node_value( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->value().c_str() );
	return 1;
}

/// @brief Lua: `node:text()` → string
static int node_text( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->text().c_str() );
	return 1;
}

/// @brief Lua: `node:attribute(name)` → string
static int node_attribute( lua_State *L ) {
	const char *name = luaL_checkstring( L, 2 );
	lua_pushstring( L, check_node( L, 1 )->attribute( name ).c_str() );
	return 1;
}

/// @brief Lua: `node:attributes()` → table {name=value, …}
static int node_attributes( lua_State *L ) {
	auto pairs = check_node( L, 1 )->attributes();
	lua_createtable( L, 0, static_cast<int>( pairs.size() ) );
	for ( const auto &kv : pairs ) {
		lua_pushstring( L, kv.second.c_str() );
		lua_setfield( L, -2, kv.first.c_str() );
	}
	return 1;
}

/// @brief Lua: `node:child(name)` → Node
static int node_child( lua_State *L ) {
	const char *name = luaL_checkstring( L, 2 );
	push_node( L, check_node( L, 1 )->child( name ) );
	return 1;
}

/// @brief Lua: `node:children([name])` → table of Node
static int node_children( lua_State *L ) {
	const char *name = lua_isstring( L, 2 ) ? lua_tostring( L, 2 ) : "";
	std::vector<Node> kids = check_node( L, 1 )->children( name );

	lua_createtable( L, static_cast<int>( kids.size() ), 0 );
	for ( int i = 0; i < static_cast<int>( kids.size() ); ++i ) {
		push_node( L, kids[i] );
		lua_rawseti( L, -2, i + 1 );
	}
	return 1;
}

/// @brief Lua: `node:select(xpath)` → NodeSet
static int node_select( lua_State *L ) {
	const char *xpath = luaL_checkstring( L, 2 );
	push_nodeset( L, check_node( L, 1 )->select( xpath ) );
	return 1;
}

/// @brief Lua: `node:select_single(xpath)` → Node
static int node_select_single( lua_State *L ) {
	const char *xpath = luaL_checkstring( L, 2 );
	push_node( L, check_node( L, 1 )->select_single( xpath ) );
	return 1;
}

/// @brief Lua: `node:inner_xml()` → string
static int node_inner_xml( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->inner_xml().c_str() );
	return 1;
}

/// @brief Lua: `node:outer_xml()` → string
static int node_outer_xml( lua_State *L ) {
	lua_pushstring( L, check_node( L, 1 )->outer_xml().c_str() );
	return 1;
}

/**
 * @brief Lua: `node:html()` → `doc` | `nil, errmsg`
 *
 * Extracts the HTML content stored in this node, repairs HTML5 void tags,
 * and parses the result as a new `xml.document`. The returned document is
 * fully independent and can be queried with all Document/Node/NodeSet methods.
 *
 * ### Content extraction strategy
 * The method automatically detects how the HTML is stored in the node:
 *
 * | Node content             | Strategy used   | Rationale                          |
 * |--------------------------|-----------------|------------------------------------|
 * | CDATA or text only       | `node:text()`   | Decodes the CDATA/text characters  |
 * | Element children         | `node:inner_xml()` | Preserves child markup          |
 *
 * This covers both RSS feeds (where HTML lives in a CDATA section inside
 * `<description>`) and regular XML nodes that contain child HTML elements.
 *
 * ### Void tag repair
 * HTML5 void elements (`<img>`, `<br>`, `<input>`, etc.) are not
 * self-closing in XML. Before parsing, `FixVoidTags()` rewrites them to
 * `<img … />` form so pugixml accepts them.
 *
 * ### Parsing flags
 * Uses `pugi::parse_fragment | parse_embed_pcdata | parse_ws_pcdata`,
 * which allows the snippet to have multiple root elements and tolerates
 * mixed content models common in HTML.
 *
 * @par Stack
 * - `[1]` xml.node  — the node whose content should be parsed.
 *
 * @return 1 (`xml.document` userdata) on success.
 * @return 2 (`nil`, error string) on invalid node or parse failure.
 *
 * @par Lua — RSS CDATA description
 * @code{.lua}
 * local desc = item:select_single("description")
 * local hd, err = desc:html()
 * if not hd then print("html parse error:", err) return end
 *
 * local img = hd:select_single("//img")
 * print(img and img:attribute("src") or "no image")
 * @endcode
 *
 * @par Lua — XML node with element children
 * @code{.lua}
 * local wrapper = doc:select_single('//*[@class="content"]')
 * local hd, err = wrapper:html()
 * if not hd then print(err) return end
 *
 * local snippet = hd:select_single("//*[contains(@class,'snippet')]")
 * print(snippet and snippet:text() or "")
 * @endcode
 */
static int node_html( lua_State *L ) {
	Node *node = check_node( L, 1 );
	if ( !node->valid() ) {
		lua_pushnil( L );
		lua_pushstring( L, "node:html() — node is invalid or document has been collected" );
		return 2;
	}

	// --- Detect content type and extract the raw HTML string ---
	//
	// If the node's first child is a CDATA or plain-text node, the HTML is
	// stored as character data (typical for RSS <description> with CDATA).
	// node::text() decodes it cleanly.
	//
	// If the first child is an element, the node contains actual child markup.
	// inner_xml() serialises it back to a string so we can re-parse it.

	bool has_element_child = false;
	for ( pugi::xml_node c = node->node.first_child(); c; c = c.next_sibling() ) {
		if ( c.type() == pugi::node_element ) {
			has_element_child = true;
			break;
		}
	}

	std::string html_content = has_element_child ? node->inner_xml() : node->text();

	if ( html_content.empty() ) {
		lua_pushnil( L );
		lua_pushstring( L, "node:html() — node has no extractable content" );
		return 2;
	}

	// --- Fix HTML5 void elements to be XHTML self-closing ---
	std::string fixed_html = FixVoidTags( html_content );

	// --- Parse as a document fragment ---
	// parse_fragment: allows multiple root-level elements (common in HTML snippets).
	// parse_embed_pcdata + parse_ws_pcdata: tolerate mixed content.
	auto new_doc = std::make_shared<Document>();
	const unsigned int flags = pugi::parse_fragment | pugi::parse_embed_pcdata | pugi::parse_ws_pcdata;

	pugi::xml_parse_result result = new_doc->doc->load_string( fixed_html.c_str(), flags );

	if ( !result ) {
		lua_pushnil( L );
		lua_pushfstring( L, "node:html() — parse error at offset %d: %s", static_cast<int>( result.offset ), result.description() );
		return 2;
	}

	// Inherit the XPath cache setting from the parent document.
	if ( auto parent = node->doc.lock() )
		new_doc->set_cache_enabled( parent->is_cache_enabled() );

	push_document( L, std::move( new_doc ) );
	return 1;
}

// ---------------------------------------------------------------------------
// Lua bindings — NodeSet methods
// ---------------------------------------------------------------------------

/// @brief Lua: `set:size()` → integer
static int nodeset_size( lua_State *L ) {
	lua_pushinteger( L, static_cast<lua_Integer>( check_nodeset( L, 1 )->size() ) );
	return 1;
}

/// @brief Lua: `set:get(i)` → Node  (1-based)
static int nodeset_get( lua_State *L ) {
	int idx = static_cast<int>( luaL_checkinteger( L, 2 ) );
	push_node( L, check_nodeset( L, 1 )->get( idx ) );
	return 1;
}

/**
 * @brief Iterator closure next-function.
 *
 * Upvalue layout:
 *  - `[1]` Lua table of pre-built Node userdata objects.
 *  - `[2]` Current 0-based integer index (incremented each call).
 *
 * Returns the next Node, or nothing to stop iteration.
 */
static int nodeset_iter_next( lua_State *L ) {
	lua_Integer i = lua_tointeger( L, lua_upvalueindex( 2 ) ) + 1;
	lua_pushinteger( L, i );
	lua_replace( L, lua_upvalueindex( 2 ) );

	lua_rawgeti( L, lua_upvalueindex( 1 ), i );
	if ( lua_isnil( L, -1 ) )
		return 0; // stop
	return 1;
}

/**
 * @brief Lua: `set:iter()` → iterator function
 *
 * Returns a stateful closure for use in a generic `for` loop.
 * Builds a temporary Lua table of all Node userdata objects as an upvalue
 * so each iteration step is a simple `rawgeti` with no C++ allocation.
 *
 * @par Lua
 * @code{.lua}
 * for item in doc:select("//item"):iter() do
 *     print(item:select_single("title"):text())
 * end
 * @endcode
 */
static int nodeset_iter( lua_State *L ) {
	const std::vector<Node> &nodes = check_nodeset( L, 1 )->to_vector();

	lua_createtable( L, static_cast<int>( nodes.size() ), 0 );
	for ( int i = 0; i < static_cast<int>( nodes.size() ); ++i ) {
		push_node( L, nodes[i] );
		lua_rawseti( L, -2, i + 1 );
	}

	lua_pushinteger( L, 0 );
	lua_pushcclosure( L, nodeset_iter_next, 2 );
	return 1;
}

// ---------------------------------------------------------------------------
// __gc metamethods
//
// Each calls the C++ destructor in-place via the canonical
// `ptr->~Type()` form (no explicit template args needed).
// ---------------------------------------------------------------------------

static int doc_gc( lua_State *L ) {
	auto *ptr = static_cast<std::shared_ptr<Document> *>( lua_touserdata( L, 1 ) );
	if ( ptr )
		ptr->~shared_ptr();
	return 0;
}

static int node_gc( lua_State *L ) {
	auto *ptr = static_cast<Node *>( lua_touserdata( L, 1 ) );
	if ( ptr )
		ptr->~Node();
	return 0;
}

static int nodeset_gc( lua_State *L ) {
	auto *ptr = static_cast<NodeSet *>( lua_touserdata( L, 1 ) );
	if ( ptr )
		ptr->~NodeSet();
	return 0;
}

// ---------------------------------------------------------------------------
// push_* helpers
//
// Each function:
//  1. Allocates raw Lua userdata of the exact size of the C++ object.
//  2. Constructs the C++ object in-place via placement new.
//  3. Fetches the pre-registered metatable from the Lua registry.
//  4. Sets it on the userdata.
//
// The metatable has __index = itself (set in register_metatables), so
// method calls like `doc:root()` resolve correctly without a wrapper table.
// ---------------------------------------------------------------------------

static void push_document( lua_State *L, std::shared_ptr<Document> doc ) {
	void *ud = lua_newuserdata( L, sizeof( std::shared_ptr<Document> ) );
	new ( ud ) std::shared_ptr<Document>( std::move( doc ) );
	luaL_getmetatable( L, MT_DOCUMENT );
	lua_setmetatable( L, -2 );
}

static void push_node( lua_State *L, const Node &node ) {
	if ( !node.valid() ) {
		lua_pushnil( L );
		return;
	}
	void *ud = lua_newuserdata( L, sizeof( Node ) );
	new ( ud ) Node( node );
	luaL_getmetatable( L, MT_NODE );
	lua_setmetatable( L, -2 );
}

static void push_nodeset( lua_State *L, const NodeSet &set ) {
	void *ud = lua_newuserdata( L, sizeof( NodeSet ) );
	new ( ud ) NodeSet( set );
	luaL_getmetatable( L, MT_NODESET );
	lua_setmetatable( L, -2 );
}

// ---------------------------------------------------------------------------
// Metatable registration
// ---------------------------------------------------------------------------

/**
 * @brief Register the three metatables into the Lua registry.
 *
 * Called once inside `luaopen_xml`, before any `push_*` call.
 *
 * `luaL_newmetatable` creates the metatable **and** stores it in the Lua
 * registry under the given name, which is what enables `luaL_checkudata`
 * to perform type-safe validation. The `if` guard means repeated calls are
 * safe — a name is only registered once per Lua state.
 *
 * Each metatable gets:
 * - `__index = itself`   — enables `obj:method()` syntax.
 * - All public methods.
 * - `__gc`               — calls the C++ destructor in-place.
 */
static void register_metatables( lua_State *L ) {

	// ---- xml.document -------------------------------------------------------
	if ( luaL_newmetatable( L, MT_DOCUMENT ) ) {
		lua_pushvalue( L, -1 );
		lua_setfield( L, -2, "__index" );

		lua_pushcfunction( L, doc_root );
		lua_setfield( L, -2, "root" );
		lua_pushcfunction( L, doc_select );
		lua_setfield( L, -2, "select" );
		lua_pushcfunction( L, doc_select_single );
		lua_setfield( L, -2, "select_single" );
		lua_pushcfunction( L, doc_gc );
		lua_setfield( L, -2, "__gc" );
	}
	lua_pop( L, 1 );

	// ---- xml.node -----------------------------------------------------------
	if ( luaL_newmetatable( L, MT_NODE ) ) {
		lua_pushvalue( L, -1 );
		lua_setfield( L, -2, "__index" );

		lua_pushcfunction( L, node_type );
		lua_setfield( L, -2, "type" );
		lua_pushcfunction( L, node_name );
		lua_setfield( L, -2, "name" );
		lua_pushcfunction( L, node_value );
		lua_setfield( L, -2, "value" );
		lua_pushcfunction( L, node_text );
		lua_setfield( L, -2, "text" );
		lua_pushcfunction( L, node_attribute );
		lua_setfield( L, -2, "attribute" );
		lua_pushcfunction( L, node_attributes );
		lua_setfield( L, -2, "attributes" );
		lua_pushcfunction( L, node_child );
		lua_setfield( L, -2, "child" );
		lua_pushcfunction( L, node_children );
		lua_setfield( L, -2, "children" );
		lua_pushcfunction( L, node_select );
		lua_setfield( L, -2, "select" );
		lua_pushcfunction( L, node_select_single );
		lua_setfield( L, -2, "select_single" );
		lua_pushcfunction( L, node_inner_xml );
		lua_setfield( L, -2, "inner_xml" );
		lua_pushcfunction( L, node_outer_xml );
		lua_setfield( L, -2, "outer_xml" );
		lua_pushcfunction( L, node_html );
		lua_setfield( L, -2, "html" );
		lua_pushcfunction( L, node_gc );
		lua_setfield( L, -2, "__gc" );
	}
	lua_pop( L, 1 );

	// ---- xml.nodeSet --------------------------------------------------------
	if ( luaL_newmetatable( L, MT_NODESET ) ) {
		lua_pushvalue( L, -1 );
		lua_setfield( L, -2, "__index" );

		lua_pushcfunction( L, nodeset_size );
		lua_setfield( L, -2, "size" );
		lua_pushcfunction( L, nodeset_get );
		lua_setfield( L, -2, "get" );
		lua_pushcfunction( L, nodeset_iter );
		lua_setfield( L, -2, "iter" );
		lua_pushcfunction( L, nodeset_gc );
		lua_setfield( L, -2, "__gc" );
	}
	lua_pop( L, 1 );
}

// ---------------------------------------------------------------------------
// Module entry point
// ---------------------------------------------------------------------------

extern "C" int luaopen_xml( lua_State *L ) {
	register_metatables( L ); // must run before any push_* call

	lua_newtable( L );
	lua_pushcfunction( L, l_parse );
	lua_setfield( L, -2, "parse" );
	lua_pushcfunction( L, l_parse_html );
	lua_setfield( L, -2, "parse_html" );
	return 1;
}

void xml::RegisterModule( lua_State *L, Rain * /*rain*/ ) {
	lua_getglobal( L, "package" );
	lua_getfield( L, -1, "preload" );
	lua_pushcfunction( L, luaopen_xml );
	lua_setfield( L, -2, "xml" );
	lua_pop( L, 2 );
}
