/**
 * @file xml.hpp
 * @brief XML/HTML parsing module for Rainmeter (read-only, based on pugixml).
 * @license GPL v2.0 License
 *
 * Provides LuaJIT bindings for fast DOM-tree parsing with full XPath 1.0 support.
 * Internally uses pugixml with UTF-8/UTF-16 conversion via utils/strings.hpp.
 *
 * @module xml
 * @usage local xml = require("xml")
 *
 * @details
 * ### Design principles
 * - **Read-only DOM** — no modification API is exposed.
 * - **UTF-8 throughout** — all Lua strings in/out are UTF-8.
 * - **Automatic lifetime management** — documents, nodes, and node sets are
 *   garbage-collected Lua userdata objects backed by `shared_ptr`/`weak_ptr`.
 *   A Document stays alive as long as any Node or NodeSet derived from it is
 *   reachable in Lua.
 * - **Thread-safe per document** — each Document is fully independent.
 *   The optional XPath cache is protected by an internal mutex.
 * - **Error-safe** — `xml.parse()` and `xml.parse_html()` never throw; they
 *   return `nil, errmsg` on failure.
 *
 * @par Lua quick-start
 * @code{.lua}
 * local xml = require("xml")
 *
 * local doc, err = xml.parse("<root><item id='1'>Hello</item></root>")
 * if not doc then error(err) end
 *
 * local root = doc:root()
 * print(root:name())                              -- "root"
 *
 * for item in doc:select("//item"):iter() do
 *     print(item:attribute("id"), item:text())    -- "1"  "Hello"
 * end
 * @endcode
 *
 * @par RSS feed example
 * @code{.lua}
 * local doc, err = xml.parse(data.text)
 * if not doc then return end
 *
 * local root = doc:root()
 * assert(root:name() == "rss")
 *
 * for item in doc:select("//item"):iter() do
 *     local title   = item:select_single("title"):text()
 *     local creator = item:select_single("*[local-name()='creator']")
 *     print(title, creator and creator:text() or "")
 * end
 * @endcode
 *
 * @par Parsing embedded HTML (e.g. RSS CDATA descriptions)
 * @code{.lua}
 * local desc_node = item:select_single("description")
 *
 * -- :html() extracts the content, fixes HTML5 void tags, and returns
 * -- a new fully-queryable Document:
 * local html_doc, err = desc_node:html()
 * if not html_doc then print("html parse failed:", err) return end
 *
 * local img = html_doc:select_single("//img")
 * if img then
 *     print("image src:", img:attribute("src"))
 * end
 *
 * local snippet = html_doc:select_single("//*[contains(@class,'medium-feed-snippet')]")
 * if snippet then
 *     print("snippet:", snippet:text())
 * end
 * @endcode
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <lua.hpp>
#include <pugixml.hpp>

struct Rain;

namespace xml {

	// ---------------------------------------------------------------------------
	// Forward declarations
	// ---------------------------------------------------------------------------

	struct Document;
	struct Node;
	struct NodeSet;

	// ---------------------------------------------------------------------------
	// ParseOptions
	// ---------------------------------------------------------------------------

	/**
	 * @brief Options controlling how a document is parsed.
	 *
	 * Passed as the optional second argument to `xml.parse()` and
	 * `xml.parse_html()` from Lua as a plain table:
	 *
	 * @code{.lua}
	 * local doc = xml.parse(str, { cache = true, parse_full = false })
	 * @endcode
	 */
	struct ParseOptions {
		/**
		 * @brief Enable the per-document XPath compilation cache.
		 *
		 * When `true`, XPath expressions are compiled once and stored in a
		 * mutex-protected cache inside the Document. Subsequent calls with the
		 * same expression string reuse the compiled query without recompilation.
		 *
		 * Recommended for scripts that run on Rainmeter update cycles and reuse
		 * the same XPath patterns on every tick.
		 *
		 * @type    bool
		 * @default false
		 */
		bool cache = false;

		/**
		 * @brief Use `pugi::parse_full` instead of `pugi::parse_default`.
		 *
		 * `parse_full` additionally preserves processing instructions,
		 * declaration nodes, and doc-type nodes in the DOM tree. Use this
		 * when you need to inspect `<?xml ...?>` or `<!DOCTYPE ...>` nodes.
		 *
		 * @type    bool
		 * @default false
		 */
		bool parse_full = false;
	};

	// ---------------------------------------------------------------------------
	// Document
	// ---------------------------------------------------------------------------

	/**
	 * @brief Owns and manages a parsed DOM tree.
	 *
	 * Wraps a `pugi::xml_document` inside a `shared_ptr`-managed lifetime so
	 * that `Node` objects (which hold `weak_ptr<Document>`) can detect when the
	 * document has been collected and safely return empty values.
	 *
	 * Inherits from `std::enable_shared_from_this` so internal helpers can
	 * obtain a `shared_ptr<Document>` from `this` when constructing Nodes.
	 *
	 * @note Non-copyable. Move-constructible. The internal mutex is not moved —
	 *       a fresh default-constructed mutex is created on every move.
	 *
	 * @par Lua type
	 * `xml.document` userdata. Methods are accessible via `doc:method()`.
	 */
	struct Document : std::enable_shared_from_this<Document> {

		/// @cond INTERNAL
		std::unique_ptr<pugi::xml_document> doc; ///< Owned pugixml document.
		std::unordered_map<std::string, pugi::xpath_query> xpath_cache; ///< Compiled query cache.
		std::mutex cache_mutex; ///< Guards xpath_cache.
		bool cache_enabled = false;
		/// @endcond

		Document();
		explicit Document( pugi::xml_document &&d );
		~Document() = default;

		Document( const Document & ) = delete;
		Document &operator=( const Document & ) = delete;

		Document( Document &&other ) noexcept;
		Document &operator=( Document &&other ) noexcept;

		// -------------------------------------------------------------------------
		// Methods exposed to Lua
		// -------------------------------------------------------------------------

		/**
		 * @brief Return the root element of the document.
		 *
		 * Delegates to `pugi::xml_document::document_element()`. For a standard
		 * XML document this is the single top-level element (e.g. `<rss>`,
		 * `<feed>`, `<root>`).
		 *
		 * @return @b Node  The root element, or `nil` in Lua if the document is empty.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local root = doc:root()
		 * print(root:name())   -- "rss", "feed", "root", …
		 * @endcode
		 */
		Node root() const;

		/**
		 * @brief Evaluate an XPath 1.0 expression and return all matching nodes.
		 *
		 * When the XPath cache is enabled (see ParseOptions::cache), repeated
		 * calls with the same expression string reuse the compiled query.
		 *
		 * @param[in] xpath  XPath 1.0 expression (UTF-8).
		 * @return @b NodeSet  A (possibly empty) ordered collection of matching nodes.
		 *
		 * @par Lua
		 * @code{.lua}
		 * for item in doc:select("//item"):iter() do
		 *     print(item:text())
		 * end
		 * @endcode
		 */
		NodeSet select( const std::string &xpath );

		/**
		 * @brief Evaluate an XPath 1.0 expression and return only the first match.
		 *
		 * @param[in] xpath  XPath 1.0 expression (UTF-8).
		 * @return @b Node  First matching node, or `nil` in Lua if nothing matched.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local title = doc:select_single("//channel/title"):text()
		 * @endcode
		 */
		Node select_single( const std::string &xpath );

		/**
		 * @brief Enable or disable the XPath compilation cache.
		 * @param[in] enable  `true` to enable caching.
		 */
		void set_cache_enabled( bool enable ) {
			cache_enabled = enable;
		}

		/**
		 * @brief Query whether the XPath compilation cache is active.
		 * @return `true` if caching is enabled.
		 */
		bool is_cache_enabled() const {
			return cache_enabled;
		}
	};

	// ---------------------------------------------------------------------------
	// Node
	// ---------------------------------------------------------------------------

	/**
	 * @brief Lightweight, lifetime-safe reference to a single DOM node.
	 *
	 * Internally stores a `pugi::xml_node` (a thin pointer-sized handle) plus a
	 * `weak_ptr<Document>` to detect document expiry. All methods first validate
	 * the node via `valid()` before touching pugixml data.
	 *
	 * @note When a Node would be invalid (missing element, expired document),
	 *       the Lua binding pushes `nil` rather than a null Node userdata, so
	 *       idiomatic `if not node then ... end` checks work naturally.
	 *
	 * @par Lua type
	 * `xml.node` userdata. Methods are accessible via `node:method()`.
	 *
	 * @par Special Lua-only method: `node:html()`
	 * This method has no C++ counterpart in this struct. In Lua it:
	 *  1. Detects whether the node's content is CDATA/text or child elements.
	 *  2. Extracts the raw HTML string accordingly (`text()` for CDATA,
	 *     `inner_xml()` for element children).
	 *  3. Fixes HTML5 void tags (`<img>`, `<br>`, etc.) to be self-closing.
	 *  4. Parses the result as a new `xml.document` using fragment-tolerant flags.
	 *  5. Returns `doc` on success, or `nil, errmsg` on failure.
	 *
	 * @par Lua — `node:html()` example
	 * @code{.lua}
	 * -- RSS <description> with embedded HTML in a CDATA section:
	 * local desc  = item:select_single("description")
	 * local hd, err = desc:html()
	 * if not hd then print("html parse error:", err) return end
	 *
	 * local img = hd:select_single("//img")
	 * print(img and img:attribute("src") or "no image")
	 *
	 * -- Element node containing child markup (not CDATA):
	 * local wrapper = doc:select_single('//*[@class="content"]')
	 * local hd2, _ = wrapper:html()
	 * for p in hd2:select("//p"):iter() do
	 *     print(p:text())
	 * end
	 * @endcode
	 */
	struct Node {

		pugi::xml_node node; ///< Thin pugixml node handle.
		std::weak_ptr<Document> doc; ///< Back-reference to the owning document.

		Node() = default;
		Node( pugi::xml_node n, std::weak_ptr<Document> d ) :
			node( n ),
			doc( std::move( d ) ) {
		}

		/**
		 * @brief Check whether the node is valid and safe to use.
		 *
		 * Returns `false` if the node handle is null (e.g. a failed `child()` or
		 * `select_single()`) or if the owning Document has been garbage-collected.
		 *
		 * @return `true` if the node can be safely accessed.
		 */
		bool valid() const {
			if ( node.empty() )
				return false;
			return !doc.expired();
		}

		/// @brief Implicit bool conversion — equivalent to `valid()`.
		explicit operator bool() const {
			return valid();
		}

		// -------------------------------------------------------------------------
		// Identity
		// -------------------------------------------------------------------------

		/**
		 * @brief Return the node type as a human-readable string.
		 *
		 * | pugixml type           | Returned string   |
		 * |------------------------|-------------------|
		 * | `node_element`         | `"element"`       |
		 * | `node_pcdata`          | `"text"`          |
		 * | `node_cdata`           | `"cdata"`         |
		 * | `node_comment`         | `"comment"`       |
		 * | `node_pi`              | `"pi"`            |
		 * | `node_declaration`     | `"declaration"`   |
		 * | `node_doctype`         | `"doctype"`       |
		 * | `node_document`        | `"document"`      |
		 * | `node_null` or expired | `"null"`          |
		 *
		 * @return @b string
		 *
		 * @par Lua
		 * @code{.lua}
		 * print(node:type())  -- "element", "text", "cdata", …
		 * @endcode
		 */
		std::string type() const;

		/**
		 * @brief Return the element tag name.
		 *
		 * Empty for non-element nodes (text, comment, etc.).
		 * Namespace prefixes are part of the name as stored by pugixml
		 * (e.g. `"dc:creator"`, `"atom:link"`).
		 *
		 * @return @b string  Tag name, e.g. `"item"`, `"rss"`, `"dc:creator"`.
		 *
		 * @par Lua
		 * @code{.lua}
		 * print(node:name())  -- "rss", "item", "dc:creator", …
		 * @endcode
		 */
		std::string name() const;

		/**
		 * @brief Return the raw node value.
		 *
		 * For `text` and `cdata` nodes: the character data content.
		 * For `element` nodes: always empty — use `text()` instead.
		 *
		 * @return @b string
		 */
		std::string value() const;

		/**
		 * @brief Return the concatenated text content of this node and all descendants.
		 *
		 * Equivalent to `innerText` / `textContent` in the browser DOM. For an
		 * element node it collects all descendant `text` and `cdata` values.
		 * For a CDATA container (e.g. RSS `<description>`) it returns the decoded
		 * CDATA string.
		 *
		 * @return @b string  May be empty if the node has no text descendants.
		 *
		 * @par Lua
		 * @code{.lua}
		 * -- Simple text content:
		 * local title = item:select_single("title"):text()
		 *
		 * -- CDATA section (returns the decoded HTML string inside it):
		 * local html_str = item:select_single("description"):text()
		 * @endcode
		 */
		std::string text() const;

		// -------------------------------------------------------------------------
		// Attributes
		// -------------------------------------------------------------------------

		/**
		 * @brief Return the value of a named attribute.
		 *
		 * @param[in] name  Attribute name (case-sensitive, UTF-8).
		 * @return @b string  Attribute value, or `""` if the attribute is absent.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local version = root:attribute("version")   -- "2.0"
		 * local href    = link:attribute("href")
		 * local src     = img:attribute("src")
		 * @endcode
		 */
		std::string attribute( const std::string &name ) const;

		/**
		 * @brief Return all attributes of this element.
		 *
		 * Iterates `pugi::xml_attribute` directly, preserving document order.
		 * Namespace declarations (`xmlns:*`) appear as regular attributes since
		 * pugixml operates in namespace-unaware mode.
		 *
		 * @return `vector<pair<string,string>>`  Each pair is `{ attr_name, attr_value }`.
		 *
		 * @par Lua — returns a `{name = value}` table
		 * @code{.lua}
		 * for name, value in pairs(root:attributes()) do
		 *     print(name, "=", value)
		 * end
		 * -- version   = 2.0
		 * -- xmlns:dc  = http://purl.org/dc/elements/1.1/
		 * -- host      = uxplanet.org
		 * @endcode
		 *
		 * @note Useful for **debugging** (verifying exactly what pugixml stored) and
		 *       for dynamically iterating attribute sets of unknown structure.
		 */
		std::vector<std::pair<std::string, std::string>> attributes() const;

		// -------------------------------------------------------------------------
		// Navigation
		// -------------------------------------------------------------------------

		/**
		 * @brief Return the first direct child element with the given tag name.
		 *
		 * @param[in] name  Tag name to match (exact, case-sensitive).
		 *                  Include the namespace prefix if present
		 *                  (e.g. `"dc:creator"`, `"atom:link"`).
		 * @return @b Node  Matching child, or `nil` in Lua if not found.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local channel = root:child("channel")
		 * local creator = item:child("dc:creator")
		 * local title   = channel:child("title"):text()
		 * @endcode
		 */
		Node child( const std::string &name ) const;

		/**
		 * @brief Return all direct child nodes, optionally filtered by tag name.
		 *
		 * @param[in] name  If empty (default), returns **all** direct children
		 *                  of any node type (elements, text nodes, comments, …).
		 *                  If non-empty, returns only element children whose
		 *                  tag name matches exactly (same rules as `child()`).
		 *
		 * @return `vector<Node>`  Ordered list; may be empty.
		 *
		 * @par Lua — all children
		 * @code{.lua}
		 * for _, child in ipairs(channel:children()) do
		 *     print(child:name(), child:type())
		 * end
		 * @endcode
		 *
		 * @par Lua — filtered by name
		 * @code{.lua}
		 * local items = root:child("channel"):children("item")
		 * for i, item in ipairs(items) do
		 *     print(i, item:select_single("title"):text())
		 * end
		 * @endcode
		 */
		std::vector<Node> children( const std::string &name = "" ) const;

		/**
		 * @brief Evaluate an XPath 1.0 expression **relative** to this node.
		 *
		 * Uses this node as the XPath context node. Relative paths (e.g. `"title"`,
		 * `"./link"`, `"..`) work as expected. Note that `"//x"` inside a relative
		 * expression still searches the entire document subtree below the context.
		 *
		 * @param[in] xpath  XPath 1.0 expression (UTF-8).
		 * @return @b NodeSet  All matching nodes; may be empty.
		 *
		 * @par Lua
		 * @code{.lua}
		 * -- Finds all <category> elements inside this <item>:
		 * for cat in item:select("category"):iter() do
		 *     print(cat:text())
		 * end
		 * @endcode
		 */
		NodeSet select( const std::string &xpath ) const;

		/**
		 * @brief Evaluate an XPath 1.0 expression relative to this node; return first match.
		 *
		 * @param[in] xpath  XPath 1.0 expression (UTF-8).
		 * @return @b Node  First matching node, or `nil` in Lua if nothing matched.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local title = item:select_single("title"):text()
		 * local img   = item:select_single(".//img")
		 * local src   = img and img:attribute("src") or ""
		 * @endcode
		 */
		Node select_single( const std::string &xpath ) const;

		// -------------------------------------------------------------------------
		// Serialisation
		// -------------------------------------------------------------------------

		/**
		 * @brief Serialise the children of this node as a raw XML string.
		 *
		 * Returns the markup of all child nodes without the node's own opening
		 * and closing tags. Analogous to `innerHTML` in the browser DOM.
		 * No indentation is added (uses `pugi::format_raw`).
		 *
		 * @return @b string  Raw XML, or `""` for an invalid or leaf node.
		 *
		 * @par Lua
		 * @code{.lua}
		 * -- Given: <p class="x">Hello <b>world</b></p>
		 * p:inner_xml()   -- → 'Hello <b>world</b>'
		 * @endcode
		 */
		std::string inner_xml() const;

		/**
		 * @brief Serialise this node including its own opening and closing tags.
		 *
		 * Analogous to `outerHTML` in the browser DOM.
		 * No indentation is added (uses `pugi::format_raw`).
		 *
		 * @return @b string  Raw XML, or `""` for an invalid node.
		 *
		 * @par Lua
		 * @code{.lua}
		 * -- Given: <p class="x">Hello <b>world</b></p>
		 * p:outer_xml()   -- → '<p class="x">Hello <b>world</b></p>'
		 * @endcode
		 */
		std::string outer_xml() const;
	};

	// ---------------------------------------------------------------------------
	// NodeSet
	// ---------------------------------------------------------------------------

	/**
	 * @brief An ordered collection of nodes produced by an XPath query.
	 *
	 * Holds a `shared_ptr<Document>` to keep the source document alive for
	 * the lifetime of the set, even if the original `doc` variable in Lua is
	 * released before the set is collected.
	 *
	 * @par Lua type
	 * `xml.nodeSet` userdata. Methods are accessible via `set:method()`.
	 */
	struct NodeSet {

		std::vector<Node> nodes; ///< Ordered query results.
		std::shared_ptr<Document> doc; ///< Keeps the owning document alive.

		NodeSet() = default;
		explicit NodeSet( pugi::xpath_node_set set, std::shared_ptr<Document> d );

		/**
		 * @brief Return the number of nodes in the set.
		 *
		 * @return @b integer  Count; 0 for an empty set.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local items = doc:select("//item")
		 * print("Found", items:size(), "items")
		 * @endcode
		 */
		size_t size() const {
			return nodes.size();
		}

		/**
		 * @brief Return the node at a 1-based index.
		 *
		 * Uses 1-based indexing to match Lua's table convention.
		 *
		 * @param[in] index  1-based position.
		 * @return @b Node   Node at that position, or `nil` in Lua if out of range.
		 *
		 * @par Lua
		 * @code{.lua}
		 * local first_item  = doc:select("//item"):get(1)
		 * local second_item = doc:select("//item"):get(2)
		 * @endcode
		 */
		Node get( int index ) const;

		/**
		 * @brief Return a const reference to the underlying node vector.
		 *
		 * Used internally by the iterator Lua binding.
		 * Prefer `iter()` from Lua.
		 *
		 * @return `const vector<Node>&`
		 */
		const std::vector<Node> &to_vector() const {
			return nodes;
		}
	};

	// ---------------------------------------------------------------------------
	// Module registration
	// ---------------------------------------------------------------------------

	/**
	 * @brief Register the `xml` module into `package.preload`.
	 *
	 * After this call, any Lua script can load the module with `require("xml")`.
	 * Must be called **once** during plugin initialisation, before any scripts run.
	 *
	 * Internally pushes `luaopen_xml` as a preloader so it is executed lazily on
	 * the first `require` call, which also registers the three metatables.
	 *
	 * @param[in] L     Active Lua state.
	 * @param[in] rain  Rainmeter plugin handle (currently unused, reserved).
	 *
	 * @par C++ usage
	 * @code{.cpp}
	 * xml::RegisterModule(L, rain);
	 * @endcode
	 */
	void RegisterModule( lua_State *L, Rain *rain );

	/**
	 * @brief Standard `luaopen_*` entry point invoked by `require("xml")`.
	 *
	 * Registers three named metatables in the Lua registry:
	 * - `xml.document` — for Document userdata
	 * - `xml.node`     — for Node userdata
	 * - `xml.nodeSet`  — for NodeSet userdata
	 *
	 * Each metatable has `__index = itself` so method calls resolve correctly,
	 * and a `__gc` metamethod that calls the C++ destructor in-place.
	 *
	 * Returns the module table with fields `parse` and `parse_html`.
	 *
	 * @note `extern "C"` linkage is required by Lua's `require` mechanism.
	 *
	 * @param[in] L  Active Lua state.
	 * @return       1 (module table pushed onto the stack).
	 */
	extern "C" int luaopen_xml( lua_State *L );

} // namespace xml
