

require( 'math.utils' )


local function calcY( value, max, height )
	if max == 0 then return height end

	local y = ( height - PADDING_BOTTOM ) - ( value / max ) * ( height - ( PADDING_TOP + PADDING_BOTTOM ))
	return math.min( height, math.max( 0, y ))
end






local M = {}
M.__index = M



function M:addLine( shape )
	if type( shape ) == 'table' then
		-- table.insert( self.line, shape:add()
	end
end


function M:update()
	local lastX
	local points = {}

	-- local MAX    = math.max( MAX_DOWNLOAD, MAX_UPLOAD )

	-- MAX_DOWNLOAD = math.maximo( DOWNLOAD_ARRAY )
	-- MAX_UPLOAD   = math.maximo( UPLOAD_ARRAY )

	-- for index, value in ipairs( DOWNLOAD_ARRAY ) do
	-- 	local x = PADDING_LEFT + ( index * SPACING )
	-- 	local y = calcY( value, MAX, HEIGHT )

	-- 	if index == 1 then
	-- 		lastX = PADDING_LEFT
	-- 	else
	-- 		lastX = x
	-- 	end

	-- 	table.insert( points, x )
	-- 	table.insert( points, y )
	-- end
end





return setmetatable( {}, {
	__call = function( _, options )
		local meta = {
			shape         = options.meter,
			points        = options.points or 10,
			paddingTop    = options.paddingTop or 0,
			paddingBottom = options.paddingBottom or 0,
			paddingLeft   = options.paddingLeft or 0,
			paddingRight  = options.paddingRight or 0,
			height        = options.height,
			width         = options.width -- and ( options.width - options.paddingRight ) or 0
		}

		meta.spacing = options.spacing and options.spacing or (( meta.width - meta.paddingLeft ) / meta.points )
		meta.lines   = options.lines
		meta._paths  = {
			-- 1: yAxis
			-- 2: line grath 1
			-- 3: background grath 1
			-- 4: line grath 2
			-- 5: background grath 2
			-- ...
		}

		-- Cada prorpriedade line deve conter uma propriedade de data, que será um array de números
		-- Caso esteja faltando número no array ele adciona.
		for _, line in ipairs( meta.lines ) do
			for index, array in ipairs( line.data or { 0 } ) do
				for i = 1, ( meta.points - #line.data ) do
					table.insert( line.data, 0 )
				end
			end
		end


		-- Cria a barra yAxis
		if options.yAxis ~= false then
			local width = 2
			meta._paths[1] = meta.shape:add()
				:strokecolor( 255, 255, 255, 200 )
				:strokewidth( width )
				:path(('%d,%d| L %d,%d | L %d,%d | L %d,%d'):format(
					meta.width - width - 18, 9,
					meta.width - width - 6, 9,
					meta.width - width - 6, meta.height - meta.paddingTop - width,
					meta.width - width - 18, meta.height - meta.paddingTop - width
				))
				:update()
		end


		for _, line in ipairs( meta.lines ) do
			local stroke = false
			local background = false

			if line.strokewidth ~= false then
				stroke = meta.shape:add()
					:strokecolor( line.strokecolor )
					:strokewidth( strokewidth )
			end


			if line.fill ~= false then
				background = meta.shape:add()
					:fill( line.fill )
			end


			table.insert( meta._paths, stroke )
			table.insert( meta._paths, background )
		end


		local class = setmetatable( meta, M )
		class:update()

		return class
	end
})