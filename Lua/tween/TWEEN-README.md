# tween.lua
A lightweight and flexible tweening module for Lua.

This module interpolates numeric values or nested tables of numbers
over time using a wide variety of easing functions.

<br>
<br>


## Features
- Supports numbers and nested tables
- Over 40 easing functions
- Time-based updates
- Zero dependencies
- Works with pure Lua
- Inspired by kikito/tween.lua

<br>
<br>


## Basic usage
### 1. Tweening a single number:
When the tween finishes, update() will keep returning the final value.
```lua
local tween = require('tween')
local t = tween(1.0, 0, 100, 'linear')
-- In your update loop
local value = t:update(dt)
```

<br>


### Using easing functions
You can specify easing by name:
```lua
tween(1.0, 0, 100, "outquad")
```
Or pass a function directly:
```lua
tween(1.0, 0, 100, tween.easing.outbounce)
```

<br>

### Available easing families include:
- linear
- quad, cubic, quart, quint
- sine, expo, circ
- elastic, back, bounce
- in / out / inout / outin variants

<br>
<br>


## Tweening tables
### Tweening structured data:
```lua
local position = { x = 0, y = 0 }

local t = tween(
  2.0,
  position,
  { x = 100, y = 50 },
  "inoutcubic"
)

t:update(dt)
print(position.x, position.y)
```

<br>

### Nested tables are fully supported:
```lua
local state = {
  transform = {
    x = 0,
    y = 0
  },
  alpha = 1
}

local t = tween(
  1.5,
  state,
  {
    transform = { x = 200, y = 100 },
    alpha = 0
  },
  "outsine"
)
```

<br>

### Resetting and manual control
#### Reset the tween:
```lua
t:reset()
```


#### Set an absolute time manually:
```lua
t:set(0.5)
```

### IMPORTANT NOTES
- The "before" table is mutated in place.
- If you need immutable behavior, pass a copy of your data.
- Duration of 0 is internally clamped to 1.


# LICENSE
MIT License.<br>
Original concept by kikito, extended by ramake.
