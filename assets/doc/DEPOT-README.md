
<div align="center">

  # Depot Module for RainJIT

  ### A lightweight, intelligent data persistence system for Rainmeter skins using native Windows INI files

  <br>
  <br>

</div>


## Overview
The Depot module is a sophisticated data persistence system for RainJIT that transcends simple INI file manipulation. Built on a modern C++ foundation with RAII design principles, it provides Lua scripts with seamless access to Windows-native configuration storage through an intelligent abstraction layer.<br>
The module doesn't load entire files into memory – it maintains only the instance handle for continuous data manipulation.

> 🚀 Future Plans:
> - Password-protected encryption for enhanced security
> - Data handling without physical files

<br>
<br>


## :green_book: Features

- **Intelligent type conversion** – Strings automatically convert to appropriate Lua types (numbers, booleans, strings)
- **Thread-safe operations** – Safe for concurrent access
- **Automatic garbage collection** – Seamless integration with Lua's memory management
- **Automatic directory creation** – Creates parent directories if they don't exist
- **Zero file loading** – No memory overhead from reading entire files
- **UTF-8/UTF-16 handling** – Automatic conversion between Lua strings and Windows API

---

<br>
<br>


## :book: Usage

### :large_orange_diamond: Method `depot()`

Creates a new **Depot** instance bound to a specific section and file.

```lua
-- @usage depot([ section [, filepath ]]) → userdata
-- @param (string) section - INI file section name. Default: "root"
-- @param (string) filepath - Path to INI file. Default: "#@#depot\#CURRENTCONFIG#\main.depot"
-- @return (userdata) - A Depot instance

-- Simplest form (uses defaults)
local dp = depot()

-- Custom section only
local config = depot("Settings")

-- Custom section and file
local data = depot("UserData", "#@#storage\\data.ini")

-- Absolute path
local secure = depot("Secrets", "C:\\MyData\\secrets.ini")
```

<br>


### :large_orange_diamond: Method `depot:set()`

Writes a value to the file.

```lua
-- @usage depot:set( key, value ) → userdata
-- @param (string) key - Key name
-- @param (any) value - Any Lua value (automatically converted to string)
-- @return (userdata) - The instance itself (for method chaining)

-- Basic string storage
dp:set("username", "JohnDoe")

-- Automatic type conversion (all stored as strings)
dp:set("counter", 42)    -- Stored as "42"
dp:set("active", true)   -- Stored as "true"
dp:set("data", {1,2,3})  -- Stored as "table: 0x..."

-- Method chaining
dp:set("first", "value")
  :set("second", 123)
  :set("third", false)
```

<br>


### :large_orange_diamond: Method `depot:get()`

Retrieves a value from the file with automatic type conversion.<br>
When `raw = true`, the value is returned exactly as stored (as a string), without any conversion.

> [!NOTE]
> | Stored Value         | Lua Type  | Example        |
> | :--                  | :--:      | --:            |
> | `"true"`/`"false"`   | `boolean` | `true`/`false` |
> | Numeric with decimal | `number`  | `3.14`         |
> | Numeric integer      | `integer` | `42`           |
> | Everything else      | `string`  | `"hello"`      |


```lua
-- @usage depot:get( key [, default ]) → any
-- @param (string) key - Key name
-- @param (any) default - Default value if key doesn't exist
-- @param (boolean) raw - Returns the value without conversion.
-- @return (any) - The converted value, default if provided, or nil

-- Basic retrieval
local name = dp:get("username")

-- With default value
local port = dp:get("port", 8080)

-- Automatic type conversion
local count   = dp:get("counter")    -- Returns as number
local enabled = dp:get("active")     -- Returns as boolean
local label   = dp:get("label")      -- Returns as string
local value   = dp:get("not_exists") -- Returns as nil

-- Raw retrieval (preserves original string)
dp:set("code", "001")
print(dp:get("code"))               -- 1 (number)
print(dp:get("code", nil, true))    -- "001" (string)
```

<br>


### :large_orange_diamond: Method `depot:has()`

Checks whether a key exists in the section.

```lua
-- @usage depot:has( key ) → boolean
-- @param (string) key - Key name
-- @return (boolean) - true if the key exists, false otherwise

if dp:has("username") then
  print("Welcome back, " .. dp:get("username"))
end
```

