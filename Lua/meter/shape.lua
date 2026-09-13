--- Shape meter extension.
-- Provides a **builder-style API** for constructing and manipulating
-- Rainmeter **Shape meters** programmatically.
--
-- Rainmeter Shape meters normally require complex strings such as:
--   Shape=Rectangle 0,0,100,50 | Fill Color 255,0,0 | StrokeWidth 2
--
-- This submodule exposes a Lua DSL that allows building these shapes
-- through chained method calls, improving readability and maintainability.
--
-- The module is automatically instantiated by the main `meter` factory
-- whenever a meter with `Meter=Shape` is detected.
--
-- Each instance represents a specific shape entry:
--   Shape
--   Shape2
--   Shape3
--
-- The module maintains the shape definition internally and updates the
-- corresponding Rainmeter option via `!SetOption`.
--
-- Typical usage example:
--
-- @usage
-- local meter = require("meter")
--
-- local shape = meter("Background")
--
-- shape:rectangle(0, 0, 200, 100)
--   :fill(40, 40, 40)
--   :strokecolor(255, 255, 255)
--   :strokewidth(2)
--   :update()
--
-- Multiple shapes can be created using `add()`:
--
-- Supported operations include:
--   geometric primitives (rectangle, ellipse, path, polygon, polyline)
--   stroke configuration (width, color, join, caps)
--   fill configuration (solid colors, gradients, transparency)
--   shape transformations (scale, anchor)
--   gradient definitions
--
-- The API is designed to be **chainable** and integrates with the base
-- `meter` methods such as `update()`, `event()`, and `option()`.
--
-- @submodule meter.shape
-- @release 0.2.4
-- @author F4Jonatas
-- @license GPL v2.0 License
-- @see https://docs.rainmeter.net/manual/meters/shape/
--
-- @usage
-- local meter = require("meter")
--
-- local shape1 = meter("Graph")
-- shape1:rectangle(0,0,200,50):fill(20,20,20)
--
-- local shape2 = shape1:add()
-- shape2:ellipse(100,25,30,30):fill(255,0,0)
--


local M   = {}
M.__index = M


local DRAWS    = 0
local GRADIENT = 0


-- Lua Regex
local REGEX = {
	rectangle = 'rectangle[%s,%.%d]+',
	ellipse   = 'ellipse[%s,%.%d]+',
	curve     = '',
	line      = '',
	arc       = '',
	path      = 'path[%s%w]+'
}

local SHAPE_PARAM = '([%s,%d%.%(%)%+%*-]+)'
local REGEX_SHAPE = '^(%s*%w+%s*)'



--- Calculate a percentage of a given value.
-- Used to resolve percentage-based parameters (e.g. `"50%"`) into
-- absolute pixel values, rounded to two decimal places.
--
-- @param (number) value - Base value (e.g. meter width/height).
-- @param (number) percent - Percentage to apply (0-100).
-- @return (number) Resulting value, rounded to 2 decimal places.
--
-- @usage percentOf(200, 50) --> 100
local percentOf = function( value, percent )
	local multi = 10 ^ 2
	return math.floor(( value * percent / 100 ) * multi + 0.5 ) / multi
end




--- Copy all inherited methods from a "super" instance onto a new class.
-- Iterates over `super`'s metatable and wraps every method so calls are
-- delegated to `super`, while preserving chainability: if the wrapped
-- call succeeds, `class` itself is returned (for method chaining);
-- otherwise the original result is returned (useful for getters).
--
-- @param (table) class - Target table that will receive the wrapped methods.
-- @param (table) super - Source instance (e.g. the base `meter` object) to inherit from.
-- @return (table) The `class` table with inherited methods attached.
--
-- @usage local shape = clone( {}, meterInstance )
local function clone( class, super )
	for key, value in pairs( getmetatable( super )) do
		if type( value ) == 'function' then
			-- wrapper ignora o primeiro argumento (class) e chama super:method(...)
			class[ key ] = function( _, ... )
				local ok, result = pcall( super[ key ], super, ... )
				return ok and class or result
			end

		-- else
			-- campos simples (como id) são copiados diretamente
			-- class[key] = value
		end
	end

	return class
end


-- local clone = function( self, super )
-- 	self.id = super.id

-- 	self.option = function( self, option, value, config )
-- 		local ok, result = pcall( super.option, super, option, value, config )
-- 		return result
-- 	end

-- 	self.event = function( self, events, callback )
-- 		local ok, result = pcall( super.event, super, events, callback )
-- 		return result
-- 	end

