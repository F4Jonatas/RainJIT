
<div align="center">

  # Meter Shape Module

</div>



## Overview

A builder-style, chainable API for constructing and manipulating Rainmeter
**Shape meters** programmatically.

Rainmeter Shape meters are normally configured through dense, hard-to-read
option strings, e.g.:
Shape=Rectangle 0,0,100,50 | Fill Color 255,0,0 | StrokeWidth 2

This module wraps that syntax behind a Lua DSL, so the same shape can be
built through readable, chainable method calls instead of hand-written
strings:

```lua
local meter = require("meter")
local shape = meter("Background")

SHAPE:RECTANGLE(0, 0, 200, 100)
  :fill(40, 40, 40)
  :strokecolor(255, 255, 255)
  :strokewidth(2)
  :update()
```

It is loaded automatically by the base `meter` factory whenever a meter
with `Meter=Shape` is detected — there is no need to `require` it directly
in most cases.

<br>
<br>


## Features

- **Chainable builder API** — every setter returns the shape instance,
  allowing fluent construction (`shape:rectangle(...):fill(...):update()`).
- **Multiple shape entries per meter** — manage `Shape`, `Shape2`, `Shape3`...
  through `add()`, `shape()`, and `length()`.
- **Geometric primitives** — `rectangle`, `ellipse`, `path`, `polygon`, `polyline`.
- **SVG-like path syntax** — `path()` accepts simplified SVG-style commands
  (`M`, `L`, `V`, `H`, `C`/`Q`, `A`, `Z`) and converts them to Rainmeter's
  `LineTo` / `CurveTo` / `ArcTo` / `ClosePath` segments.
- **Stroke configuration** — color, width, line join, start cap, dash pattern,
  dash cap, dash offset.
- **Fill configuration** — solid colors (RGB/RGBA/hex/short-hex/`transparent`),
  linear gradients, and radial gradients.
- **Transformations** — `scale()`, with optional anchor point.
- **Getter/setter symmetry** — most methods act as a getter when called
  without arguments, and as a setter otherwise.

<br>
<br>


## Shape Instances

```lua
local meter = require("meter")
local shape = meter("Graph")

-- Base "Shape"
shape:rectangle(0, 0, 200, 50):fill(20, 20, 20)

-- Adds "Shape2"
local shape2 = shape:add()
shape2:ellipse(100, 25, 30, 30):fill(255, 0, 0)

-- Retrieve an existing shape by index (1 = base Shape)
local existing = shape:shape(2)

-- Count how many active shape entries exist
local total = shape:length()
```

<br>
<br>


## Primitives

### Rectangle
```lua
shape:rectangle(0, 0, 200, 100)              -- x, y, width, height
shape:rectangle(0, 0, 200, 100, 10, 10)      -- with rounded corners (radiusX, radiusY)
shape:rectangle("10%", "10%", "80%", "50%")  -- percentage values, resolved against the meter's size

local current = shape:rectangle()  -- getter
```

### Ellipse
```lua
shape:ellipse(100, 50, 40, 40)  -- centerX, centerY, radiusX, radiusY

local current = shape:ellipse()  -- getter
```

### Path
```lua
shape:path("0,0 L 100,0 L 100,50 Z")

-- Supported commands: M (move), L (line), V (vertical line),
-- H (horizontal line), C/Q (curve), A (arc), Z (close path)
```

### Polygon / Polyline
```lua
shape:polyline({0,0, 50,20, 100,0})          -- open line
shape:polygon("0,0 50,20 100,0")             -- automatically closed
```

<br>
<br>


## Stroke
```lua
shape:strokecolor(255, 255, 255)
shape:strokewidth(2)
shape:strokelinejoin("round")        -- "miter" | "bevel" | "round"
shape:strokelinejoin("miter", 10.0)  -- with miter limit
shape:strokestartcap("round")        -- "round" | "square" | "butt"
shape:strokedashes(4, 2)             -- dash length, gap length
shape:strokedashcap("round")
shape:strokedashoffset(10)
```

<br>
<br>


## Fill
```lua
-- Solid color
shape:fill(255, 0, 0)          -- RGB
shape:fill(255, 0, 0, 120)     -- RGBA
shape:fill("FF0000")           -- hex
shape:fill("f00")              -- short hex
shape:fill("transparent")

-- Linear gradient
shape:lgradient(90, "0 255,0,0", "1.0 0,0,255,250")

-- Radial gradient
shape:rgradient(50, 50, 40, "255,0,0;0", "0,0,255;1")
```

<br>
<br>


## Transformations
```lua
shape:scale(1.5)                 -- uniform scale
shape:scale(1.5, 1.5, 50)        -- with anchor X
shape:scale(1.5, 1.5, 50, 50)    -- with anchor X and Y

local sx, sy = shape:scale()     -- getter, defaults to "1,1"
```

<br>
<br>


## Type Conversion
```lua
-- Converts the shape's current primitive while keeping other
-- parameters (fill, stroke, etc.) intact.
shape:changeType("ellipse")
```

<br>
<br>


## Updating

Shape changes are written to the meter's option immediately on each call,
but the meter itself must still be told to redraw:

```lua
shape:rectangle(0, 0, 100, 50):fill(255, 0, 0):update()
```

<br>
<br>


## Limitations

- **`gradient()` is non-functional.** It references an undefined variable
  (`attr`) and can throw when called without a `modifier` argument. Use
  `lgradient()` / `rgradient()` directly instead.
- **`line()` is a stub.** It only prints the matched value and returns
  `self` — it does not define or persist a line shape.
- **`strokelinejoin()` getter can error if never set.** Calling it as a
  getter before any `strokelinejoin` value has been defined will fail,
  unlike other getters in this module which return a default.
- **`rgradient()` has a malformed removal pattern** for the previous solid
  fill color, which may leave stale `fill color` data in place when
  switching a shape from solid fill to radial gradient.
- **`ellipse()` calls `option()` with an extra argument** compared to
  every other setter in this module; verify this matches your `meter:option()`
  signature if you rely on `ellipse()`.
- **No validation of malformed shape content.** `changeType()` and several
  parsers assume the internal content string matches expected patterns;
  unexpected formatting can raise a Lua error rather than failing gracefully.
- **Internal path/gradient counters (`paths`, `gradient`) are module-level,
  not per-meter.** Numbering (`paths1`, `gradient1`, ...) is shared across
  all shapes and all meters using this module within the same script scope.
- **State is string-based.** Shape data is stored and mutated as Rainmeter
  option strings (parsed via Lua patterns), not as structured data — highly
  unusual values (extra whitespace, unexpected decimal formats, negative
  coordinates in atypical positions) may parse incorrectly without raising
  an error.
- **`arc` and `curve` primitives are not implemented** as dedicated methods
  (only referenced in comments); use `path()` with the equivalent SVG-style
  commands instead.

<br>
<br>


## Meter Properties
```lua
-- Meter name (read-only)
print(shape.name)

-- Current shape type (read-only, e.g. "rectangle", "ellipse", "path")
print(shape.type)
```

---

<br>
<br>

## :scroll: License

<a href="../../assets/images/logo-gpl-v2.png">
  <img src="../../assets/images/logo-gpl-v2.png" alt="LOGO-GPL-V2" width="150" height="150" align="right">
</a>

The **RainJIT** Plugin is licensed under the [**GPL v2.0 license**](../../LICENSE).<br>
This project also relies on external libraries that may use different open-source licenses.<br>
If you are contributing documentation or changes to the source code, please ensure that your contributions comply with the project's licensing guidelines.

<br>

---

<br>
<br>