<br>


### :large_orange_diamond: Method `depot:remove()`

Deletes a specific key from the section.

```lua
-- @usage depot:remove( key ) → nil
-- @param (string) key - Key name
-- @return (boolean) - Nothing

dp:remove("username")
```

<br>


### :large_orange_diamond: Method `depot:clear()`

Deletes the entire section and all its keys from the file.

> [!WARNING]
> This operation cannot be undone.

```lua
-- @usage depot:clear() → nil
-- @return (boolean) - Nothing

dp:clear()  -- Removes all keys from the section
```

<br>


### :large_orange_diamond: Method `depot:keys()`

Returns a list of all keys in the current section.

```lua
-- @usage depot:keys() → table
-- @return (table) - A table (array) of key names

local keys = dp:keys()
for i, key in ipairs(keys) do
  print(i .. ": " .. key)
end
```

<br>


### :large_orange_diamond: Method `depot:values()`

Returns a list of all values in the current section (with type conversion).

```lua
-- @usage depot:values() → table
-- @return (table) - A table (array) of values

local values = dp:values()
for i, value in ipairs(values) do
  print(i .. ": " .. tostring(value))
end
```

<br>


### :large_orange_diamond: Method `depot:name()`

Returns the section name this instance is bound to.

```lua
-- @usage depot:name() → string
-- @return (string) - Section name

print("Working with section: " .. dp:name())
```

<br>


### :large_orange_diamond: Method `depot:filePath()`

Returns the full path to the file.

```lua
-- @usage depot:filePath() → string
-- @return (string) - File path as string

print("Data stored in: " .. dp:filePath())
```

<br>


### :large_orange_diamond: Method `depot:delete()`

Permanently deletes the file from disk.

> [!WARNING]
> This operation cannot be undone. All data will be lost.

```lua
-- @usage depot:delete() → boolean
-- @return (boolean) - true on success, false + error code on failure

local success, err = dp:delete()
if success then
  print("File deleted successfully")
else
  print("Failed to delete file. Error code: " .. err)
end
```
---

<br>
<br>


## :zap: Best Practices

- **Read operations** – O(1) with small overhead for type detection
- **Write operations** – O(1) but file I/O is relatively slow
- **Key enumeration** – Scans entire section (use sparingly)
- **Type detection** – Minimal overhead, caches nothing
- **Memory usage** – Only the instance handle (a few bytes)

### ✅ DO
- Use meaningful section and key names
- Provide default values when reading
- Remove unused keys with :remove()
- Leverage automatic type conversion
- Use method chaining for multiple sets

### ❌ DON'T
- Store binary data directly (use encoding)
- Rely on key order (INI files don't guarantee order)
- Use extremely long keys (>255 characters)
- Perform thousands of writes in loops (consider caching)

---

<br>
<br>


## :warning: Limitations


| Limitation   | Description                                                         | Workaround                      |
| :--          | :--:                                                                | --:                             |
| Value length | Maximum 4096 characters per value                                   | Split data across multiple keys |
| Section size | No practical limit, but performance degrades with thousands of keys | Use multiple files              |
| Binary data  | INI files are text-based                                            | Use base64 encoding             |
| Concurrent   | access	Windows INI functions aren't thread-safe for writing         | Implement your own locking      |

---

<br>
<br>


## Troubleshooting

### File not found
- Verify directory permissions
- Check if path contains invalid characters
- Ensure parent directories exist (Depot creates them automatically)

### Values truncated
- Data exceeds 4096 character limit
- Consider compression or splitting across multiple keys

### Encoding issues
- Ensure all strings are valid UTF-8
- Special characters must be properly encoded

### Performance problems
- Too many write operations in loops
- Consider caching frequently accessed values
- Batch related writes together

### Type conversion not working
- Values must exactly match patterns: `"true"`, `"false"`, numbers without extra spaces
- Leading/trailing whitespace will result in string type

---

<br>
<br>


<div align="center">
  <b>Depot makes data persistence in Rainmeter as easy as working with Lua tables, but with the durability of disk storage.</b>
  <br>
  For more information, visit the <b><a href="../../README.md">RainJIT documentation</a></b>
</div>

<br>
<br>