-- 	self.update = function( self )
-- 		super:update()
-- 		return self
-- 	end

-- 	return self
-- end




--- Normalize color arguments into Rainmeter's comma-separated format.
-- Accepts either a single hex string (`"transparent"`, 3-digit or
-- 6-digit hex) or separate R, G, B, (A) numeric components, and
-- returns them pre-formatted with leading commas so they can be
-- concatenated directly into a `Fill`/`StrokeColor` option string.
--
-- @param (number|string) r - Red value, or a hex/"transparent" string.
-- @param (number) [g] - Green value (ignored when `r` is a string).
-- @param (number) [b] - Blue value (ignored when `r` is a string).
-- @param (number) [a] - Alpha value (ignored when `r` is a string).
-- @return (string) r - Red value or expanded hex.
-- @return (string) g - Green value, prefixed with `,` (or empty for hex).
-- @return (string) b - Blue value, prefixed with `,` (or empty for hex).
-- @return (string) a - Alpha value, prefixed with `,` (or empty if absent/hex).
--
-- @usage parseColor(255, 0, 0, 120)  --> "255", ",0", ",0", ",120"
-- @usage parseColor("f00")           --> "ff0000", "", "", ""
-- @usage parseColor("transparent")   --> "0,0,0,0", "", "", ""
local function parseColor( r, g, b, a )
	-- if hex or transparent
	if r and not g and not b and not a then
		g = ''
		b = ''
		a = ''

		if r == 'transparent' then
			r = '0,0,0,0'

		-- using hex color 3 digits
		elseif r:len() == 3 then
			r = r:sub( 1, 1 ):rep( 2 ) ..
			      r:sub( 2, 2 ):rep( 2 ) ..
			      r:sub( 3, 3 ):rep( 2 )
		end

	-- rgb/a
	else
		g = ','.. g
		b = ','.. b
		a = a and ','.. a or ''

	end

	return r, g, b, a
end





--- Construct a new Shape instance bound to a meter.
-- Builds a shape wrapper (inheriting the base `meter` methods via `clone`)
-- for a specific `Shape`/`Shape2`/`Shape3`... option. If the option does
-- not yet exist in the meter, it is initialized with a default rectangle
-- and a placeholder value, avoiding a Rainmeter error state. If it
-- already exists, its current type is detected from the stored content.
--
-- @param (table) super - The base meter instance (from `meter()` factory).
-- @param (string) name - The meter's name (unused directly, kept for reference).
-- @param (number|string) [index] - Numeric suffix for indexed shapes (`""`, `2`, `3`, ...).
-- @return (table) New Shape instance bound to the given option slot.
--
-- @usage local shape = construct( meterInstance, "Graph" )      -- Shape
-- @usage local shape2 = construct( meterInstance, "Graph", 2 )  -- Shape2
local function construct( super, name, index )
	local class = clone( {}, super )
	class.name = 'shape'.. ( index and index or '' )
	class.meter = super

	class.content = class.meter:option( class.name )
	if not class.content then
		class.content = 'rectangle 0,0,0,0|strokeWidth 0'
		class.meter:option( class.name, 'rectangle 0,0,0,0' )
		class.type = 'rectangle'

	else
		class.type = class.content:gsub( '(%w+).*', '%1' ):lower()

	end

	return setmetatable( class, M )
end




--- Create or modify a rectangle shape.
-- Defines the rectangle geometry for the current shape entry.
-- If called without arguments, returns the current rectangle parameters.
--
-- Percentage values (e.g. `"50%"`) are automatically resolved
-- relative to the parent meter dimensions.
--
-- @param (number|string) left   - Left coordinate
-- @param (number|string) top    - Top coordinate
-- @param (number|string) width  - Rectangle width
-- @param (number|string) height - Rectangle height
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage
-- shape:rectangle(0,0,200,100)
-- shape:rectangle("10%","10%","80%","50%")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Rectangle
function M:rectangle( left, top, width, height, radiusX, radiusY )
	local value = self.content:lower():match( 'rectangle%s*'.. SHAPE_PARAM )
	height = height and height or width
	radiusX = radiusX and radiusX or 0
	radiusY = radiusY and radiusY or radiusX

	if not left and not top and not width then
		local result = value:gsub( '%s*|', '' )
		return result
	end


	-- support for percentage
	if type( width ) == 'string' then
		width = width:gsub( '%%', '' )
		width = percentOf( self.meter:width(), tonumber( width ))
	end
	if type( height ) == 'string' then
		height = height:gsub( '%%', '' )
		height = percentOf( self.meter:height(), tonumber( height ))
	end


	if value then -- If exists, substitute.
		self.content = self.content:lower():gsub( '%s*rectangle%s*'.. SHAPE_PARAM ..'%s*',
			'rectangle '.. left ..','.. top ..','.. width ..','.. height ..','.. radiusX ..','.. radiusY
		)

	else -- Add/Change
		self:changeType( 'rectangle ' ..left.. ',' ..top.. ',' ..width.. ',' ..height.. ',' ..radiusX.. ',' ..radiusY )
	end

	self.meter:option( self.name, self.content )
	return self
