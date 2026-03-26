--- Tweening module for Lua.
-- 
-- A minimal and flexible interpolation engine focused on **data transformation over time**.
--
-- ## Concept
--
-- This module implements a **pure tweening core**, responsible only for:
--
-- - Interpolating values over time
-- - Supporting numbers, arrays, and nested tables
-- - Applying easing functions
--
-- It does **not** implement animation state machines (play, pause, etc.).
-- Instead, it is designed to be used as a **low-level building block**
-- for higher-level animation systems.
--
-- ## Data Model
--
-- All inputs are normalized into a unified structure:
--
-- - `number` → `{ value }`
-- - `array`  → `{ v1, v2, ... }`
-- - `table`  → `{ x=..., y=... }`
--
-- Internally, the tween operates on three distinct structures:
--
-- - `from`    → immutable start values
-- - `to`      → immutable target values
-- - `current` → mutable interpolated values
--
-- This separation ensures predictable behavior and avoids mutation side-effects.
--
-- ## Behavior
--
-- - Interpolation is **time-based** (`clock` / `duration`)
-- - Values are updated via `update(dt)` or `set(time)`
-- - The module performs **in-place updates on `current` only**
-- - Supports deeply nested structures
--
-- ## Example
--
-- ```lua
-- local tween = require("tween")
--
-- local t = tween(1.0, { x = 0 }, { x = 100 }, "outQuad")
--
-- for i = 1, 60 do
--     local value = t:update(1/60)
--     print(value.x)
-- end
-- ```
--
-- @module tween
-- @author F4Jonatas
-- @license MIT
-- @version 2.0.1




--- Tweening module for Lua.
-- Provides time-based interpolation (tweening) for numbers and nested tables
-- using a wide range of easing functions.
--
-- Inspired by kikito/tween.lua and extended with multiple easing equations.
--
--
-- Original File: https://github.com/kikito/tween.lua
-- Inspirate:
--   https://gist.github.com/okikio/bed53ed621cb7f60e9a8b1ef92897471
--   https://developer.roblox.com/en-us/articles/Bezier-curves
--   https://easings.net/
--   http://www.joelambert.co.uk/morf/
--   https://gist.github.com/jamesu/321207
--   https://github.com/tsuyoshiwada/css-keyframer
--   https://github.com/juliangarnier/anime/blob/master/anime.js
--   https://github.com/SitePen/dgrid/tree/master/util
--   http://elrumordelaluz.github.io/csshake/#classes
--   http://textillate.js.org
--   https://daniel-lundin.github.io/snabbt.js/
--   http://ricostacruz.com/jquery.transit/
--   https://www.styled-components.com
--   https://daneden.github.io/animate.css/
--
--   https://github.com/EsotericSoftware/spine-runtimes/blob/4.0/spine-lua/spine-lua/Animation.lua#L220-L269
--   https://love2d.org/forums/viewtopic.php?t=93791
--
--




local M   = {}
M.__index = M



--- Check if value is a number.
-- @tparam any v
-- @treturn boolean
--
local function isNumber(v)
	return type(v) == "number"
end


--- Normalize input into table form.
-- Numbers are converted to `{ value }`.
--
-- @tparam number|table v
-- @treturn table normalized value
-- @treturn boolean isNumber original type flag
--
local function normalize(v)
	if isNumber(v) then
		return { v }, true
	end
	return v, false
end


--- Deep clone a table.
--
-- @tparam table dest destination table
-- @tparam table src source table
-- @treturn table cloned table
--
local function clone(dest, src)
	for k, v in pairs(src) do
		if type(v) == "table" then
			dest[k] = clone({}, v)
		else
			dest[k] = v
		end
	end
	return dest
end


--- Validate structure compatibility between `from` and `to`.
--
-- Ensures all numeric fields in `to` exist in `from`.
--
-- @tparam table from
-- @tparam table to
-- @tparam[opt=""] string path
--
local function checkSubject(from, to, path)
	path = path or ""

	for k, v in pairs(to) do
		local t = type(v)

		if t == "number" then
			if type(from[k]) ~= "number" then
				error("Invalid field: " .. path .. "/" .. tostring(k))
			end

		elseif t == "table" then
			checkSubject(from[k], v, path .. "/" .. tostring(k))

		else
			error("Unsupported type at: " .. path .. "/" .. tostring(k))
		end
	end
