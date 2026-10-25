# json

JSON / JSON5 module for RainJIT (Lua 5.1 / LuaJIT).

License: GPL v2.0. Bundled third-party code: see `THIRD_PARTY_LICENSES.md`.

## Files

| File | Purpose |
| --- | --- |
| `json.hpp` / `json.cpp` | C++ module and Lua bindings |
| `json.lua` | API reference (LDoc stub, not loaded at runtime) |
| `third_party/json5/` | json5 reader (MIT) with two documented fixes |
| `THIRD_PARTY_LICENSES.md` | MIT notice for json5 |

## Integration

1. Add the files to the project and add `third_party` to the include path
   (so that `#include <json5/json5_input.hpp>` resolves).
2. Build with C++17 or newer.
3. Register the module next to the others:

```cpp
json::RegisterModule( L, rain );
```

4. Use it from Lua:

```lua
local json = require("json")

local cfg = json.load("#@#settings.json") or {}
cfg.width = 100
json.save("#@#settings.json", cfg, { pretty = true })
```

## Behavior notes

- Reading accepts JSON and JSON5 (comments, unquoted keys, single quotes,
  trailing commas). The root must be an object or an array.
- Input must be valid UTF-8; a UTF-8 BOM is ignored.
- JSON `null` is `json.null` (never `nil`), so arrays keep their length.
- Empty arrays and objects round-trip; use `json.array()` / `json.object()` to
  force the type of a table.
- Output is deterministic (sorted keys) and numbers keep full precision.
- Nesting is limited to 200 levels (also protects against circular tables).
- Numbers are doubles: integers above 2^53 lose precision. Hexadecimal literals
  (`0x10`) are not supported by the reader.
- A `\u0000` escape inside a string truncates that string (json5 limitation).
- Duplicate keys in a document: the last value wins when decoded into a table.
- Functions never raise Lua errors on bad data; they return `nil, message`.
  Argument type errors (wrong parameter types) do raise, as usual.
