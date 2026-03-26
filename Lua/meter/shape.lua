--- Shape meter extension.
--
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
-- @version: 0.2.2
-- @see meter
-- @see https://docs.rainmeter.net/manual/meters/shape/
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
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

local PATHS = 0
local GRADIENT = 0


-- Lua Regex
local SHAPE_PARAM = '([%s,%d%.%(%)%+%*-]+)'



local percentOf = function( value, percent )
	local multi = 10 ^ 2
	return math.floor(( value * percent / 100 ) * multi + 0.5 ) / multi
end



local shape   = {}
shape.__index = shape
shape.paths   = 0



--- Create or modify a rectangle shape.
--
-- Defines the rectangle geometry for the current shape entry.
-- If called without arguments, returns the current rectangle parameters.
--
-- Percentage values (e.g. `"50%"`) are automatically resolved
-- relative to the parent meter dimensions.
--
-- @tparam shape self Shape instance.
-- @tparam (number|string) x Left coordinate.
-- @tparam (number|string) y Top coordinate.
-- @tparam (number|string) width Rectangle width.
-- @tparam (number|string) height Rectangle height.
--
-- @treturn shape When used as setter.
-- @treturn[2] (string|nil) When used as getter.
--
-- @usage
-- shape:rectangle(0,0,200,100)
-- shape:rectangle("10%","10%","80%","50%")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Rectangle
--
function shape:rectangle( left, top, width, height, radiusX, radiusY )
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
			'rectangle ' .. left .. ',' .. top .. ',' .. width .. ',' .. height .. ',' .. radiusX .. ',' .. radiusY )

	else -- Add
		self.content = self.content ..
			'rectangle ' ..left.. ',' ..top.. ',' ..width.. ',' ..height.. ',' ..radiusX.. ',' ..radiusY
	end

	self.meter:option( self.name, self.content )
	return self
end



--- Create or modify an ellipse shape.
--
-- Defines an ellipse primitive with center coordinates
-- and horizontal/vertical radii.
--
-- If called without parameters the current ellipse
-- definition is returned.
--
-- @tparam shape self Shape instance.
-- @tparam (number|string) x Center X coordinate.
-- @tparam (number|string) y Center Y coordinate.
-- @tparam (number|string) radiusX Horizontal radius.
-- @tparam (number|string) radiusY Vertical radius.
--
-- @treturn shape When used as setter.
-- @treturn[2] (string|nil) When used as getter.
--
-- @usage
-- shape:ellipse(100,50,40,40)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Ellipse
--
function shape:ellipse( left, top, radiusX, radiusY )
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
	self.meter:option( self.meter.name, self.name, self.content )
	return self
end



--- Define a path-based shape.
--
-- Accepts a simplified path syntax similar to SVG commands.
-- The method automatically converts commands to Rainmeter
-- path segments such as LineTo, CurveTo, and ArcTo.
--
-- Supported commands include:
--   `L` → line to
--   `C` → cubic bezier
--   `A` → arc
--   `Z` → close path
--
-- @tparam shape self Shape instance.
-- @tparam[opt] string path Path definition string.
--
-- @treturn shape When used as setter.
-- @treturn[2] (string|nil) When used as getter.
--
-- @usage
-- shape:path("0,0 L 100,0 L 100,50 Z")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
--
function shape:path( inner )
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
		:gsub( '%s*[Cc]%s*', '|curveTo ' )
		:gsub( '%s*[Aa]%s*', '|arcTo ' )
		:gsub( '%s*[Ll]%s*', '|lineTo ' )
		:gsub( '%s*[Zz]%s*', '|closePath 1' )


	if Name then
		self.meter:option( Name, inner )

	else
		PATHS = PATHS + 1
		self.meter:option( self.meter.name, 'Paths' .. PATHS, inner )
		self:changeType( 'Path Paths' .. PATHS )
		self.meter:option( self.meter.name, self.name, self.content )
	end

	self.type = 'path'
	self.contentPath = inner
	return self
end



