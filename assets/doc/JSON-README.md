<div align="center">

  # JSON

  ### Read and write JSON and JSON5

  <br>

  <img src="../../assets/images/json-logo.png" alt="LOGO" width="200" height="200">

</div>

<br>
<br>


## Overview

This module reads and writes JSON from Lua. It is built around two familiar functions, `JSON.parse()` and `JSON.stringify()`, so anyone who has used JSON in JavaScript can use it without learning a new API. Two helpers, `JSON.load()` and `JSON.save()`, do the same work directly on files and understand **Rainmeter** variables such as `#@#`.

The reader accepts **JSON5**, a superset of JSON designed to be pleasant to write and maintain by hand. It allows comments (`//` and `/* */`), keys without quotes, single-quoted strings and trailing commas. These features make configuration files easier to read, easier to document and less fragile: a forgotten comma or a missing quote no longer breaks the whole file, and every setting can carry a note explaining what it does.

Regular JSON is still fully supported. Every valid JSON document is also a valid JSON5 document, so existing files, data exported by other tools and responses from web services are read without any change. When writing, the module produces **strict JSON by default**, so the result can be consumed by any other parser. JSON5-style output (unquoted keys) is available as an opt-in option.

<br>
<br>


## :green_book: Features

- Familiar API (`JSON.parse`, `JSON.stringify`)
- Reads **JSON** and **JSON5** (comments, unquoted keys, single quotes, trailing commas)
- Writes strict **JSON**, with optional pretty printing and [**JSON5-style**](https://spec.json5.org/) keys
- File helpers with **Rainmeter** variable support (`JSON.load`, `JSON.save`)
- Atomic file writes and automatic creation of missing directories
- `JSON.null` sentinel, so `null` never breaks array length
- Empty arrays and objects keep their type (`JSON.array`, `JSON.object`)
- Deterministic output (keys are always sorted)
- Full number precision on output
- Strict UTF-8 validation and depth limit
- Errors are returned as values (`nil, message`), never thrown for bad data

<br>
<br>


## :jigsaw: Quick Example

### Read a configuration file written by hand, with comments and a trailing comma:

```lua
local JSON = require("json")

local cfg = JSON.parse([[
{
  // window geometry
  width: 300,
  height: 120,
  title: 'My Skin',
  tags: ['a', 'b',],
}
]])

print(cfg.width, cfg.title, #cfg.tags)  -- 300  My Skin  2
```


### Load, change and save a settings file:

```lua
local cfg = JSON.load("#@#settings.json") or {}
cfg.width = 100

JSON.save("#@#settings.json", cfg, { pretty = true })
```

---

<br>
<br>


## :book: Module `json`

### :diamond_shape_with_a_dot_inside: Property `JSON.null`

Sentinel that represents JSON `null`.<br>
It is a light userdata (a `NULL` pointer), so it can be stored inside tables and compared with `==`. Using it instead of `nil` keeps array positions intact: `[1, null, 3]` is read as a table with 3 elements.

```lua
local t = JSON.parse('[1, null, 3]')

print(#t, t[2] == JSON.null)  -- 3  true
print(JSON.stringify({ 1, JSON.null, 3 }))  -- [1,null,3]
```

> [!NOTE]
> `nil` values are also written as `null` when passed directly to [**`JSON.stringify()`**](#large_orange_diamond-method-jsonstringify), but a `nil` inside a table does not exist in Lua, so it cannot be written. Use `JSON.null` when the `null` must be kept.

<br>

### :diamond_shape_with_a_dot_inside: Table `options`

Options accepted by [**`JSON.stringify()`**](#large_orange_diamond-method-jsonstringify) and [**`JSON.save()`**](#large_orange_diamond-method-jsonsave).

| Field           | Type             | Default | Description                                                                                                  |
| :---            | :--:             | :--:    | :---                                                                                                         |
| `pretty`        | boolean          | `false` | Write one element per line                                                                                   |
| `indent`        | number \| string | `2`     | Indentation unit: a number of spaces (`0`-`16`) or a string made only of spaces and tabs                    |
| `json5`         | boolean          | `false` | Write keys that are valid identifiers without quotes                                                         |
| `escapeUnicode` | boolean          | `false` | Escape every non-ASCII character as `\uXXXX` (ASCII-only output)                                             |

> [!NOTE]
> Setting `indent` turns `pretty` on, unless `pretty = false` is given explicitly.

<br>


### :large_orange_diamond: Method `JSON.parse()`

Parse JSON or JSON5 text and return the result as a Lua table.<br>
Objects become tables with string keys, arrays become sequences, `null` becomes [**`JSON.null`**](#diamond_shape_with_a_dot_inside-property-jsonnull).

> [!IMPORTANT]
> The root of the document must be an object or an array. Scalar roots such as `42` are rejected with `invalid root`.

> [!NOTE]
> On invalid input the function does not throw an error: it returns `nil` followed by a message with the line and column of the problem.

```lua
-- @param (string) text - Document text (UTF-8)
-- @return (table) The decoded root value
-- @return (nil, string) On failure: nil and an error message
local value, err = JSON.parse(text)

local cfg, err = JSON.parse('{ name: "Rain", list: [1, 2, 3] }')

if not cfg then
  print(err)  -- e.g. "comma expected at line 1, column 7"
end
```

<br>


### :large_orange_diamond: Method `JSON.stringify()`

Convert a Lua value to JSON text.<br>
Tables whose keys are exactly `1..n` are written as arrays, any other table is written as an object. Keys are always written in sorted order, so the same data always produces the same text.

> [!IMPORTANT]
> Writing fails (returns `nil, message`) for `NaN`, `Infinity`, functions, threads, userdata (except [**`JSON.null`**](#diamond_shape_with_a_dot_inside-property-jsonnull)), invalid UTF-8 and circular references.

> [!NOTE]
> An empty table is written as `{}`. Use [**`JSON.array()`**](#large_orange_diamond-method-jsonarray) to write it as `[]`.<br>
> Comments are never written. A file read with comments and saved back loses them.

```lua
-- @usage JSON.stringify(value [, options])
-- @param (table) value - Lua table
-- @param (table) [options] - Writer options (see Table `options`)
-- @return (nil|string) The JSON text
-- @return (nil|string) On failure: nil and an error message
local raw = JSON.stringify({ a = 1, b = { 1, 2 } })
print(raw) -- {"a":1,"b":[1,2]}

print(JSON.stringify({ name = "Rain", list = { 1, 2 } }, { indent = 4 }))
-- {
--     "list": [
--         1,
--         2
--     ],
--     "name": "Rain"
-- }

print(JSON.stringify({ alpha = 1, ["my-key"] = 2 }, { json5 = true }))
-- {alpha:1,"my-key":2}

print(JSON.stringify("café", { escapeUnicode = true }))
-- "café"
```

<br>


### :large_orange_diamond: Method `JSON.load()`

Read and parse a **JSON**/**JSON5** file.<br>
[**Rainmeter Variables**](https://docs.rainmeter.net/manual/variables/nesting-variables/) (`#@#`, `#CURRENTPATH#`, ...) are expanded in the path. A UTF-8 byte order mark is ignored.

> [!NOTE]
> Failures (file not found, invalid content) are returned as `nil, message` and are also written to the [**Rainmeter Log**](https://docs.rainmeter.net/manual/user-interface/about/#LogTab).

```lua
-- @param (string) path - File path (UTF-8)
-- @return (table) The decoded root value
-- @return (nil, string) On failure: nil and an error message
local cfg, err = JSON.load("#@#settings.json")
```

<br>


### :large_orange_diamond: Method `JSON.save()`

Convert a value to JSON and write it to a file.<br>
A trailing newline is added. Missing directories are created automatically and **Rainmeter** variables are expanded in the path.

> [!IMPORTANT]
> The file is written to a temporary file first and then renamed over the destination, so an interrupted write never leaves a truncated file behind.

> [!NOTE]
> Failures are returned as `nil, message` and are also written to the [**Rainmeter Log**](https://docs.rainmeter.net/manual/user-interface/about/#LogTab). Nothing is written if the value cannot be converted.

```lua
-- @param (string) path - File path (UTF-8)
-- @param (any) value - Value to write
-- @param (table) [options] - Writer options, same as JSON.stringify()
-- @return (boolean) true on success
-- @return (nil, string) On failure: nil and an error message
JSON.save("#@#settings.json", { width = 100 }, { pretty = true })
```

<br>


### :large_orange_diamond: Method `JSON.array()`

Mark a table so it is always written as a JSON array.<br>
This is useful for empty tables, which would otherwise be written as `{}`. Called without arguments, it creates a new marked table.

> [!NOTE]
> An empty array read by [**`JSON.parse()`**](#large_orange_diamond-method-jsonparse) is already marked, so it is written back as `[]`.<br>
> An error is raised if the table already has an unrelated metatable.

```lua
-- @usage JSON.array([array])
-- @param (table) [array] - Table to mark
-- @return (table) The same table, or a new one
local raw = JSON.stringify({ items = JSON.array() })

print(raw) -- {"items":[]}
```

<br>


### :large_orange_diamond: Method `JSON.object()`

Mark a table so it is always written as a JSON object.<br>
Useful when a table with keys `1..n` must stay an object. Called without arguments, it creates a new marked table.

```lua
-- @usage JSON.object([object])
-- @param (table) [object] - Table to mark
-- @return (table) The same table, or a new one
local raw = JSON.stringify(JSON.object({ "a", "b" }))

print(raw) -- {"1":"a","2":"b"}
```

---

<br>
<br>


## :warning: Behavior and Limits

- The text must be valid UTF-8. A UTF-8 byte order mark at the start is ignored.
- Nesting is limited to **200 levels** when reading and writing (this also stops circular tables).
- Numbers are doubles: integers above 2<sup>53</sup> lose precision. Numbers are written in their shortest exact form.
- Duplicate keys in a document: the last value wins.
- The reader is lenient: text after the end of the root value is ignored.
- Not supported by the reader: hexadecimal numbers (`0x10`), `Infinity` and `NaN`.
- A `\u0000` escape inside a string truncates that string.
- Bad data never raises a Lua error. Wrong argument types (for example, `JSON.parse({})`) do, as in any Lua function.

---

<br>
<br>


## :scroll: License

Licensed under the **GPL v2.0 License**.

The reader uses [**JSON5**](https://github.com/P-i-N/json5), developed [**by P-i-N**](https://github.com/P-i-N) and licensed under the **MIT License - Copyright (c) 2020 P-i-N**.<br>
Two minor modifications have been made (`null` is read as `null` instead of `0`, and quoted keys now accept any characters).<br>
The original copyright and permission notice has been preserved.

<br>
<br>


<div align="center">
  For more information, visit the <b><a href="../../README.md">RainJIT documentation</a></b>
</div>

<br>
<br>