end



--- Create or modify an ellipse shape.
-- Defines an ellipse primitive with center coordinates
-- and horizontal/vertical radii.
--
-- If called without parameters the current ellipse
-- definition is returned.
--
-- @param (number|string) x Center X coordinate.
-- @param (number|string) y Center Y coordinate.
-- @param (number|string) radiusX Horizontal radius.
-- @param (number|string) radiusY Vertical radius.
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage shape:ellipse(100,50,40,40)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Ellipse
function M:ellipse( left, top, radiusX, radiusY )
	local value = self.content:lower():match( 'ellipse%s*'.. SHAPE_PARAM )
	radiusY = radiusY and radiusY or radiusX

	if not left and not top and not radiusX and not radiusY then
		return value
	end


	if value then -- if exists, substitute.
		self.content = self.content:lower():gsub(
			'%s*ellipse%s*'.. SHAPE_PARAM ..'%s*',
			'ellipse '.. left ..','.. top ..','.. radiusX ..','.. radiusY
		)

	else -- add
		self:changeType( 'ellipse '.. left ..','.. top ..','.. radiusX ..','.. radiusY )
	end


	self.type = 'ellipse'
	self.meter:option( self.name, self.content )
	return self
end



--- Define a path-based shape.
--
-- Accepts a simplified path syntax similar to SVG commands.
-- The method automatically converts commands to Rainmeter
-- path segments such as LineTo, CurveTo, and ArcTo.
--
-- Supported commands include:
--   `M` → Move to
--   `L` → Line to
--   `V` → Vertical Line to.   Set Y and use last X
--   `H` → Horizontal Line to. Set X and use last Y
--   `C` → Cubic bezier
--   `S` → Cubic bezier using last X Y.
--   `Q` → Quadratic bezier curve
--   `A` → Elliptical arc
--   `Z` → Close path
--
-- @param (string) path - Path definition string.
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage
-- shape:path("0,0 L 100,0 L 100,50 Z")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
-- @see https://developer.mozilla.org/en-US/docs/Web/SVG/Tutorials/SVG_from_scratch/Paths
function M:path( inner )
	local Name = self.content:lower():match( 'path%s*([%d%w]+)' )

	-- Getter
	if not inner then
		if Name then
			return self.meter:option( Name )
		end

		return nil
	end


	inner =
		inner:gsub( '|', ' ' )
		:gsub( '^%s*[Mm]%s*'  , '' )
		:gsub( '%s*[MmLl]%s*'  , '|lineTo ' )
		:gsub( '%s*[QqCc]%s*', '|curveTo ' )
		:gsub( '%s*[Aa]%s*'  , '|arcTo ' )
		:gsub( '([%.%+%-%d]+)%s*([%.%+%-%d]+)%s*[Vv]%s*([%.%+%-%d]+)', '%1 %2|lineTo %1 %3' )
		:gsub( '([%.%+%-%d]+)%s*([%.%+%-%d]+)%s*[Hh]%s*([%.%+%-%d]+)', '%1 %2|lineTo %3 %2' )
		:gsub( '%s*[Zz]%s*'  , '|closePath 1' )
		-- :gsub( '(%d%.)[^%d]', '%10' )
		:gsub( '(%d)%s+(%d)' , '%1,%2' )
		:gsub( '(%d)%s+(%d)' , '%1,%2' )

	if Name then
		self.meter:option( Name, inner )

	else
		DRAWS = DRAWS + 1
		self:changeType( 'path paths' .. DRAWS )
		self.meter:option( 'paths' .. DRAWS, inner )
		self.meter:option( self.name, self.content )
	end

	self.type = 'path'
	self.contentPath = inner
	return self
end