--- Define a polyline shape.
--
-- Creates a sequence of connected line segments.
--
-- @tparam shape self Shape instance.
-- @tparam[opt] (string) points List of points `"x1,y1 x2,y2 x3,y3"`.
--
-- @treturn shape
--
-- @usage
-- shape:polyline("0,0 50,20 100,0")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
--
function shape:polygon( points )
	return self:polyline( points, true )
end



--- Define a polygon shape.
--
-- Similar to `polyline` but automatically closes the path.
--
-- @tparam shape self Shape instance.
-- @tparam[opt] (string) points List of points `"x1,y1 x2,y2 x3,y3"`.
--
-- @treturn shape
--
-- @usage
-- shape:polygon("0,0 50,50 100,0")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Path
--
function shape:polyline( points, close )
	local name = self.content:lower():match( 'path%s*'.. SHAPE_PARAM )
	local inner = ''

	for value in points:gmatch( '[%d%.]+%s*,%s*[%d%.]+' ) do
		if inner == '' then
			inner = value
		else
			inner = inner ..'|lineTo '.. value
		end
	end

	-- use for polygon
	if close == true then
		inner = inner ..'|closePath 1'
		self.type = 'polygon'
	else
		self.type = 'polyline'
	end


	if name then
		-- local Value = self.meter:option( name )
		self.meter:option( self.name, inner )

	else
		self.paths = self.paths + 1
		self:changeType( 'path paths' .. self.paths )
		self.meter:option( 'paths' .. self.paths, inner )
		self.meter:option( self.name, self.content )
	end

	return self
end



--[[
	https://docs.rainmeter.net/manual/meters/shape/#Line
--]]
-- shape:line



--[[
	https://docs.rainmeter.net/manual/meters/shape/#Arc
--]]
-- shape:arc



--[[
	https://docs.rainmeter.net/manual/meters/shape/#Curve
--]]
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
-- @tparam shape self Shape instance.
-- @tparam (number|string) r Red value or hex string.
-- @tparam (number) g Green value.
-- @tparam (number) b Blue value.
-- @tparam (number) a Alpha value (0-255).
--
-- @treturn shape When used as setter.
-- @treturn[2] (string|nil) When used as getter.
--
-- @usage
-- shape:fill(255,0,0)
-- shape:fill(255,0,0,120)
-- shape:fill("FF0000")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Fill
--
function shape:fill( red, green, blue, alpha )
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



	-- if hex or transparent
	if red and not green and not blue and not alpha then
		green = ''
		blue  = ''
		alpha = ''

		if red == 'transparent' then
			red = '0,0,0,0'

		-- using hex color 3 digits
		elseif red:len() == 3 then
			red = red:sub( 1, 1 ):rep( 2 ) ..
			      red:sub( 2, 2 ):rep( 2 ) ..
			      red:sub( 3, 3 ):rep( 2 )
		end


	else
		green = ','.. green
		blue  = ','.. blue
		alpha = alpha and ','.. alpha or ''
	end


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
--
-- Defines the outline color of the shape.
--
-- @tparam shape self Shape instance.
-- @tparam (number) r Red component.
-- @tparam (number) g Green component.
-- @tparam (number) b Blue component.
-- @tparam (number) a Alpha component.
--
-- @treturn shape
--
-- @usage
-- shape:strokecolor(255,255,255)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Stroke
--
function shape:strokecolor( red, green, blue, alpha )
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


	-- if hex
	if red and not green and not blue and not alpha then
		green = ''
		blue  = ''
		alpha = ''

		if red == 'transparent' then
			red = '0,0,0,0'

		elseif red:len() == 3 then
			red = red:sub( 1, 1 ):rep( 2 ) ..
			      red:sub( 2, 2 ):rep( 2 ) ..
			      red:sub( 3, 3 ):rep( 2 )
		end

	-- if rgba
	else
		green = ','.. green
		blue  = ','.. blue
		alpha = alpha and ','.. alpha or ''
	end


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
--
-- Defines the thickness of the shape outline.
--
-- @tparam shape self Shape instance.
-- @tparam[opt] (number) width Stroke width in pixels.
--
-- @treturn shape
--
-- @usage
-- shape:strokewidth(2)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeWidth
--
function shape:strokewidth( width )
	local value = self.content:lower():match( 'strokewidth%s*(%d+)' )

	if not width then
		return value
	end


	if value then
		self.content = self.content:lower():gsub( '%s*strokewidth%s*(%d+)%s*',
			'strokewidth '.. width )

	else
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'strokewidth '.. width

	end


	self.meter:option( self.name, self.content )
	return self
