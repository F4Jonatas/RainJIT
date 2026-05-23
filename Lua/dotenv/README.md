# module dotenv

A simple `.env` file parser and manager for **Lua** and **LuaJIT**.

This library provides an **instance-based API** for loading, reading,
modifying and saving environment-style key-value files.

It is intentionally minimal and predictable.

---

## Features

- Instance-based API (no global state)
- Simple `.env` key-value parsing
- Preserves empty string values (`KEY=` → `""`)
- Supports reading, writing and updating values
- LuaJIT compatible
- No dependencies

---

## Installation

Copy `dotenv.lua` into your project and require it normally:

```lua
local env = require("dotenv")
```


## Basic Usage

### Load a `.env` file
This immediately parses the file and loads all variables into memory.

```lua
local env = require("dotenv")
local c = env(".env")
```

### Get a value
If the key exists with an empty value (`KEY=`),
the empty string (`""`) is returned.

```lua
local host = c:get("DB_HOST")

-- With a default value
local port = c:get("DB_PORT", "5432")
```


### Set a value
All values are stored as strings.

```lua
c:set("APP_ENV", "production")
c:set("EMPTY_VALUE", "")
```


### Check if a key exists

```lua
if c:hasKey("API_KEY") then
  print("API_KEY is defined")
end
```


### List all keys
Returns an table of all loaded keys.

```lua
local keys = c:keys()

for _, key in ipairs(keys) do
  print(key)
end
```


### Save changes to file

- Overwrites the file completely
- Writes KEY=value per line
- Does not preserve comments or order

```lua
local ok = c:save()

if not ok then
  error("Failed to save .env file")
end
```

### Releasing memory
There is no `close()` method.
To release the instance and allow Lua's garbage collector to free memory

```lua
c = nil
```


## File Format
Supported format:

```lua
KEY=value
EMPTY=
FOO="bar"
BAZ='qux'
```

Unsupported / ignored:
- Comments
- Inline comments
- `export KEY=value`
- Variable expansion
These may be added in future versions if desired.


## Design Philosophy
This library intentionally avoids complex parsing rules.
It focuses on:
- Predictability
- Explicit behavior
- Minimal overhead
- Lua idioms
If you need full `.env` compatibility, consider extending the parser.


## License
MIT