--- Define a polyline shape.
-- Creates a sequence of connected line segments.
--
-- @param (string) points - List of points `"x1,y1 x2,y2 x3,y3"`.
-- @return (table) Shape instance
--
-- @usage shape:polyline("0,0 50,20 100,0")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
function M:polygon( points )
	return self:polyline( points, true )
end



--- Define a polyline shape.
-- Creates a path using a flat numeric coordinate array.
-- Similar to `polygon`, but does not automatically close the path.
--
-- @param (table) points - Flat coordinate array.
-- @param (boolean) [close=false] - Close the path automatically.
-- @return (table) Shape instance
--
-- @usage
-- shape:polyline({0,0, 50,50, 100,0})
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
function M:polyline( points, close )
	local name = self.content:lower():match( 'path%s*(%w+)' )
	local out = {}

	out[1] = points[1] ..','.. points[2]

	local index = 2
	for indice = 3, #points, 2 do
		out[ index ] = 'lineTo '.. points[ indice ] ..','.. points[ indice + 1 ]
		index = index + 1
	end

	if close then
		out[ index ] = 'closePath 1'
		self.type = 'polygon'
	else
		self.type = "polyline"
	end


	local inner = table.concat( out, '|' )
	self.contentPath = inner

	if name then
		self.meter:option( name, inner )

	else
		DRAWS = DRAWS + 1
		local pathName = 'paths'.. DRAWS

		self:changeType( 'path ' .. pathName )
		self.meter:option( pathName, inner )
		self.meter:option( self.name, self.content )
	end

	return self
end



--- Create or modify an line shape.
-- Basic shape used to create a line connecting two points.
--
-- @param (number) startx - Coordinate of the starting point of the line.
-- @param (number) starty - Coordinate of the starting point of the line.
-- @param (number) endx - Coordinate of the ending point of the line.
-- @param (number) Y - Coordinate of the ending point of the line.
-- @return (table) Shape instance
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Line
function M:line( startx, starty, endx, endy )
	local value = self.content:lower():match( 'line%s*'.. SHAPE_PARAM )

	print( value )
	return self
end



-- @see https://docs.rainmeter.net/manual/meters/shape/#Arc
-- shape:arc



-- @see https://docs.rainmeter.net/manual/meters/shape/#Curve
-- shape:curve



--- Set or get the fill color.
--
-- Supports multiple formats:
--   RGB         → fill(255,0,0)
--   RGBA        → fill(255,0,0,150)
--   Hex         → fill("FF0000")
--   Short hex   → fill("f00")
--   Transparent → fill("transparent")
--
-- @param (number|string) r - Red value or Hex string.
-- @param (number) [g] - Green value.
-- @param (number) [b] - Blue value.
-- @param (number) [a] - Alpha value (0-255).
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage
-- shape:fill(255,0,0)
-- shape:fill(255,0,0,120)
-- shape:fill("FF0000")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Fill
function M:fill( red, green, blue, alpha )
	-- Detects whether gradients will be used and redirects to another function.
	if type( red ) == 'string' then
		if red:match( '^[%d%s-]+|' ) then
			return self:lgradient( red )

		elseif red:match( '^[%d%s-]+,[%d%s-]+|' ) then
			return self:rgradient( red )
		end
	end


	local value = self.content:lower():match( 'fill%s+[colringadet]+%s+([%s,%.%d%w]+)' )

	-- Getter
	if not red and not green and not blue and not alpha then
		-- if linear or radial gradient
		if value and self.content:lower():match( 'gradient' ) then
			return self.meter:option( value:gsub( '%s*', '' )):gsub( '^[%s-+]*%d+%.*%d*%s*|%s*', '' ), value

		else
			return value and value or 'FFFFFF'
		end
	end


	red, green, blue, alpha = parseColor( red, green, blue, alpha )

	if value then -- substitute fill, if exist
		-- remove fill attribute
		self.content = self.content:lower():gsub(
			'%s*fill%s+[colringadet]+%s+[%s,%d%.%d%w]+%s*',
			'fill color '.. red .. green .. blue .. alpha
		)


	else -- add fill.
		self.content =
			self.content ..
			( self.content:find( '|$' ) and '' or '|' ) ..
			'fill color '.. red .. green .. blue .. alpha
	end

	self.meter:option( self.name, self.content )
	return self
end



