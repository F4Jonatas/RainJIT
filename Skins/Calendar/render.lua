--- Visual layer of the calendar skin.
-- Draws dots, tooltips, applies the glass effect, sets the weekday labels
-- and refreshes the skin group. Reads state from `core.lua`.
-- @license GPL v2.0 License

local core    = require( 'core'    )
local glass   = require( 'glass'   )
local meter   = require( 'meter'   )
local i18n    = require( 'i18n'    )
local tooltip = require( 'tooltip' )

local render  = {}

local body    = meter( 'body' )


-- Apply the glass effect
if core.dp:get( 'glass', true ) then
	glass( rain.hwnd, {
		effect  = 'mica_alt',
		dark    = true,
		corners = 'round'
	})
end


for i, key in ipairs( core.WEEKDAYS ) do
	rain:bang(
		'setOption',
		'weekday'.. i,
		'text',
		i18n( key )
	)
end



--- Returns the accent color of an event.
-- The color is picked from `ACCENT_COLOR` using a hash of the text, so it looks
-- random between events but stays the same across rebuilds.
-- @param (string) text - Event text.
-- @return (string) Hex color, e.g. `00858F`.
function render.eventcolor( text )
	local hash = 0

	for i = 1, #text do
		hash = ( hash * 31 + text:byte( i )) % 2147483647
	end

	return core.ACCENT_COLOR[ hash % #core.ACCENT_COLOR + 1 ]
end



--- Draws a filled circle on the `body` meter.
-- @param (number) x - Center X.
-- @param (number) y - Center Y.
-- @param (number) radius - Circle radius.
-- @param (string) color - Fill color.
function render.drawdot( x, y, radius, color )
	body:add():ellipse( x, y, radius ):fill( color ):strokewidth( 1 )
end



--- Clears every shape of the `body` meter, except shape 1 (the background).
function render.clearbody()
	for i = 2, body:length() do
		body:option( 'shape'.. i, '' )
	end
end



--- Updates the skin group and redraws the skin.
function render.refreshgroup()
	rain:bang( 'updateMeterGroup', core.GROUP )
	rain:bang( 'redraw' )
end



--- Meter callback event. Create and destroy tooltips.
local activeTT
local function mouseevent( self, event )
	if event.type == 'over' then
		activeTT = tooltip( core.tooltipText[ self.name ], {
			title           = 'List of Events',
			backdrop        = 'acrylic',
			backdropCorner  = 'round',
			dark            = true,
			fadeDuration    = 200,
			backgroundColor = 0x00000080,
		})

	else
		activeTT:destroy()
	end
end



--- Writes tooltips and draws the dots of every cell with events.
-- All events appear in the tooltip. At most `MAX_DOTS` dots are drawn, centered
-- at the bottom of the cell; the moon dot goes in the top-right corner.
function render.render()
	local c = core.cellsize()

	for index, list in pairs( core.events ) do
		local m       = meter( core.dayname( index ))
		local x, y    = core.cellpos( index )
		local lines   = {}
		local stacked = {}

		for _, kind in ipairs( core.ORDER ) do
			for _, text in ipairs( list[ kind ] or {} ) do
				table.insert( lines, text )

				if core.STACKED[ kind ] then
					table.insert( stacked, text )
				end
			end
		end

		core.tooltipText[ m.name ] = '• '.. table.concat( lines, '\n• ' )
		m:event( 'over leave', mouseevent )

		-- Centered dots
		local total = math.min( #stacked, core.MAX_DOTS )
		for i = 1, total do
			local dx = x + ( c.w / 2 ) + ( i - ( total + 1 ) / 2 ) * core.DOT_SPACING
			render.drawdot( dx, y + c.h, core.DOT_RADIUS, render.eventcolor( stacked[ i ] ))
		end

		-- Moon dot
		if list.moon then
			render.drawdot( x + c.w - 2, y + 2, core.DOT_RADIUS, core.MOON_COLOR )
		end
	end

	render.refreshgroup()
end



return render