end



--==================================================
-- Core interpolation
--==================================================

--- Internal recursive interpolation function.
--
-- @tparam table current mutable output
-- @tparam table to target values
-- @tparam table from initial values
-- @tparam number t current time
-- @tparam number d duration
-- @tparam function easing easing function
-- @treturn table current
--
local function calc(current, to, from, t, d, easing)
	for k, v in pairs(to) do
		if type(v) == "table" then
			calc(current[k], v, from[k], t, d, easing)
		else
			current[k] = easing(t, from[k], v - from[k], d)
		end
	end
	return current
end





--- Set tween to a specific time.
--
-- Clamps the time between `0` and `duration` and updates values accordingly.
--
-- @tparam number clock time position
-- @treturn number|table interpolated value
--
function M:set(clock)
	assert(type(clock) == "number", "clock must be number")

	self.clock = math.max(0, math.min(clock, self.duration))

	if self.clock == 0 then
		clone(self.current, self.from)

	elseif self.clock == self.duration then
		clone(self.current, self.to)

	else
		calc(self.current, self.to, self.from, self.clock, self.duration, self.easing)
	end

	if self.isNumber then
		return self.current[1]
	end

	return self.current
end



--- Advance the tween by delta time.
--
-- @tparam number dt delta time
-- @treturn number|table interpolated value
--
-- @usage
-- t:update(1/60)
--
function M:update(dt)
	return self:set(self.clock + dt)
end



--- Reset tween to initial state.
--
-- Equivalent to `set(0)`.
--
-- @treturn number|table initial value
--
function M:reset()
	return self:set(0)
end




--- Easing functions table.
-- Loaded from `tween.easing`.
--
local okEasing, easingModule = pcall( require, 'tween.easing' )
if not okEasing then
	print('[tween] ERROR: Failed to load module "tween.easing"\n[tween] Reason: '.. tostring( easingModule ))
else
	M.easing = easingModule
end


local okBezier, bezierModule = pcall( require, 'tween.cubicBezier' )
if not okBezier then
	print( '[tween] ERROR: Failed to load module "tween.cubicBezier"\n[tween] Reason: '.. tostring( bezierModule ))
else
	M.cubicBezier = bezierModule
end





--- Create a new tween.
--
-- @tparam number duration total duration
-- @tparam number|table from starting value(s)
-- @tparam number|table to target value(s)
-- @tparam[opt="linear"] string|function easing easing function
--
-- @treturn table tween instance
--
-- @usage
-- -- Number tween
-- local t = tween(1.0, 0, 100)
--
-- @usage
-- -- Array tween
-- local t = tween(1.0, {0, 10}, {100, 200})
--
-- @usage
-- -- Object tween
-- local t = tween(1.0, {x = 0, y = 0}, {x = 100, y = 50})
--
-- @usage
-- -- Nested table tween
-- local t = tween(1.0,
--   { pos = { x = 0, y = 0 } },
--   { pos = { x = 100, y = 50 } }
-- )
--
-- @usage
-- -- Custom easing function
-- local t = tween(1.0, 0, 100, function(t, b, c, d)
--   return c * (t / d) + b
-- end)
--
return function( duration, from, to, easing )
	assert( type( duration ) == 'number', 'duration must be number' )

	-- resolve easing
	easing = easing or 'linear'
	if type( easing ) == 'string' then
		easing = easing:lower()
		assert( M.easing[ easing ], 'invalid easing: '.. easing )
		easing = M.easing[ easing ]
	end

	-- normalize inputs
	local fromNorm, isNumber = normalize( from )
	local toNorm = normalize( to )

	assert( type( fromNorm ) == 'table', 'from must be table/number' )
	assert( type( toNorm )   == 'table', 'to must be table/number'   )

	-- validate structure
	checkSubject( fromNorm, toNorm )

	return setmetatable({
		duration = duration == 0 and 1 or duration,
		clock    = 0,

		from     = clone({}, fromNorm ),
		to       = clone({}, toNorm   ),
		current  = clone({}, fromNorm ),

		easing   = easing,
		isNumber = isNumber
	}, M )
end
