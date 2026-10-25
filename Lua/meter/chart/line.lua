--- Line chart extension.
-- Provides a **builder-style API** for constructing and updating
-- animated line charts using Rainmeter **Shape meters** as the
-- rendering backend.
--
-- This module does not draw directly with Rainmeter primitives — it
-- generates and updates one or more `path`/`polyline` shapes (via the
-- `meter.shape` submodule) representing each data series, recalculating
-- point coordinates whenever new data is pushed.
--
-- Each series may optionally have:
--   a stroke line (the visible line of the chart)
--   a filled background (area under the line)
--   custom dash patterns and caps for the stroke
--
-- An optional Y-axis reference bar can also be generated automatically.
--
-- The module keeps a fixed-size rolling window of data points per series
-- (`options.points`), discarding the oldest value whenever a new one is
-- added beyond that limit — making it suitable for live/streaming charts
-- (e.g. CPU usage, network throughput).
--
-- Typical usage example:
--
-- @usage
-- local shape = require("meter")
-- local chartLine = require("chart.line")
--
-- local chart = chartLine({
--   meter         = meter("graph"),
--   points        = 30,
--   width         = 200,
--   height        = 100,
--   paddingTop    = 10,
--   paddingBottom = 10,
--   paddingLeft   = 10,
--   paddingRight  = 10,
--   yAxis         = true,
--   series = {
--     {
--       strokecolor  = "0, 255, 0, 255",
--       strokewidth  = 2,
--       fill         = "0, 255, 0, 60",
--       data         = { 10, 20, 15, 30 }
--     }
--   }
-- })
--
-- Pushing new values and redrawing the chart:
--
-- @usage
-- chart:addPoint( { 42 } ) -- one value per series, in order
--
-- Updating without triggering an automatic redraw:
--
-- @usage
-- chart:addPoint( { 42 }, false )
-- chart:update()
--
-- @submodule chart.line
-- @release 0.1.0
-- @author F4Jonatas
-- @license GPL v2.0 License
-- @see https://docs.rainmeter.net/manual/meters/shape/


require( 'math.utils' )

local M = {}
M.__index = M



local function calcY( value, max, height, self )
	local padding = ( self.paddingTop + self.paddingBottom )

	if max == 0 then
		return ( height - padding )
	end

	local y = ( height - padding ) - (( value / max ) * ( height - padding ))
	return math.min( height, math.max( self.lineWidth, y ))
end







-- @return max value of series data
function M:addPoint( points, update )
	for index, value in ipairs( points ) do
		table.insert( self.series[ index ].data, value )

		if #self.series[ index ].data > self.points then
			table.remove( self.series[ index ].data, 1 )
		end
	end

	if update ~= false then
		return self:update()
	end

	return 0
end



-- @return max value of series data
function M:update()
	local lastX
	local max = self.lineWidth

	for _, serie in ipairs( self.series ) do
		max = math.max( math.maximo( serie.data ), max )
	end


	for _, serie in ipairs( self.series ) do
		local points = {}

		for index, value in ipairs( serie.data ) do
			local x = self.paddingLeft + (( index - 1 ) * self.spacing )
			local y = calcY( value, max, self.height, self )
			lastX = x

			table.insert( points, x )
			table.insert( points, y )
		end


		if serie._fill then
			local bgPath = {
				self.paddingLeft,
				self.height - ( self.paddingTop + self.paddingBottom )
			}

			for index, value in ipairs( points ) do
				table.insert( bgPath, value )
			end

			table.insert( bgPath, lastX )
			table.insert( bgPath, bgPath[2] )

			serie._fill:polyline( bgPath )
		end


		if serie._stroke then
			serie._stroke:polyline( points )
		end

	end

	self.shape:update()
	return max
end





return setmetatable( {}, {
	__call = function( _, options )
		local meta = {
			shape         = options.meter,
			points        = options.points        or 10,
			paddingTop    = options.paddingTop    or 0,
			paddingBottom = options.paddingBottom or 0,
			paddingLeft   = options.paddingLeft   or 0,
			paddingRight  = options.paddingRight  or 0,
			height        = options.height,
			width         = options.width, -- and ( options.width - options.paddingRight ) or 0
			lineWidth     = 2
		}

		meta.spacing = options.spacing and options.spacing or (( meta.width - meta.paddingLeft - meta.paddingRight ) / math.max( meta.points, 1 ))
		meta.series  = options.series

		-- Cada prorpriedade line deve conter uma propriedade de data, que será um array de números
		-- Caso esteja faltando número no array ele adiciona.
		for _, serie in ipairs( meta.series ) do
			serie.data = serie.data or { 0 } -- fallback

			for index, array in ipairs( serie.data ) do
				for i = 1, ( meta.points - #serie.data ) do
					table.insert( serie.data, 0 )
				end
			end
		end


		-- Cria a barra yAxis
		if options.yAxis ~= false then
			local padding = ( meta.paddingTop + meta.paddingBottom )
			meta._yAxis = meta.shape:add()
				:strokecolor( 255, 255, 255, 200 )
				:strokewidth( meta.lineWidth )
				:path(('%d,%d | L %d,%d | L %d,%d | L %d,%d'):format(
					meta.width - meta.lineWidth - ( meta.paddingRight - meta.lineWidth ), 0,
					meta.width - meta.lineWidth - 6, 0,
					meta.width - meta.lineWidth - 6, meta.height - padding,
					meta.width - meta.lineWidth - ( meta.paddingRight - meta.lineWidth ), meta.height - padding
				))
				:update()
		end


		for _, line in ipairs( meta.series ) do
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