--- Set or get the stroke color.
-- Defines the outline color of the shape.
--
-- @param (number) r - Red component.
-- @param (number) g - Green component.
-- @param (number) b - Blue component.
-- @param (number) a - Alpha component.
-- @return (table) Shape instance
--
-- @usage shape:strokecolor(255,255,255)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Stroke
function M:strokecolor( red, green, blue, alpha )
	-- Detects whether gradients will be used and redirects to another function.
	if type( red ) == 'string' then
		if red:match( '^[%d%s-]+|' ) then
			return self:lgradient( red, 'stroke' )

		elseif red:match( '^[%d%s-]+,[%d%s-]+|' ) then
			return self:rgradient( red, 'stroke' )
		end
	end


	local param = self.content:lower():match( 'stroke%s*[colringadet]+%s*([%s,%.%d%w]+)' )

	if not red and not green and not blue and not alpha then
		-- If linear or radial gradient
		if param and self.content:lower():match( 'gradient' ) then
			local attr  = self.meter:option( param:gsub( '%s*', '' ))
			local angle = attr:match( '^([%s-+]*%d+%.*%d*%s*)|' )
			local color = attr:gsub( '^[%s-+]*%d+%.*%d*%s*|%s*', '' )
			return attr, angle, color, param

		else
			return param and param or '000000'
		end
	end


	red, green, blue, alpha = parseColor( red, green, blue, alpha )

	if param then
		self.content = self.content:lower():gsub(
			'%s*stroke%s*color%s*([%s,%d%.]+)%s*',
			'stroke color '.. red .. green .. blue .. alpha
		)

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'stroke color '.. red .. green .. blue .. alpha

	end

	self.meter:option( self.name, self.content )
	return self
end



