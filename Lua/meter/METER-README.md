
<div align="center">

  # Meter Module

</div>



## Overview

Programmatic, object-oriented control of Rainmeter meters.

This module acts as a factory: it detects the meter type declared in the
Rainmeter skin (`String`, `Image`, `Shape`, ...) and returns an instance
with a common base API — positioning, dimensions, visibility, generic
option access, transformations, and mouse events.

For meter types that need richer, type-specific behavior, the factory
automatically loads and attaches a dedicated submodule (e.g. `meter.shape`),
extending the base instance with additional methods while keeping the
same chainable interface. Additional meter types can be supported the
same way, without changing how the module is consumed.

<br>


## Instance Creation

```lua
local meter = require("meter")
local clock = meter("ClockMeter")

clock:left(100)
clock:top(50)
clock:show()
```

<br>
<br>


## Visibility
```lua
meter:show()     -- Shows the meter
meter:hide()     -- Hides the meter
meter:update()   -- Updates the meter
meter:update(true) -- Forces a redraw before updating
```

<br>
<br>


## Positioning
```lua
-- Getter/Setter
local x = meter:left()     -- Gets X position
meter:left( 100 )          -- Sets X position

local y = meter:top()      -- Gets Y position
meter:top( 50 ):update()   -- Sets Y position and updates

-- Method chaining
meter:left( 100 ):top( 50 ):update()

-- In Lua, convert string to number when necessary
local numX = tonumber( meter:left() ) or 0
```

<br>
<br>


## Dimensions
```lua
-- Width
local width = meter:width()      -- Gets width
meter:width( 300 ):update()      -- Sets width

-- Height
local height = meter:height()    -- Gets height
meter:height( 200 )              -- Sets height

-- Full method chaining
meter:left( 100 ):top( 50 ):width( 300 ):height( 200 ):update()
```

<br>
<br>


## Transformations
```lua
-- Uniform scale, applied from the meter's center
meter:scale( 1.5 )
local factor = meter:scale()   -- Gets current scale factor

-- Translation (composes with scale via a shared transformation matrix)
meter:translatex( 10 )
meter:translatey( -5 )

local tx = meter:translatex()  -- Gets current X offset (default 0)
local ty = meter:translatey()  -- Gets current Y offset (default 0)

-- Scale and translate together
meter:scale( 1.2 ):translatex( 20 ):translatey( 10 )
```

<br>
<br>


## Generic Options
```lua
-- Getter: returns the option value
local text = meter:option( 'Text' )
local font = meter:option( 'FontFace' )

-- Setter: returns self for chaining
meter:option( 'Text', 'Hello World' )
  :option( 'FontSize', 12 )
  :option( 'FontColor', '255,255,255' )
  :update()

-- Special options
meter:option( 'SolidColor', '0,0,0,150' )
  :option( 'AntiAlias', 1 )
```

<br>
<br>


## Mouse Events
```lua
-- Register an event
meter:event( 'leftup', function( self, event )
  print( event.type )
end )

-- Multiple events with one callback
meter:event( 'over leave', function( self, event )
  print( event.type )
end )

-- Manually trigger an already-registered event
meter:event( 'leftup' )

-- Remove an event
meter:event( 'leftup', false )
```

<br>
<br>


## Meter Properties
```lua
-- Meter name (read-only)
print( meter.name )

-- Meter type (read-only)
print( meter.type )  -- string, image, shape, etc.
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
