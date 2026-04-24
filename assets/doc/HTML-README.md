
<div align="center">

  # HTML Module

  ### HTML parsing for Lua<br>Created for efficient extraction of web data

  <br>

</div>


A lightweight, read‑only HTML parsing module for Lua, built on top of [Google’s **Gumbo** HTML5 parser](https://github.com/google/gumbo-parser).<br>
Designed for **web scraping** and **DOM querying**, it offers a familiar **jQuery‑like** API with [**CSS selector support**](https://developer.mozilla.org/en-US/docs/Web/CSS/Guides/Selectors), including attribute operators and basic pseudo‑classes.

<br>
<br>


## Features

- **Parse HTML** into a DOM tree – fast, compliant with HTML5.
- **CSS‑like selectors** (subset):
  - Tag, ID, class (e.g. `div`, `#main`, `.info`)
  - Attribute presence and value matching (`[href]`, `[type="text"]`)
  - Attribute operators: `=` (equals), `^=` (starts with), `$=` (ends with), `*=` (contains)
  - Pseudo‑classes: `:first‑child`, `:last‑child`, `:empty`
  - Combinators: descendant (space), direct child (`>`), group (`,`)
- **Traversal methods** on nodes: children, parent, text content, attributes.
- **Node list** with common helpers (`count`, `first`, `last`, `eq`, `text`, `attr`).
- **Deduplicated results** – no duplicate nodes in query outputs.
- **Zero modification** – the DOM is immutable, perfect for scraping.

---

<br>
<br>



### Loading in Lua

```lua

-- Register the module with package.preload (C side)
-- In your C/C++ host: html::RegisterModule(L);
-- Then in Lua:
local html = require("html")
```


## API Reference

### `html.parse(html_string)`

Parses a string containing HTML and returns an `HtmlDocument` object.



```lua

local doc = html.parse("<html>...</html>")

### `HtmlDocument`

|Method|Description|
|---|---|
|`doc:root()`|Returns the root `HtmlNode` of the document.|
|`doc:find(selector)`|Returns an `HtmlNodeList` of all elements matching the CSS selector (relative to document root).|

### `HtmlNode`

|Method|Description|
|---|---|
|`node:name()`|Tag name (e.g. `"div"`, `"a"`). Returns `nil` for non‑element nodes.|
|`node:text()`|Recursively extracts all text inside the node (concatenated).|
|`node:attr(name)`|Value of the given attribute, or `nil` if not present.|
|`node:children()`|Returns an `HtmlNodeList` of direct element children.|
|`node:parent()`|Parent node (as `HtmlNode`) or `nil` if none.|
|`node:find(selector)`|Returns an `HtmlNodeList` of all descendants matching the selector.|

### `HtmlNodeList`

|Method|Description|
|---|---|
|`list:count()`|Number of nodes in the list.|
|`list:first()`|First node, or `nil` if empty.|
|`list:last()`|Last node, or `nil` if empty.|
|`list:eq(index)`|Node at 1‑based `index`, or `nil`.|
|`list:text()`|Concatenated text of all nodes (recursive).|
|`list:attr(name)`|Value of the first attribute `name` found in the list, or `nil`.|

**Note:** Node lists are not iterable by default in Lua, but you can use `count()` and `eq()` in a loop.  
_(Future versions may add `__len` and `__index` for `ipairs` support.)_

## Supported CSS Selectors

### Basic selectors

|Example|Description|
|---|---|
|`div`|All `<div>` elements.|
|`#main`|Element with id `"main"`.|
|`.active`|Elements with class `"active"`.|
|`div.box`|`<div>` with class `"box"`.|
|`div#main.active`|`<div id="main" class="active">`.|

### Attribute selectors

|Example|Description|
|---|---|
|`a[href]`|`<a>` with an `href` attribute.|
|`input[type="text"]`|Attribute equals `"text"`.|
|`a[href^="https"]`|`href` starts with `"https"`.|
|`a[href$=".pdf"]`|`href` ends with `".pdf"`.|
|`a[href*="example"]`|`href` contains `"example"`.|

### Pseudo‑classes

|Example|Description|
|---|---|
|`p:first-child`|`<p>` that is the first child of its parent.|
|`p:last-child`|`<p>` that is the last child.|
|`div:empty`|`<div>` with no children (including text nodes).|

### Combinators

|Example|Description|
|---|---|
|`div a`|Any `<a>` descendant of a `<div>`.|
|`div > a`|`<a>` that is a direct child of a `<div>`.|
|`div, a`|All `<div>` and all `<a>` elements (union).|

## Limitations

- **Read‑only** – No methods to modify the DOM. Use for scraping only.
    
- **No support for**:
    
    - Pseudo‑classes: `:nth-child`, `:not`, `:has`, `:contains`, etc.
        
    - Attribute operators: `~=`, `|=`.
        
    - Combinators: `+` (adjacent sibling), `~` (general sibling).
        
    - Selector specificity or `:is()` / `:where()`.
        
- **Whitespace sensitivity** – Selectors should not contain extra spaces inside attribute brackets or around combinators (e.g., `div > a` works, but `div> a` may fail). Use spaces consistently.
    
- **No XPath support**.
    
- **Memory** – The entire DOM is kept in memory until the `HtmlDocument` is garbage collected. For huge documents, consider streaming parsers.
    

## Example: Web Scraping

lua

local html = require("html")
local doc = html.parse([[
<html>
<body>
  <div id="content">
    <h1>News</h1>
    <ul class="items">
      <li><a href="/a.html">First</a></li>
      <li><a href="/b.html">Second</a></li>
    </ul>
  </div>
</body>
</html>
]])
-- Find all links inside the items list
local links = doc:find("ul.items li a")
print("Found " .. links:count() .. " links")
-- Extract each link's href and text
for i = 1, links:count() do
    local link = links:eq(i)
    print(link:attr("href"), link:text())
end
-- Get the first heading text
local heading = doc:find("h1"):first()
print(heading:text())  -- "News"
-- Direct child selector
local direct_children = doc:root():find("body > div")
print(direct_children:count())  -- 1

## Technical Notes

- **Parsing** – Gumbo produces a read‑only tree. This module wraps it in lightweight userdata objects.
    
- **Selector matching** – Implements a recursive descent matcher.  
    Results are automatically **deduplicated** to avoid duplicates caused by multiple matching paths.
    
- **Performance** – For small to medium documents (up to a few MB) it is very fast. For huge pages, consider limiting selector complexity.
    
- **Garbage collection** – The underlying `GumboOutput` is freed when the `HtmlDocument` is collected. Node and node list objects hold only references to the document and raw pointers to tree nodes.
    

## License

This module is provided under the **MIT License**.  
Gumbo is distributed under the Apache License 2.0.

---

_Built for efficient web scraping with Lua. Contributions and bug reports welcome._