end



--- Set the stroke line join style.
--
-- Determines how two connected stroke segments join.
--
-- Common values:
--   "miter"
--   "bevel"
--   "round"
--
-- @tparam shape self Shape instance.
-- @tparam (string) join Join style.
--
-- @treturn shape
--
-- @usage
-- shape:strokelinejoin("round")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeLineJoin
--
function shape:strokelinejoin( type, limit )
	local value = self.content:lower():match( 'strokelinejoin%s*([%.,%s%d%w]+)' )

	-- Get and return values
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
--
-- Defines the cap applied to the beginning of stroke segments.
--
-- @tparam shape self Shape instance.
-- @tparam (string) cap Cap style (`"round"`, `"square"`, `"butt"`).
--
-- @treturn shape
--
-- @usage
-- shape:strokestartcap("round")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#StrokeStartCap
--
function shape:strokestartcap( captype )
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



--- Apply a scale transformation.
--
-- Scales the shape relative to an anchor point.
--
-- @tparam shape self Shape instance.
-- @tparam (number) scaleX Horizontal scale factor.
-- @tparam (number) scaleY Vertical scale factor.
-- @tparam (number) anchorX Anchor X coordinate.
-- @tparam (number) anchorY Anchor Y coordinate.
--
-- @treturn shape
--
-- @usage
-- shape:scale(1.5,1.5,50,50)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Scale
--
function shape:scale( axisX, axisY, anchorX, anchorY )
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
--
-- Defines a Rainmeter gradient option and applies it
-- to the current shape fill.
--
-- @tparam shape self Shape instance.
-- @tparam (number) x1 Start X.
-- @tparam (number) y1 Start Y.
-- @tparam (number) x2 End X.
-- @tparam (number) y2 End Y.
-- @tparam (string) ... Gradient color stops.
-- @treturn shape
--
-- @usage
-- shape:lgradient(0,0,100,0,"255,0,0;0","0,0,255;1")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#DefiningGradients
--
function shape:gradient( attr )
	local value = self.content:lower():match( 'fill%s+lringadet]+%s+([%s,%d%w]+)' )

	if attr then
		-- Remove fill attribute
		self.content = self.content:lower():gsub( '|%s*fill%s+[lringadet]+%s+[%s,%d%w]+%s*', '' )
		self.content = self.content ..'| Fill'.. attr:lower():gsub( '^[linear]+%s+', 'LinearGradient ' )
		self.meter:option( self.name, self.content )
	end
end



--- Create a linear gradient fill.
--
-- Defines a Rainmeter gradient option and applies it
-- to the current shape fill.
--
-- @tparam shape self Shape instance.
-- @tparam (number) x1 Start X.
-- @tparam (number) y1 Start Y.
-- @tparam (number) x2 End X.
-- @tparam (number) y2 End Y.
-- @tparam (string) ... Gradient color stops.
-- @treturn shape
--
-- @usage
-- shape:lgradient( 90, "0 255, 10, 10", ".9 20, 20, 255, 250" )
-- shape:lgradient( 90, "0% 10, 10, 10", "90% 20, 20, 20, 254" )
-- shape:lgradient( "90 | 10, 10, 10 ; 0.0 | 20, 20, 20, 254 ; 1.0" )
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#LinearGradient
--
function shape:lgradient( angle, ... )
	local value  = self.content:lower():match( 'fill%s+[lringadet]+%s+([%s,%d%w]+)' )
	local result = ''
	local new    = false
	local arg    = { ... }

	-- If not exists gradient.
	if not value then
		new = true
		GRADIENT = GRADIENT + 1
		value = 'gradient'.. GRADIENT
		-- Remove fill attribute
		self.content = self.content:lower():gsub( '%s*|%s*fill%s*color%s*[%s,%d%w]+%s*', '' )
	end


	for index = 1, #arg do -- organize syntax.
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
	end


	result = ( angle .. result ):gsub( '%s*', '' )
	self.meter:option( value:gsub( '%s*', '' ), result )


	if new then
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'fill linearGradient '.. value

		self.meter:option( self.name, '' )
		self.meter:option( self.name, self.content )
	end

	return self
