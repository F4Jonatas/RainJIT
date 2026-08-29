

require( 'math.utils' )


local function calcY( value, max, height, self )
	if max == 0 then
		return ( height - self.paddingBottom )
	end

	local y = ( height - self.paddingBottom ) - ( value / max ) * ( height - ( self.paddingTop + self.paddingBottom ))
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
	local max = 0

	for _, line in ipairs( self.lines ) do
		max = math.max( math.maximo( line.data ), max )
	end

	for _, line in ipairs( self.lines ) do
		local points = {}

		for index, value in ipairs( line.data ) do
			local x = ( self.paddingLeft - self.paddingRight ) + ( index * self.spacing )
			local y = calcY( value, max, self.height - self.paddingTop + self.lineWidth, self )

			if index == 1 then
				lastX = self.paddingLeft - self.paddingRight
			else
				lastX = x
			end

			table.insert( points, x )
			table.insert( points, y )
		end


		if line._fill then
			-- line._fill:polyline( points )
			-- print(line._fill.contentPath )
		end

		if line._stroke then
			line._stroke:polyline( points )
			-- print(line._stroke.contentPath)
			-- print( table.concat( points, ', ' ) )
		end
	end


	self.shape:update()
	-- print( self._yAxis.content )
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
			width         = options.width, -- and ( options.width - options.paddingRight ) or 0
			lineWidth     = 2
		}

		meta.spacing = options.spacing and options.spacing or (( meta.width - meta.paddingLeft ) / meta.points )
		meta.lines   = options.lines

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
			meta._yAxis = meta.shape:add()
				:strokecolor( 255, 255, 255, 200 )
				:strokewidth( width )
				:path(('%d,%d| L %d,%d | L %d,%d | L %d,%d'):format(
					meta.width - width - 18, 9,
					meta.width - width - 6, 9,
					meta.width - width - 6, meta.height - meta.paddingTop - ( width * 2 ),
					meta.width - width - 18, meta.height - meta.paddingTop - ( width * 2 )
				))
				:update()
		end


		for _, line in ipairs( meta.lines ) do
			local stroke     = false
			local background = false

			if line.strokewidth ~= false then
				stroke = meta.shape:add()
					:strokecolor( line.strokecolor )
					:strokewidth( line.strokewidth )

				if line.strokedashes then
					stroke:strokedashes( line.strokedashes[1], line.strokedashes[2] )
				end

				if line.strokedashcap then
					stroke:strokedashcap( line.strokedashcap )
				end
			end


			if line.fill ~= false then
				background = meta.shape:add()
					:fill( line.fill )
			end

			line._stroke = stroke
			line._fill   = background
		end


		local class = setmetatable( meta, M )
		class:update()

		return class
	end
})