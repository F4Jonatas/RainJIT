--- JSON / JSON5 module API reference (documentation stub).
--
-- This file is NOT loaded at runtime. It only documents the API that the
-- native module (json.cpp) registers through `require("json")`, in LDoc style.
--
-- @module json
-- @license GPL v2.0 License
-- @usage
-- local json = require("json")
-- local cfg = json.decode('{ width: 100, tags: ["a", "b"], }')
-- print(json.encode(cfg, { pretty = true }))

local json = {}



--- Sentinel that represents JSON `null`.
-- It is a light userdata (NULL pointer), so `nil` never appears inside
-- arrays and `[1, null, 3]` keeps its length of 3. Compare with `==`.
-- @field null
-- @usage
-- local t = json.decode('[1, null, 3]')
-- print(#t, t[2] == json.null)  --> 3  true
json.null = nil



--- Encode options.
-- @table EncodeOptions
-- @field pretty (boolean) Write one element per line. Default `false`.
-- @field indent (number|string) Indentation unit: a number of spaces (0-16) or a
--   string made only of spaces and tabs. Default `2`. Setting it turns `pretty`
--   on unless `pretty = false` is given explicitly.
-- @field json5 (boolean) Write keys that are valid identifiers without quotes.
--   Default `false` (strict JSON).
-- @field escapeUnicode (boolean) Escape every non-ASCII character as `\uXXXX`
--   (ASCII-only output). Default `false`.



--- Parse JSON or JSON5 text.
-- Accepts comments, unquoted and single-quoted keys/strings, trailing commas
-- and a leading UTF-8 BOM. The root must be an object or
-- an array, the text must be valid UTF-8 and nesting is limited to 200 levels.
-- JSON `null` becomes @{json.null}; empty arrays and objects are marked so
-- they survive a round trip.
-- @function decode
-- @tparam string text Document text (UTF-8).
-- @treturn table The decoded root value.
-- @treturn[2] nil On failure.
-- @treturn[2] string Error message with line and column.
-- @usage
-- local cfg, err = json.decode('{ name: "Rain", list: [1, 2, 3] }')
-- if not cfg then print(err) end
function json.decode(text) end



--- Convert a Lua value to JSON text.
-- Tables whose keys are exactly 1..n are written as arrays, other tables as
-- objects. Keys are written in sorted order, so the output is deterministic.
-- Numbers keep full precision; `NaN`, `Infinity`, functions, threads,
-- userdata (except @{json.null}), invalid UTF-8 and circular references are
-- reported as errors.
-- @function encode
-- @param value Any Lua value.
-- @tparam[opt] EncodeOptions options Writer options.
-- @treturn string The JSON text.
-- @treturn[2] nil On failure.
-- @treturn[2] string Error message.
-- @usage
-- print(json.encode({ a = 1, b = { 1, 2 } }))  --> {"a":1,"b":[1,2]}
-- print(json.encode({ a = 1 }, { indent = 4 }))
function json.encode(value, options) end



--- Read and decode a JSON/JSON5 file.
-- Rainmeter variables such as `#@#` are expanded in the path. Failures are
-- also written to the Rainmeter log.
-- @function load
-- @tparam string path File path (UTF-8).
-- @treturn table The decoded root value.
-- @treturn[2] nil On failure.
-- @treturn[2] string Error message.
-- @usage
-- local cfg, err = json.load("#@#settings.json")
function json.load(path) end



--- Encode a value and write it to a file.
-- A trailing newline is added. Missing directories are created, Rainmeter
-- variables are expanded and the file is replaced atomically (temporary file
-- plus rename), so an interrupted write never leaves a truncated file.
-- Failures are also written to the Rainmeter log.
-- @function save
-- @tparam string path File path (UTF-8).
-- @param value Value to write.
-- @tparam[opt] EncodeOptions options Same as @{json.encode}.
-- @treturn boolean `true` on success.
-- @treturn[2] nil On failure.
-- @treturn[2] string Error message.
-- @usage
-- json.save("#@#settings.json", { width = 100 }, { pretty = true })
function json.save(path, value, options) end



--- Mark a table so it is always written as a JSON array.
-- Useful for empty tables, which are otherwise written as `{}`.
-- Creates a new table when called without arguments. Raises an error if the
-- table already has an unrelated metatable.
-- @function array
-- @tparam[opt] table t Table to mark.
-- @treturn table The same table (or a new one).
-- @usage
-- print(json.encode({ items = json.array() }))  --> {"items":[]}
function json.array(t) end



--- Mark a table so it is always written as a JSON object.
-- Useful when a table with keys `1..n` must stay an object.
-- Creates a new table when called without arguments.
-- @function object
-- @tparam[opt] table t Table to mark.
-- @treturn table The same table (or a new one).
-- @usage
-- print(json.encode(json.object({ "a", "b" })))  --> {"1":"a","2":"b"}
function json.object(t) end



return json