end



--- Create a radial gradient fill.
--
-- Similar to `lgradient` but produces a radial gradient.
--
-- @tparam shape self Shape instance.
-- @tparam (number) x Center X.
-- @tparam (number) y Center Y.
-- @tparam (number) radius Gradient radius.
-- @tparam (string) ... Gradient stops.
--
-- @treturn shape
--
-- @usage
-- shape:rgradient(50,50,40,"255,0,0;0","0,0,255;1")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#RadialGradient
--
function shape:rgradient( ... )
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
		self.content = self.content:lower():gsub( '%s*|%s*fill%s*color]%s*[%s,%d%w]+%s*', '' )
	end


	result =
		centerX ..','..
		centerY .. offsetX ..
		offsetY .. radiusX ..
		radiusY .. result

	self.meter:option( value:gsub( '%s*', '' ), result )


	if new then
		self.content = self.content ..
		( self.content:find( '|$' ) and '' or '|' ) ..
		'fill radialgradient '.. value

		self.meter:option( self.name, self.content )
	end
end



--[[ !!!TESTING!!!
--]]
function shape:shadow( color, left, top, blur )
	return self
end



--[[ !!!TESTING!!!
--]]
function shape:top( move )
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
			print( value )
		end
	end

	return self
end



--- Change the current shape type.
--
-- Replaces the primitive type in the internal shape definition.
-- This allows converting an existing shape (for example `rectangle`)
-- into another type such as `path` or `ellipse` while preserving
-- the remaining parameters.
--
-- @tparam shape self Shape instance.
-- @tparam (string) newtype New shape type (e.g. `"rectangle"`, `"ellipse"`, `"path"`).
-- @treturn shape Returns the shape instance for chaining.
--
-- @usage
-- shape:changeType("ellipse")
--
-- @see https://docs.rainmeter.net/manual/meters/shape/
--
function shape:changeType( newType )
	local option = {
		rectangle = 'rectangle%s*[%s,%d]+',
		ellipse   = 'ellipse%s*([%s,%d]+)',
		curve     = '',
		line      = '',
		arc       = '',
		path      = ''
	}

	local shapeType = self.type
	if shapeType == 'polyline' then
		shapeType = 'rectangle'
	end

	self.content = self.content:lower():gsub( option[ shapeType ], newType )
	return self
end



--- Create a new shape entry in the meter.
--
-- Automatically finds the next available shape option
-- (`Shape2`, `Shape3`, etc.) and returns a new shape instance.
--
-- @tparam shape self Shape instance.
-- @treturn shape New shape instance.
--
-- @usage
-- local shape1 = meter("Graph"):rectangle(0,0,100,50)
-- local shape2 = shape1:add()
-- shape2:ellipse(50,25,20,20)
--
-- @see https://docs.rainmeter.net/manual/meters/shape/#Shape
--
function shape:add()
	index = 2

	while true do
		if self.meter:option( 'shape' .. index ) then
			index = index + 1
		else
			break
		end
	end

	return require( 'meter' )( self.meter.name, index )
end







local clone = function( self, super )
	self.option = super.option
	self.id = super.id

	self.event = function( self, events, callback )
		local ok, result = pcall( super.event, super, events, callback )
		return result
	end

	self.update = function( self )
		super:update()
		return self
	end

	return self
end





return function( super, name, index )
	local class = clone( {}, super )
	class.name = 'shape'.. ( index and index or '' )
	class.meter = super

	class.content = class.meter:option( class.name )
	if not class.content then
		class.meter:option( class.name, 'rectangle 0,0,0,0' )
		class.content = 'rectangle 0,0,0,0'
		class.type = 'rectangle'

	else
		class.type = class.content:gsub( '(%w+).*', '%1' ):lower()

	end

	return setmetatable( class, shape )
end