--- Set or get the stroke width.
-- Defines the thickness of the shape outline.
--
-- @param (number) [width] - Stroke width in pixels.
-- @return (table) Shape instance
--
-- @usage shape:strokewidth(2)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeWidth
function M:strokewidth( width )
	local value = self.content:lower():match( 'strokewidth%s*(%d+)' )

	if width == nil then
		return value
	end


	if value then
		self.content = self.content:lower():gsub( '%s*strokewidth%s*(%d+)%s*',
			'StrokeWidth '.. width )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'StrokeWidth '.. width

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set the stroke line join style.
-- Determines how two connected stroke segments join.
--
-- @param ("miter"|"bevel"|"round") type - Join style.
-- @param (float) limit - Specify how sharp the miter joints can be (the default is 10.0).
-- @return (table) Shape instance
--
-- @usage
-- shape:strokelinejoin("round")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeLineJoin
function M:strokelinejoin( type, limit )
	local value = self.content:lower():match( 'strokelinejoin%s*([%.,%s%d%w]+)' )

	-- Getter
	if not type and not limit then
		return value:match( '[%d%w]+' ), value:match( ',%s*([%d%.]+)' )
	end


	if not limit then
		limit = ''
	else
		limit = ','.. limit
	end

	if value then
		self.content = self.content:lower():gsub(
			'%s*strokelinejoin%s*([%.,%s%d%w]+)%s*',
			'strokelinejoin '.. type .. limit
		)

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokelinejoin '.. type .. limit

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set the starting cap style for strokes.
-- Defines the cap applied to the beginning of stroke segments.
--
-- @param ("round"|"square"|"butt") captype - Cap style.
-- @return (table) Shape instance
--
-- @usage shape:strokestartcap("round")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeStartCap
function M:strokestartcap( captype )
	local Value = self.content:lower():match( 'strokestartcap%s*(%d+)' )

	if Value then
		self.content = self.content:lower():gsub( '%s*strokestartcap%s*(%d+)%s*',
			'strokestartcap '.. captype )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokestartcap '.. captype

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set or get the stroke dash offset.
-- Defines the starting offset into the dash pattern, useful for
-- animating dashed strokes.
--
-- @param (number) [offset] - Offset in pixels. Omit to use as getter.
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage shape:strokedashoffset(10)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeDashOffset
function M:strokedashoffset( offset )
	local value = self.content:lower():match( 'strokedashoffset%s*(%d+)' )

	if value then
		self.content = self.content:lower():gsub( '%s*strokedashoffset%s*(%d+)%s*',
			'strokedashoffset '.. offset )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokedashoffset '.. offset

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set or get the stroke dash cap style.
-- Defines the cap style applied to the ends of each dash segment.
--
-- @param ("round"|"square"|"butt") [dashType] - Dash cap style. Omit to use as getter.
-- @return (table) Shape instance
-- @return (string|nil) When used as getter.
--
-- @usage shape:strokedashcap("round")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeDashCap
function M:strokedashcap( dashType )
	local value = self.content:lower():match( 'strokedashcap%s*(%w+)' )

	if value then
		self.content = self.content:lower():gsub( '%s*strokedashcap%s*(%w+)%s*',
			'strokedashcap '.. dashType )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokedashcap '.. dashType

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set or get the stroke dash pattern.
-- Defines the length of dashes and gaps to create a dashed stroke line.
--
-- @param (number) [dashSize] - Length of each dash, in pixels.
-- @param (number) [gapSize] - Length of each gap, in pixels.
-- @return (table) Shape instance
-- @return (number|nil) dashSize, (number|nil) gapSize - When used as getter.
--
-- @usage shape:strokedashes(4, 2)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeDashes
function M:strokedashes( dashSize, gapSize )
	local val1, val2 = self.content:lower():match( 'strokedashes%s*(%d+)%s*,%s*(%d+)' )

	if not dashSize and not gapSize then
		return tonumber( val1 ), tonumber( val2 )
	end


	if val1 and val2 then
		self.content = self.content:lower():gsub( '%s*strokedashes%s*(%d+)%s*,%s*(%d+)%s*',
			'strokedashes '.. dashSize ..','.. gapSize )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokedashes '.. dashSize ..','.. gapSize

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Apply a scale transformation.
-- Scales the shape relative to an anchor point.
--
-- @param (number) axisX - Horizontal scale factor.
-- @param (number) axisY - Vertical scale factor.
-- @param (number) anchorX - Anchor X coordinate.
-- @param (number) anchorY - Anchor Y coordinate.
-- @return (table) Shape instance
--
-- @usage
-- shape:scale(1.5)
-- shape:scale(1.5,1.5,50)
-- shape:scale(1.5,1.5,50,50)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Scale
function M:scale( axisX, axisY, anchorX, anchorY )
	local value = self.content:lower():match( 'scale%s*([%s,%d]+)' )
	axisY = axisY and axisY or axisX
	anchorY = anchorY and anchorY or anchorX

	if not axisX and not axisY and not anchorX and not anchorY then
		return value and value or '1,1'
	end

	if value then
		self.content = self.content:lower():gsub(
			'%s*scale%s*([%s,%d]+)%s*',
			'scale '.. axisX ..','.. axisY .. ( anchorX and ','.. anchorX ..','.. anchorY or '' ))

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'scale '.. axisX ..','.. axisY .. ( anchorX and ','.. anchorX ..','.. anchorY or '' )

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Create a linear gradient fill.
-- Defines a Rainmeter gradient option and applies it
-- to the current shape fill.
--
-- @param (string) value - Gradient color stops
-- @param ("fill"|"stroke") [modifier="fill"] - You reference this in the shape Fill or Stroke modifiers.
-- @return (table) Shape instance
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#DefiningGradients
function M:gradient( value, modifier )
	modifier = modifier:lower() or 'fill'

	-- local find = self.content:lower():match( modifier.. '%s+[lringadet]+%s+([%s,%d%w]+)' )
	local tipo = 'LinearGradient'
	if value:match( '^[%d%s-]+,[%d%s-]+|' ) then
		tipo = 'radial'
	end


	if attr then
		-- Remove fill attribute
		self.content = self.content:lower():gsub( '|%s*'.. modifier ..'%s+[lringadet]+%s+[%s,%d%w]+%s*', '' )

		GRADIENT = GRADIENT + 1
		value = 'gradient'.. GRADIENT

		self.content = self.content ..'|'.. modifier .. ' ' ..tipo.. ' gradient' ..GRADIENT
		self.meter:option( self.name, self.content )
	end
end



