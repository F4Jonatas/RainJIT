

local M = {}
M.__index = M



return setmetatable( {}, {
	__call = function( _, options )
		local meta = {
			meter         = options.meter,
			points        = options.points or 10,
			paddingTop    = options.paddingTop,
			paddingBottom = options.paddingBottom,
			paddingLeft   = options.paddingLeft,
			paddingRight  = options.paddingRight,
			height        = options.height,
			width         = options.width
		}

		meta.spacing    = ( meta.width - meta.paddingLeft ) / meta.points
		meta.lines      = options.lines

		local copyLines
		for _, line in ipairs( meta.lines ) do
			for index, array in ipairs( line.data or { 0 } ) do
				if type( array ) == 'number' then
					array = { array }
				end


				for _ = 1, ( meta.points - #array ) do
					table.insert( array, 0 )
				end

				line.data[ index ] = array
			end
		end


	-- Stroke
	-- shape         = path path | stroke linearGradient gradient
	-- path          = 0,0 | lineTo 0,0
	-- gradient      = 180 | 102,219,252,20 ; 0 | 102,219,252,180 ; 0.2 | 102,219,252,180 ; 0.85 | 102,219,252,20 ; 1

	-- background
	-- shape2        = path path2 | strokeWidth 0 | fill linearGradient gradient2
	-- path2         = 0,0 | lineTo 0,0
	-- gradient2     = -90 | 102,219,252,86 ; 0 | 102,219,252,20 ; 1

	-- yAxis
	-- shape4        = path path4 | stroke color 255,255,255,200 | strokeWidth 2
	-- path4         = ( #WIDTH# - 20 ),9 | lineTo ( #WIDTH# - 8 ),9 | lineTo ( #WIDTH# - 8 ), ( #HEIGHT# - #MARGIN_TOP# - 8 ) | lineTo ( #WIDTH# - 20 ), ( #HEIGHT# - #MARGIN_TOP# - 8 )


		return setmetatable( meta, M )
	end
})