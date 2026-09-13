-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

local depot   = require( 'depot' )
local meter   = require( 'meter' )
local chart   = require( 'meter.chart.line' )
local measure = require( 'measure' )
local glass   = require( 'glass' )


local dp       = depot()
local netIn    = measure( 'netIn' )
local netout   = measure( 'netOut' )
local value    = meter( 'value' )
local valueMax = meter( 'value.max' )



local grapth = chart({
	meter         = meter( 'graphic' ),
	points        = 40,
	paddingTop    = 9,
	paddingBottom = 8,
	paddingLeft   = 90,
	paddingRight  = 20,
	height        = rain:var( 'HEIGHT' ) - 35,
	width         = rain:var( 'WIDTH' ),

	series        = {
		{
			strokewidth = 1,
			strokecolor = '180 | 102,219,252,20 ; 0 | 102,219,252,180 ; 0.2 | 102,219,252,180 ; 0.85 | 102,219,252,20 ; 1',
			fill = '-90 | 102,219,252,86 ; 0 | 102,219,252,20 ; 1',
			data = { 0 }
		},
		{
			strokewidth = 1,
			strokecolor = '180 | 255,80,220,20 ; 0 | 255,80,220,255 ; 0.2 | 255,80,220,255 ; 0.85 | 255,80,220,20 ; 1',
			strokedashes = { 5, 3 },
			strokedashcap = 'round',
			fill = false,
			data = { 0 }
		}
	}
})



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

	local max = grapth:addPoint({ download, upload })
	valueMax:text( fmt_bytes( max )):update()

	value:text(
		fmt_bytes( upload ) ..' ↑\n'..
		fmt_bytes( download ) ..' ↓'
	):update()
end)
