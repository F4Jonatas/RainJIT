-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

require( 'math.utils' )

local depot   = require( 'depot' )
local meter   = require( 'meter' )
local chart   = require( 'meter.chart.line' )
local measure = require( 'measure' )
local glass   = require( 'glass' )


local MAX_POINTS     = 40
local MAX_DOWNLOAD   = 0
local MAX_UPLOAD     = 0
local PADDING_TOP    = 15
local PADDING_BOTTOM = 5
local PADDING_LEFT   = 75
local PADDING_RIGHT  = 15
local HEIGHT         = rain:var( 'HEIGHT' ) - rain:var( '#MARGIN_TOP#' ) - 3
local WIDTH          = rain:var( 'WIDTH'  ) - PADDING_RIGHT
local SPACING        = ( WIDTH - PADDING_LEFT ) / MAX_POINTS

local DOWNLOAD_ARRAY = {}
local UPLOAD_ARRAY   = {}
for index = 1, MAX_POINTS do
	table.insert( DOWNLOAD_ARRAY, 0 )
	table.insert( UPLOAD_ARRAY  , 0 )
end


local dp = depot()
local downMaxValue = math.max( dp:get( 'download-max-value', 0 ), 0 )
local upMaxValue   = math.max( dp:get( 'upload-max-value'  , 0 ), 0 )

local netIn       = measure( 'netIn' )
local netout      = measure( 'netOut' )
local value       = meter( 'value' )
local valueMax    = meter( 'value.max' )
local downGraph   = meter( 'graphic' )
local downGraphBG = downGraph:shape(2)
local upGraph     = downGraph:shape(3)
local loading     = downGraph:shape(5)


-- local grapth = chart({
-- 	meter         = downGraph,
-- 	points        = MAX_POINTS,
-- 	paddingTop    = PADDING_TOP,
-- 	paddingBottom = PADDING_BOTTOM,
-- 	paddingLeft   = PADDING_LEFT,
-- 	paddingRight  = PADDING_RIGHT,
-- 	height        = HEIGHT,
-- 	width         = WIDTH,

-- 	lines         = {
-- 		{
-- 			strokecolor = '102,219,252',
-- 			fill = '102,219,252',
-- 			data = { 0 }
-- 		},
-- 		{
-- 			strokecolor = '255,80,220',
-- 			fill = 'transparent',
-- 			data = { 0 }
-- 		}
-- 	}
-- })


-- yAxis
-- downGraph:shape(4)
-- 	:polyline('380,9 l 392,9 l 392,97 l 380,97')


-- Forward declarations
local addPoint



--- Convert a byte count to a human-readable string with binary prefixes (IEC).
-- @param bytes number The number of bytes to format (must be non-negative).
-- @return string A formatted string like "12.34 MB" (two decimal places).
-- @usage local size = fmt_bytes(1234567) -- yields "1.18 MB"
local fmt_bytes = setmetatable(
	-- static
	{ prefixes = { 'B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB' }},

	-- function
	{ __call = function( self, bytes )
		if bytes >= 1024 then
			local index = 0

			while bytes >= 1024 do
				bytes = bytes / 1024
				index = index + 1
			end

			return ('%.2f %s'):format( bytes, self.prefixes[ index + 1 ])

		else
			return ('%.f B'):format( bytes )

		end
	end
	}
)



if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



netIn:event( 'update', function( self, response )
	local download = self:value()
	local upload   = netout:value()

	addPoint( download, upload )

	value:text(
		fmt_bytes( upload ) ..' ↑\n'..
		fmt_bytes( download ) ..' ↓'
	):update()
end)



local function calcY( value, max, height )
	if max == 0 then
		return ( height - PADDING_BOTTOM )
	end

	local y = ( height - PADDING_BOTTOM ) - ( value / max ) * ( height - ( PADDING_TOP + PADDING_BOTTOM ))
	return math.min( height, math.max( 0, y ))
end



local function redrawPath()
	local lastX
	local MAX = math.max( MAX_DOWNLOAD, MAX_UPLOAD )

	valueMax:text( fmt_bytes( MAX )):update()

	local dPath = {}
	for index, value in ipairs( DOWNLOAD_ARRAY ) do
		local x = PADDING_LEFT + ( index * SPACING )
		local y = calcY( value, MAX, HEIGHT )

		if index == 1 then
			lastX = PADDING_LEFT
		else
			lastX = x
		end

		table.insert( dPath, x )
		table.insert( dPath, y )
	end

	-- Tabela para upload
	local uPath = {}
	for index, value in ipairs( UPLOAD_ARRAY ) do
		local x = PADDING_LEFT + ( index * SPACING )
		local y = calcY( value, MAX, HEIGHT )
		table.insert( uPath, x )
		table.insert( uPath, y )
	end


	local bgPath = {
		PADDING_LEFT,
		HEIGHT - PADDING_BOTTOM
	}

	for _, v in ipairs( dPath ) do
		table.insert( bgPath, v )
	end

	table.insert( bgPath, lastX )
	table.insert( bgPath, HEIGHT - PADDING_BOTTOM )


	downGraph:polyline( dPath )
	upGraph:polyline( uPath )
	downGraphBG:polyline( bgPath )

	downGraph:update()
end



function addPoint( download, upload )
	table.insert( DOWNLOAD_ARRAY, download )
	table.insert( UPLOAD_ARRAY  , upload   )

	if #DOWNLOAD_ARRAY > MAX_POINTS then
		table.remove( DOWNLOAD_ARRAY, 1 )
		table.remove( UPLOAD_ARRAY  , 1 )
	end

	MAX_DOWNLOAD = math.maximo( DOWNLOAD_ARRAY )
	MAX_UPLOAD   = math.maximo( UPLOAD_ARRAY )

	redrawPath()
end