--- Create a linear gradient fill.
-- Defines a Rainmeter gradient option and applies it
-- to the current shape fill.
--
-- @param (number) x1 Start X.
-- @param (number) y1 Start Y.
-- @param (number) x2 End X.
-- @param (number) y2 End Y.
-- @param (string) ... Gradient color stops.
-- @return (table) Shape instance
--
-- @usage
-- shape:lgradient(90, "0 255, 10, 10", ".9 20, 20, 255, 250")
-- shape:lgradient(90, "0% 10, 10, 10", "90% 20, 20, 20, 254")
-- shape:lgradient("90 | 10, 10, 10 ; 0.0 | 20, 20, 20, 254 ; 1.0")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#LinearGradient
function M:lgradient( angle, ... )
	local result = ''
	local new    = false
	local arg    = { ... }
	local tipo   = 'fill'

	if arg[ #arg ] == 'fill' then
		table.remove( arg, 1 )
	elseif arg[ #arg ] == 'stroke' then
		tipo   = 'stroke'
		table.remove( arg, 1 )
	end

	local value  = self.content:lower():match( tipo.. '%s+[lringadet]+%s+([%s,%d%w]+)' )

	-- If not exists gradient.
	if not value then
		new = true
		GRADIENT = GRADIENT + 1
		value = 'gradient'.. GRADIENT

		-- Remove fill attribute
		self.content = self.content:lower():gsub( '%s*|%s*' ..tipo.. '%s*color%s*[%s,%d%w]+%s*', '' )
	end


	-- organize syntax
	for index = 1, #arg do
		local percentage = arg[ index ]:match( '([%-%d]+)%%' )
		if percentage then
			percentage = percentage / 100
		else
			percentage = arg[ index ]:match( '([%-%d]?%.?[%-%d]+)' )
		end

		local color = arg[ index ]:match( '(%d+%s*,%s*%d+%s*,%s*%d+%s*,?%s*%d*)' )
		if not color then
			color = arg[ index ]:match( '%x%x%x%x%x%x%x?%x?' )
		end

		result = result ..'|'.. color ..';'.. percentage
	end


	result = ( angle .. result ):gsub( '%s*', '' )
	self.meter:option( value:gsub( '%s*', '' ), result )


	if new then
		self.content =
			self.content ..
			( self.content:find( '|$' ) and '' or '|' ) ..
			tipo ..' linearGradient '.. value

		self.meter:option( self.name, '' )
		self.meter:option( self.name, self.content )
	end

	return self
end



--- Create a radial gradient fill.
-- Similar to `lgradient` but produces a radial gradient.
--
-- @param (number) x Center X.
-- @param (number) y Center Y.
-- @param (number) radius Gradient radius.
-- @param (string) ... Gradient stops.
-- @return (table) Shape instance
--
-- @usage shape:rgradient(50,50,40,"255,0,0;0","0,0,255;1")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#RadialGradient
function M:rgradient( ... )
	local value   = self.content:lower():match( 'fill%s+[radilgent]+%s+([%s,%d%w]+)' )
	local result  = ''
	local new     = false
	local centerX = 0
	local centerY = 0
	local offsetX = ''
	local offsetY = ''
	local radiusX = ''
	local radiusY = ''
	local arg     = {...}

	for index = 1, #arg do -- organize syntax.
		local tipo = type( arg[ index ])
		if tipo == 'string' then
			local percentage = arg[ index ]:match( '(%d+)%%' )
			if percentage then
				percentage = percentage / 100
			else
				percentage = arg[ index ]:match( '(%d?%.?%d+)' )
			end


			local color = arg[ index ]:match( '(%d+%s*,%s*%d+%s*,%s*%d+%s*,?%s*%d*)' )
			if not color then
				color = arg[ index ]:match( '%x%x%x%x%x%x%x?%x?' )
			end

			result = result ..'|'.. color ..';'.. percentage


		elseif tipo == 'number' then
			    if centerX == 0  then centerX = arg[ index ]
			elseif centerY == 0  then centerY = arg[ index ] ..','
			elseif offsetX == '' then offsetX = arg[ index ] ..','
			elseif offsetY == '' then offsetY = arg[ index ] ..','
			elseif radiusX == '' then radiusX = arg[ index ] ..','
			elseif radiusY == '' then radiusY = arg[ index ] ..','
			else error( 'param error' )
			end
		end
	end


	-- If not exists gradient.
	if not value then
		new = true
		GRADIENT = GRADIENT + 1
		value = 'gradient'.. GRADIENT
		-- Remove fill attribute
		self.content = self.content:lower():gsub( '%s*|%s*fill%s*color%s*[%s,%d%w]+%s*', '' )
	end


	result =
		centerX ..','..
		centerY .. offsetX ..
		offsetY .. radiusX ..
		radiusY .. result

	self.meter:option( value:gsub( '%s*', '' ), result )


	if new then
		self.content =
			self.content ..
			( self.content:find( '|$' ) and '' or '|' ) ..
			'fill radialgradient '.. value

		self.meter:option( self.name, self.content )
	end

	return self
end



--- !!!TESTING!!!
--- Get or translate the minimum Y coordinate of a shape.
-- Intended to read (getter) or shift (setter) all Y coordinates found
-- in the shape's path/polygon data or inline parameters.
-- NOTE: experimental/incomplete — the setter branch computes new Y
-- values locally but does not persist them back to `self.content`
-- or the meter option.
--
-- @param (number) [move] - Amount to add to each Y coordinate. Omit to use as getter.
-- @return (number) Minimum Y coordinate found, when used as getter.
-- @return (table) Shape instance, when used as setter (see note above).
--
-- @usage local minY = shape:trasnlatey()
function M:trasnlatey( move )
	local result
	local content

	if self.type == 'path' or self.type == 'polygon' then
		local option = self.content:match( '^%s*path%s+([%d%w]+)' )
		content = self.meter:option( option ):lower()
			:gsub( 'closepath%s+[10]', '' )
			:gsub( '|', '' )
			:gsub( '[a-zA-Z]+', '' )

	else
		content = self.content:match( '^%s*[a-zA-Z]+%s+([^|]+)' )
	end


	if not move then -- Getter
		for value in content:gmatch( '[%d%.]+%s*,%s*[%d%.]+' ) do
			value = tonumber( value:match( ',(%s*[%d.]+)' ))
			result = math.min( result or value, value )
		end
		return result


	else -- Setter
		for value in content:gmatch( '[%d%.]+%s*,%s*[%d%.]+' ) do
			value = tonumber( value:match( ',(%s*[%d.]+)' ))
			value = value + move
		end
	end

	return self
end



--- Change the current shape type.
-- Replaces the primitive type in the internal shape definition.
-- This allows converting an existing shape (for example `rectangle`)
-- into another type such as `path` or `ellipse` while preserving
-- the remaining parameters.
--
-- @param (string) newtype - New shape type (e.g. `"rectangle"`, `"ellipse"`, `"path"`).
-- @return shape Returns the shape instance for chaining.
--
-- @usage shape:changeType("ellipse")
function M:changeType( newType )
	local shapeType = self.content:lower():match( REGEX_SHAPE ):gsub( '%s*$', '' )
	assert( REGEX[ shapeType ], 'The Shape type is probably wrong: "'..  shapeType ..'".' )

	self.content = self.content:lower():gsub( REGEX[ shapeType ], newType )
	return self
end



--- Create a new shape entry in the meter.
-- Automatically finds the first available shape slot, including the base
-- `Shape` (no index) and subsequent indexed shapes (`Shape2`, `Shape3`, etc.).
--
-- If no shapes exist yet, this will create and return the base `Shape`.
-- Otherwise, it will create the next available indexed shape.
--
-- @return (table) New Shape instance
--
-- @usage
-- local shape1 = meter("Graph"):rectangle(0,0,100,50) -- creates Shape
-- local shape2 = shape1:add() -- creates Shape2
-- shape2:ellipse(50,25,20,20)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Shape
function M:add()
	local index = self:length() +1
	local key =
		index == 1
		and 'shape'
		or ( 'shape'.. index )

	return construct( self.meter, self.meter.name, index ~= 1 and index or '' )
end



--- Retrieve an existing shape by index.
-- Returns a shape instance corresponding to the given index if it exists.
-- The base `Shape` is represented by index `1` or `nil`.
--
-- This method does not create new shapes. If the requested shape does not
-- exist, it returns `nil`.
--
-- @param (number) index - Shape index (`1` for base `Shape`, `2+` for `Shape2`, etc.).
-- @return (table|nil) Existing shape instance, or `nil` if not found.
--
-- @usage
-- local base = shape:shape()     -- retrieves Shape
-- local s2   = shape:shape(2)    -- retrieves Shape2 (if it exists)
--
-- if not s2 then
--     s2 = shape:add()           -- explicitly create Shape2
-- end
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Shape
function M:shape( index )
	index = index or 1

	local key =
		index == 1
		and 'shape'
		or ( 'shape'.. index )

	if self.meter:option( key ) then
		return construct( self.meter, self.meter.name, index ~= 1 and index or '' )
	end

	return nil
end



--- Count how many shape entries currently exist in the meter.
-- Walks the `Shape`, `Shape2`, `Shape3`... options sequentially and
-- stops at the first one that is missing.
--
-- @return (number) Number of active shape entries found.
--
-- @usage local total = shape:length()
function M:length()
	local index = 1

	while true do
		local key = (
			index == 1
			and 'shape'
			or 'shape'.. index
		)

		local exist = self.meter:option( key )
		if exist and exist ~= 'none' then
			index = index + 1
		else
			break
		end
	end

	return index -1
end



return construct
