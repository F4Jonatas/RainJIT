
<div align="center">

  # XML Module

  ### HTML parsing for Lua<br>Created for efficient extraction of web data

  <br>
  <br>


  <img src="../images/xml-logo.png" alt="LOGO" height="200">

</div>

# xml — Lua XML/HTML Parsing Module

A read-only XML and HTML parsing module for **Rainmeter**, built as a LuaJIT ↔ C++ binding on top of [**pugixml**](https://pugixml.org/). It exposes a clean DOM API and full XPath 1.0 support directly to Lua scripts.

# xml — Lua XML/HTML Parsing Module

A read-only XML and HTML parsing module for **Rainmeter**, built as a LuaJIT ↔ C++ binding on top of [pugixml](https://pugixml.org/). It exposes a clean DOM API and full XPath 1.0 support directly to Lua scripts.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Registration](#registration)
- [Module Functions](#module-functions)
- [Objects](#objects)
  - [Document](#document)
  - [Node](#node)
  - [NodeSet](#nodeset)
- [XPath Guide](#xpath-guide)
  - [Learning Resources](#learning-resources)
- [node:html() — Embedded HTML Parsing](#nodehtml--embedded-html-parsing)
  - [How it works](#how-it-works)
  - [Content detection](#content-detection)
  - [Void tag repair](#void-tag-repair)
  - [Parser limitations](#parser-limitations)
  - [Full example — RSS CDATA description](#full-example--rss-cdata-description)
  - [Full example — XML element children](#full-example--xml-element-children)
- [Parse Options](#parse-options)
- [Error Handling](#error-handling)
- [Namespaces](#namespaces)
- [Debugging](#debugging)
- [Limitations](#limitations)
- [Examples](#examples)

---

## Overview

```lua
local xml = require("xml")

local doc, err = xml.parse(data.text)
if not doc then error(err) end

local root = doc:root()
print(root:name())                          -- "rss", "feed", "root", …

for item in doc:select("//item"):iter() do
    print(item:select_single("title"):text())
end
```

**Key properties:**

- **Read-only** — the DOM cannot be modified after parsing.
- **UTF-8** throughout. All strings in and out are UTF-8.
- **No external allocations visible to Lua** — documents, nodes, and node sets are all garbage-collected Lua userdata objects.
- **Thread-safe per document** — each document is fully independent. The optional XPath cache is protected by a mutex.
- **Zero-copy node references** — `Node` objects hold a lightweight `pugi::xml_node` handle plus a `weak_ptr` back to the owning `Document`. The document stays alive as long as any node or node set derived from it is reachable.

---

## Architecture

```
Lua script
    │  require("xml")
    ▼
xml::RegisterModule()          ← called once at plugin startup
    │  registers luaopen_xml into package.preload
    ▼
luaopen_xml()                  ← called by require()
    │  registers 3 metatables in the Lua registry:
    │    xml.document, xml.node, xml.nodeSet
    │  returns module table { parse, parse_html }
    ▼
xml.parse(str) / xml.parse_html(str)
    │  creates std::shared_ptr<Document>  (owns pugi::xml_document)
    │  pushes userdata with metatable "xml.document"
    ▼
doc:root() / doc:select() / doc:select_single()
    │  returns userdata with metatable "xml.node" or "xml.nodeSet"
    │  Node holds pugi::xml_node + weak_ptr<Document>
    ▼
Garbage collection (__gc)
    calls the C++ destructor of each userdata in-place
    shared_ptr ref-count drops → Document freed when no nodes remain
```

The metatables use `__index = metatable` so that method calls (`doc:root()`, `node:text()`, etc.) are resolved correctly without an extra wrapper table per object.

---

## Registration

Call `xml::RegisterModule` once when your plugin initialises, **before** any Lua script runs:

```cpp
#include "xml.hpp"

// Inside your plugin's Initialize or equivalent:
xml::RegisterModule(L, rain);
```

From Lua, load the module with `require`:

```lua
local xml = require("xml")
```

---

## Module Functions

### `xml.parse(str [, options])` → `doc, nil` | `nil, errmsg`

Parses a well-formed XML string. Returns a `Document` on success, or `nil` plus an error message on failure.

```lua
local doc, err = xml.parse(data.text)
if not doc then
    print("Parse failed:", err)
    return
end
```

### `xml.parse_html(str [, options])` → `doc, nil` | `nil, errmsg`

Parses an HTML string using tolerant flags (`parse_embed_pcdata`, `parse_ws_pcdata`). Useful for scraping HTML that is not strict XML. Returns the same types as `xml.parse`.

```lua
local doc, err = xml.parse_html(html_string)
```

> See [Parse Options](#parse-options) for the optional second argument.

---

## Objects

### Document

Returned by `xml.parse` and `xml.parse_html`. Owns the entire DOM tree.

| Method | Returns | Description |
|---|---|---|
| `doc:root()` | `Node` | Root element of the document (`document_element()`). |
| `doc:select(xpath)` | `NodeSet` | Evaluate an XPath expression; returns all matching nodes. |
| `doc:select_single(xpath)` | `Node` or `nil` | Evaluate an XPath expression; returns only the first match. |

> **Lifetime:** keep the `doc` variable alive as long as you use any `Node` or `NodeSet` derived from it. If `doc` is collected first, all derived nodes become invalid and their methods return empty strings or `nil`.

---

### Node

A lightweight reference to a single DOM node. Returned by most methods.

When a method would return an invalid or missing node, it returns `nil` instead of a null `Node` — this makes Lua `if` checks natural:

```lua
local node = doc:select_single("//missing")
if not node then print("not found") end
```

#### Identity & Type

| Method | Returns | Description |
|---|---|---|
| `node:name()` | `string` | Element tag name, e.g. `"item"`. Empty for text/comment nodes. |
| `node:type()` | `string` | One of: `"element"`, `"text"`, `"cdata"`, `"comment"`, `"pi"`, `"declaration"`, `"doctype"`, `"document"`, `"null"`. |
| `node:value()` | `string` | Raw node value. Meaningful for `text` and `comment` nodes. |
| `node:text()` | `string` | Concatenated text content of the node and all its descendants. Equivalent to `innerText` in browsers. |

#### Attributes

| Method | Returns | Description |
|---|---|---|
| `node:attribute(name)` | `string` | Value of the named attribute, or `""` if absent. |
| `node:attributes()` | `table` | All attributes as `{ name = value }`. Preserves document order in iteration via `pairs()`. |

```lua
-- Single attribute
local version = root:attribute("version")   -- "2.0"

-- All attributes (useful for debugging or dynamic access)
for name, value in pairs(root:attributes()) do
    print(name, "=", value)
end
```

#### Navigation

| Method | Returns | Description |
|---|---|---|
| `node:child(name)` | `Node` or `nil` | First direct child element with the given tag name. |
| `node:children()` | `table` of `Node` | All direct child nodes (elements, text, comments, …). |
| `node:children(name)` | `table` of `Node` | All direct child elements with the given tag name. |

```lua
local channel = root:child("channel")
local items   = channel:children("item")   -- array of Node

for i, item in ipairs(items) do
    print(i, item:select_single("title"):text())
end
```

#### XPath on Nodes

XPath expressions issued from a `Node` are **relative** to that node:

| Method | Returns | Description |
|---|---|---|
| `node:select(xpath)` | `NodeSet` | All matches relative to this node. |
| `node:select_single(xpath)` | `Node` or `nil` | First match relative to this node. |

```lua
local item = doc:select_single("//item")

-- Relative XPath from the item node:
local title = item:select_single("title"):text()

-- Equivalent absolute XPath from the document:
local title = doc:select_single("//item/title"):text()
```

#### Serialisation

| Method | Returns | Description |
|---|---|---|
| `node:inner_xml()` | `string` | Serialised child content, without the node's own tags. |
| `node:outer_xml()` | `string` | Serialised node including its own start and end tags. |
| `node:html()` | `Document` or `nil, errmsg` | Extracts the node's HTML content, repairs void tags, and returns a fully queryable `Document`. See [node:html()](#nodehtml--embedded-html-parsing). |

```lua
-- Given: <p class="intro">Hello <b>world</b></p>
p:inner_xml()  -- → 'Hello <b>world</b>'
p:outer_xml()  -- → '<p class="intro">Hello <b>world</b></p>'
```

---

### NodeSet

A collection of nodes returned by XPath queries. Keeps the source `Document` alive for its own lifetime.

| Method | Returns | Description |
|---|---|---|
| `set:size()` | `integer` | Number of nodes in the set. |
| `set:get(i)` | `Node` or `nil` | Node at 1-based index `i`. |
| `set:iter()` | iterator | Generic `for` iterator over all nodes. |

```lua
-- size + get
local items = doc:select("//item")
for i = 1, items:size() do
    print(items:get(i):select_single("title"):text())
end

-- iter (preferred)
for item in doc:select("//item"):iter() do
    print(item:select_single("title"):text())
end
```

---

## XPath Guide

The module exposes **XPath 1.0** as implemented by pugixml. Below are the most useful patterns for real-world XML/RSS/Atom feeds.

### Basic selectors

```lua
-- Absolute path
doc:select_single("/rss/channel/title"):text()

-- Descendant anywhere in tree
doc:select("//item")

-- Direct child
doc:select("channel/item")

-- Element with specific attribute value
doc:select("//item[@id='42']")

-- Any element with a given attribute
doc:select("//*[@href]")
```

### Text and attribute values in XPath

```lua
-- Select nodes whose text equals a value
doc:select("//category[.='design']")

-- Select nodes by attribute value
doc:select("//link[@rel='alternate']")
```

### Positional selectors

```lua
-- First item
doc:select_single("//item[1]")

-- Last item
doc:select_single("//item[last()]")

-- Items 2 through 4
doc:select("//item[position() >= 2 and position() <= 4]")
```

### Namespaces

pugixml does **not** process XML namespaces — it treats `dc:creator` as a literal attribute/element name. Use `local-name()` to match regardless of prefix:

```lua
-- Works even if the prefix changes across feeds
local creator = item:select_single("*[local-name()='creator']"):text()

-- Or match by literal prefixed name (fragile if prefix varies)
local creator = item:child("dc:creator"):text()
```

### Combining axes

```lua
-- Parent of a node
node:select_single("..")

-- All ancestors
node:select("ancestor::*")

-- Following siblings
node:select("following-sibling::item")

-- Nodes that contain a given substring in their text
doc:select("//title[contains(., 'Lua')]")

-- Case-insensitive workaround (XPath 1.0 has no lower-case())
doc:select("//title[contains(translate(., 'ABCDEFGHIJKLMNOPQRSTUVWXYZ',
                                        'abcdefghijklmnopqrstuvwxyz'), 'lua')]")
```

### XPath Compilation Cache

For scripts that run repeatedly (e.g. every Rainmeter update cycle) with the same XPath expressions, enable caching to avoid recompiling queries each time:

```lua
local doc = xml.parse(data.text, { cache = true })

-- These two queries are compiled once and reused on subsequent calls:
for item in doc:select("//item"):iter() do
    local title = item:select_single("title"):text()   -- compiled once
    local link  = item:select_single("link"):text()    -- compiled once
    print(title, link)
end
```

The cache is stored per-document and protected by a mutex, so it is safe even if Rainmeter calls the update script from multiple threads.

### Learning Resources

XPath 1.0 is a well-established standard with excellent documentation available online. If you are new to XPath or want to go beyond the patterns shown above, the following resources are recommended:

| Resource | Description |
|---|---|
| [MDN XPath reference](https://developer.mozilla.org/en-US/docs/Web/XPath) | Comprehensive function and axis reference with browser-oriented examples. |
| [W3Schools XPath tutorial](https://www.w3schools.com/xml/xpath_intro.asp) | Beginner-friendly introduction covering syntax, axes, functions, and operators with an interactive tryout editor. |
| [XPath 1.0 specification (W3C)](https://www.w3.org/TR/xpath-10/) | The authoritative normative reference for XPath 1.0 — useful when you need to understand exact evaluation semantics. |
| [ZVON XPath reference](http://zvon.org/xxl/XPathTutorial/General/examples.html) | Interactive, example-driven tutorial covering every XPath axis and function with live XML samples. |
| [pugixml XPath docs](https://pugixml.org/docs/manual.html#xpath) | Documents which XPath 1.0 features pugixml supports and any engine-specific behaviour. |

> **Tip:** W3Schools' tryout editor lets you paste your own XML and test XPath expressions in the browser before writing any Lua code.

---

## node:html() — Embedded HTML Parsing

Many real-world XML feeds embed HTML content inside their nodes — either as a raw CDATA section (common in RSS) or as literal child elements. `node:html()` is a dedicated method for this pattern: it extracts that content, repairs it for the XML parser, and returns a fully queryable `Document`.

```
node:html() → doc, nil   (on success)
           → nil, errmsg (on failure)
```

### How it works

`node:html()` performs four steps internally:

```
1. Extract  ──▶  Detect whether content is CDATA/text or child elements
                 and obtain the raw HTML string accordingly.

2. Repair   ──▶  FixVoidTags() rewrites HTML5 void elements
                 (<img>, <br>, <input>, etc.) to self-closing XHTML form.

3. Parse    ──▶  Feed the fixed string into pugixml with fragment-tolerant
                 flags (parse_fragment | parse_embed_pcdata | parse_ws_pcdata).

4. Return   ──▶  Wrap the resulting pugi::xml_document in a new Document
                 userdata and push it onto the Lua stack.
```

The returned document is completely independent — it has its own lifetime, its own optional XPath cache, and its nodes do not share ownership with the original document.

### Content detection

The method automatically chooses the right extraction strategy based on the node's first child:

| First child type | Strategy | When it applies |
|---|---|---|
| `cdata` or `text` | `node:text()` | RSS `<description>` with a `<![CDATA[…]]>` section |
| `element` | `node:inner_xml()` | Any node whose children are XML/HTML elements |

This means `node:html()` works correctly for both of the most common patterns without requiring any configuration.

### Void tag repair

HTML5 defines a set of **void elements** — tags that are never closed because they cannot have children. In standard HTML they are written as `<img src="…">`. This is invalid XML, and pugixml would either reject the document or silently treat the next sibling as a child of `<img>`.

`FixVoidTags()` scans the extracted string byte-by-byte and rewrites each void opening tag to self-closing form:

```
<img src="x.png" width="800">   →   <img src="x.png" width="800"/>
<br>                             →   <br/>
<input type="text">              →   <input type="text"/>
```

Tags that are **already** self-closed (`<br/>`, `<br />`) are left untouched.

Handled void elements: `area`, `base`, `br`, `col`, `embed`, `hr`, `img`, `input`, `link`, `meta`, `param`, `source`, `track`, `wbr`.

### Parser limitations

`node:html()` uses pugixml as its parsing engine. pugixml is a **strict XML parser** adapted for tolerant HTML parsing — it is not a full HTML5 parser. This has concrete consequences you should be aware of:

**What works well:**
- Well-formed or near-well-formed HTML snippets (like those generated by Medium, WordPress, or similar CMS platforms).
- CDATA-escaped HTML from RSS feeds, after void-tag repair.
- Simple DOM structures with `<div>`, `<p>`, `<a>`, `<img>`, `<span>`, `<ul>`, `<li>`.

**What does not work:**
- **Optional closing tags.** HTML5 allows omitting `</p>`, `</li>`, `</td>`, `</tr>`, etc. pugixml does not infer them — the nesting will be wrong or the parse will fail.
- **Implicit `<html>`/`<body>` wrapping.** A real browser inserts these automatically; pugixml does not.
- **Script and style content.** The content of `<script>` and `<style>` tags often contains characters (`<`, `>`, `&`) that are invalid in XML text. These tags will frequently cause parse errors.
- **Malformed attribute quoting.** Some sites use unquoted or single-quoted attributes in ways that break XML parsing.
- **HTML entities.** Named entities like `&nbsp;`, `&mdash;`, `&copy;` are **not** defined in XML. pugixml only recognises the five standard XML entities (`&amp;`, `&lt;`, `&gt;`, `&apos;`, `&quot;`). Numeric entities (`&#160;`, `&#x2014;`) work fine.
- **Deeply broken markup.** Content scraped from arbitrary websites may be too malformed for pugixml to produce a useful tree. For such cases a dedicated HTML5 parser (e.g. Gumbo, lexbor) would be required.

**The `parse_fragment` flag** allows the parsed snippet to have multiple root-level elements (e.g. a sequence of `<p>` tags with no wrapping `<div>`). Without it, pugixml requires a single root element.

> **Practical rule:** if the HTML comes from a structured API or a known CMS (Medium, Ghost, RSS 2.0), `node:html()` will work reliably. If it comes from arbitrary web scraping, test against the actual content first.

### Full example — RSS CDATA description

This is the primary use case: an RSS feed where `<description>` contains a `<![CDATA[…]]>` section with embedded HTML.

```xml
<!-- Inside an RSS <item>: -->
<description>
  <![CDATA[
    <div class="medium-feed-item">
      <p class="medium-feed-image">
        <a href="https://uxplanet.org/article">
          <img src="https://cdn-images-1.medium.com/max/2600/photo.png" width="5848">
        </a>
      </p>
      <p class="medium-feed-snippet">
        Reaching the usage limit is one of the most annoying things&#x2026;
      </p>
      <p class="medium-feed-link">
        <a href="https://uxplanet.org/article">Continue reading on UX Planet »</a>
      </p>
    </div>
  ]]>
</description>
```

```lua
local doc, err = xml.parse(data.text)
if not doc then print("feed parse error:", err) return end

for item in doc:select("//item"):iter() do
    local title = item:select_single("title"):text()

    -- Get the <description> node; its content is a CDATA section.
    local desc_node = item:select_single("description")
    if not desc_node then
        print(title, "— no description")
        goto continue
    end

    -- node:html() detects the CDATA automatically, repairs void tags
    -- (<img> → <img/>), and returns a new queryable Document.
    local hd, html_err = desc_node:html()
    if not hd then
        print(title, "— html parse failed:", html_err)
        goto continue
    end

    -- Now query the HTML document normally with XPath.
    local img_node  = hd:select_single("//img")
    local link_node = hd:select_single("//a[@href]")
    local snip_node = hd:select_single("//*[contains(@class,'medium-feed-snippet')]")

    local image   = img_node  and img_node:attribute("src")  or ""
    local href    = link_node and link_node:attribute("href") or ""
    local snippet = snip_node and snip_node:text()           or ""

    print(string.format(
        "Title:   %s\nImage:   %s\nLink:    %s\nSnippet: %s\n",
        title, image, href, snippet
    ))

    ::continue::
end
```

**Expected output:**

```
Title:   How to prevent "You've hit your limit" when working with Claude Code
Image:   https://cdn-images-1.medium.com/max/2600/photo.png
Link:    https://uxplanet.org/article
Snippet: Reaching the usage limit is one of the most annoying things…
```

### Full example — XML element children

When a node contains actual child elements (not CDATA), `node:html()` uses `inner_xml()` for extraction. This pattern appears in custom XML APIs or Atom feeds that embed markup directly.

```xml
<content type="html_string">
  <div class="medium-feed-item">
    <p class="medium-feed-image">
      <a href="https://uxplanet.org/article">
        <img src="https://cdn.example.com/photo.png" width="800">
      </a>
    </p>
    <p class="medium-feed-snippet">Article snippet text here.</p>
    <p class="medium-feed-link">
      <a href="https://uxplanet.org/article">Continue reading »</a>
    </p>
  </div>
</content>
```

```lua
local doc, err = xml.parse_html(raw_xml)
if not doc then print("parse error:", err) return end

-- Locate the outer wrapper node.
local wrapper = doc:select_single('//*[@type="html_string"]')
if not wrapper then print("wrapper not found") return end

-- html() detects element children, serialises them via inner_xml(),
-- fixes <img> → <img/>, and re-parses as a fragment document.
local hd, html_err = wrapper:html()
if not hd then print("html parse error:", html_err) return end

-- Query the resulting document.
print("Root name:", hd:root():name())   -- first top-level element

local all_p = hd:select("//p")
print("Total <p> elements:", all_p:size())

for i = 1, all_p:size() do
    local p = all_p:get(i)
    print(string.format("  <p class=%q>  %s", p:attribute("class"), p:text()))
end

-- contains() on @class works as expected.
local snippet = hd:select_single("//*[contains(@class,'medium-feed-snippet')]")
if snippet then
    print("Snippet:", snippet:text())
end

-- Images: attribute() works correctly after void-tag repair.
for img in hd:select("//img"):iter() do
    print("img src:", img:attribute("src"),
          "width:", img:attribute("width"))
end
```

---

## Parse Options

Both `xml.parse` and `xml.parse_html` accept an optional table as their second argument:

```lua
local doc = xml.parse(str, {
    cache      = false,   -- enable XPath compilation cache (default: false)
    parse_full = false,   -- use pugi::parse_full flags (default: false)
})
```

| Option | Type | Default | Description |
|---|---|---|---|
| `cache` | `boolean` | `false` | Pre-compiles and caches XPath queries. Speeds up repeated queries on the same document. |
| `parse_full` | `boolean` | `false` | Enables pugixml's `parse_full` flag set, which preserves processing instructions, declarations, and doc-type nodes. |

---

## Error Handling

`xml.parse` and `xml.parse_html` **never throw** — they always return two values:

```lua
local doc, err = xml.parse(str)

-- On success:  doc = Document userdata,  err = nil
-- On failure:  doc = nil,                err = "XML parse error at offset N: <reason>"
```

Always check the first return value before using the document:

```lua
local doc, err = xml.parse(data.text)
if not doc then
    -- log the error, show a fallback, etc.
    SKIN:Bang("!SetOption", "Status", "Text", "Feed unavailable: " .. err)
    return
end
```

### Defensive access pattern

Because `select_single` returns `nil` for missing nodes, chain checks before calling `:text()`:

```lua
local function safe_text(node)
    return node and node:text() or ""
end

local item  = doc:select_single("//item")
local title = safe_text(item and item:select_single("title"))
local link  = safe_text(item and item:select_single("link"))
```

---

## Namespaces

pugixml operates in **namespace-unaware mode**: namespace prefixes are stored verbatim as part of element and attribute names. This has two practical consequences:

**1. Prefixed elements must be matched literally or with `local-name()`:**

```lua
-- Literal (works when the prefix is always "dc:")
item:child("dc:creator"):text()

-- Robust (works regardless of the prefix used in the document)
item:select_single("*[local-name()='creator']"):text()
```

**2. `xmlns:*` declarations appear as regular attributes:**

```lua
for name, value in pairs(root:attributes()) do
    -- "xmlns:dc", "xmlns:atom", etc. are just normal attributes here
    print(name, value)
end
```

---

## Debugging

### Inspect a node's attributes

```lua
local root = doc:root()
print("tag:", root:name(), "| type:", root:type())

for name, value in pairs(root:attributes()) do
    print(string.format("  @%-20s = %q", name, value))
end
```

### Inspect raw XML around a node

```lua
print(node:outer_xml())   -- full tag with children
print(node:inner_xml())   -- children only
```

### Verify what was actually received

```lua
-- Print the first 300 chars to confirm the raw content
print(string.sub(data.text, 1, 300))
print("total length:", #data.text)
```

### Common pitfalls

| Symptom | Likely cause |
|---|---|
| `attribute("x")` returns `""` but the attribute exists in the file | The live feed differs from a cached/copied sample; verify with `attributes()` and print the raw text. |
| `select_single` returns `nil` for an existing element | XPath prefix mismatch; use `local-name()` or verify the exact tag name with `node:name()`. |
| All node methods return `""` or `nil` | The `Document` was garbage-collected; keep a reference to `doc` alive for as long as you use its nodes. |
| `parse` returns `nil` with an offset error | The input has a BOM, is HTML (not XML), or is truncated; try `parse_html` for lenient parsing. |

---

## Limitations

- **Read-only.** There are no methods to modify, add, or remove nodes or attributes. If you need to transform XML, do it as string manipulation before parsing.
- **XPath 1.0 only.** XPath 2.0/3.0 functions (`lower-case()`, `string-join()`, etc.) are not available. Use Lua string functions as a post-processing step.
- **No attribute-node XPath results.** Expressions like `/rss/@version` select an attribute node, not an element node. The current `NodeSet` only stores element/text nodes. Use `node:attribute("version")` instead.
- **No namespace awareness.** Prefix-to-URI binding is not resolved. Use `local-name()` for robust cross-feed matching.
- **No streaming.** The entire document is parsed into memory at once. Very large feeds (several MB) may cause noticeable memory usage.
- **No write-back / serialisation of the full document.** `inner_xml`/`outer_xml` work per-node but there is no `doc:to_string()` for the whole tree.
- **`node:html()` is not a full HTML5 parser.** It uses pugixml with tolerant flags and void-tag repair. Optional closing tags, named HTML entities (`&nbsp;`), and heavily malformed markup may cause incorrect trees or parse errors. See [Parser limitations](#parser-limitations) for the complete list.

---

## Examples

### Parse an RSS feed and list items

```lua
local xml = require("xml")

local doc, err = xml.parse(data.text)
if not doc then return end

local root = doc:root()
assert(root:name() == "rss", "Not an RSS feed")

local version = root:attribute("version")     -- "2.0"
print("RSS version:", version)

-- Channel metadata
local channel = root:child("channel")
local feed_title = channel:child("title"):text()
local feed_link  = channel:child("link"):text()
print("Feed:", feed_title, "—", feed_link)

-- Items
for item in doc:select("//item"):iter() do
    local title   = item:select_single("title"):text()
    local link    = item:select_single("link"):text()
    local pubDate = item:select_single("pubDate"):text()
    local creator = item:select_single("*[local-name()='creator']")
    local author  = creator and creator:text() or "(unknown)"

    print(string.format("[%s] %s\n  by %s\n  %s", pubDate, title, author, link))
end
```

### Parse an Atom feed

```lua
local doc, err = xml.parse(data.text)
if not doc then return end

-- Atom uses <feed> as root and <entry> for items
local root = doc:root()
assert(root:name() == "feed", "Not an Atom feed")

for entry in doc:select("//entry"):iter() do
    local title = entry:select_single("title"):text()
    -- Atom links are in attributes, not text content
    local link_node = entry:select_single("link[@rel='alternate']")
                   or entry:select_single("link")
    local href = link_node and link_node:attribute("href") or ""
    print(title, href)
end
```

### Parse HTML and scrape links

```lua
local doc, err = xml.parse_html(html_string)
if not doc then return end

for a in doc:select("//a[@href]"):iter() do
    local href = a:attribute("href")
    local text = a:text()
    if href ~= "" then
        print(string.format("%-50s  %s", text, href))
    end
end
```

### Collect multiple categories per item

```lua
for item in doc:select("//item"):iter() do
    local title = item:select_single("title"):text()

    local cats = {}
    for cat in item:select("category"):iter() do
        table.insert(cats, cat:text())
    end

    print(title, "→", table.concat(cats, ", "))
end
```

### Extract a fallback value for an optional attribute

```lua
local root = doc:root()

-- Try direct attribute first, fall back to parsing the link URL
local host = root:attribute("host")
if host == "" then
    local link_node = doc:select_single("/rss/channel/link")
    if link_node then
        host = link_node:text():match("https?://([^/?]+)") or ""
    end
end

print("Host:", host)
```

### Use the XPath cache for a high-frequency update script

```lua
-- Called by Rainmeter every N seconds
function Update()
    local doc, err = xml.parse(data.text, { cache = true })
    if not doc then return end

    -- These XPath strings are compiled only once per document instance
    local title = doc:select_single("//item[1]/title"):text()
    local link  = doc:select_single("//item[1]/link"):text()

    SKIN:Bang("!SetOption", "TitleMeter", "Text", title)
    SKIN:Bang("!SetOption", "LinkMeter",  "Text", link)
end
```

---

## Dependencies

| Dependency | Role |
|---|---|
| [pugixml](https://pugixml.org/) ≥ 1.13 | DOM parsing engine and XPath 1.0 evaluation |
| LuaJIT / Lua 5.1 | Scripting runtime |
| `utils/strings.hpp` | UTF-8 ↔ UTF-16 conversion for Rainmeter APIs |
| `Includes/rain.hpp` | Rainmeter plugin interface |

---

## License

GPL v2.0 — see the `LICENSE` file in the repository root.
