
--
-- Rainmeter Meter manipulation module.
--
-- Provides an object-oriented wrapper around Rainmeter meters,
-- allowing scripts to query and modify meter properties, attach
-- mouse events, and dynamically load specialized meter handlers.
--
-- The module acts as a factory that returns a meter instance
-- corresponding to the meter type defined in the Rainmeter skin.
--
-- Supported built-in meter types include:
--
--  * string
--  * image
--  * shape
--
-- Additional meter types can be supported through submodules.
--
-- @module meter
-- @author
-- @version 2.4.3
--


local METEREVENTS = {}





--- Internal Rainmeter event dispatcher.
--
-- This function is invoked indirectly through Rainmeter
-- `!commandMeasure` bangs when a mouse action occurs on a meter.
-- It resolves the stored callback and invokes it with a structured
-- event object.
--
-- The event object contains information about the action type and,
-- when available, cursor coordinates.
--
-- @local
-- @param target Unique callback identifier.
-- @param action Event type (ex: `leftup`, `over`, `wheelup`).
-- @param mousex Cursor X position in pixels.
-- @param mousey Cursor Y position in pixels.
-- @param mousexs Cursor X position in percentage.
-- @param mouseys Cursor Y position in percentage.
--
function METERONMESSAGE( target, action, mousex, mousey, mousexs, mouseys )
	if METEREVENTS[ target ] then
		local response = {
			target = METEREVENTS[ target .. '_OUT' ].name,
			type = action
		}

		-- only in keydow ou keyup
		if mousex and mousex:match( '^(%d+)$' ) then
			response.cursor = {
				left  = tonumber( mousex ),
				leftp = tonumber( mousexs ),
				top   = tonumber( mousey ),
				topp  = tonumber( mouseys )
			}
		end


		METEREVENTS[ target ]( METEREVENTS[ target .. '_OUT' ], response )
	end
end




--- Rainmeter mouse event mapping.
--
-- Maps simplified event identifiers used by this module
-- to the corresponding Rainmeter meter options.
--
-- @local
local listEvents = {
	over         = 'MouseOverAction',
	leave        = 'MouseLeaveAction',
	wheelup      = 'MouseScrollUpAction',
	wheeldown    = 'MouseScrollDownAction',
	wheelleft    = 'MouseScrollLeftAction',
	wheelright   = 'MouseScrollRightAction',
	leftup       = 'LeftMouseUpAction',
	leftdown     = 'LeftMouseDownAction',
	leftdouble   = 'LeftMouseDoubleClickAction',
	rightup      = 'RightMouseUpAction',
	rightdown    = 'RightMouseDownAction',
	rightdouble  = 'RightMouseDoubleClickAction',
	middleup     = 'MiddleMouseUpAction',
	middledown   = 'MiddleMouseDownAction',
	middledouble = 'MiddleMouseDoubleClickAction',
	x1up         = 'X1MouseUpAction',
	x1down       = 'X1MouseDownAction',
	x1double     = 'X1MouseDoubleClickAction',
	x2up         = 'X2MouseUpAction',
	x2down       = 'X2MouseDownAction',
	x2double     = 'X2MouseDoubleClickAction',
}



--- Internal helper for dimension properties.
--
-- Provides getter/setter behavior for position and size
-- attributes (`X`, `Y`, `W`, `H`) of a Rainmeter meter.
--
-- @local
-- @param self Meter instance.
-- @param prop Meter property (`X`, `Y`, `W`, `H`).
-- @param value Optional value to assign.
-- @return string|table Current value when used as getter, or meter instance when used as setter.
local function dimension( self, prop, value )
	if value == nil then
		return rain:var( '['..self.name..':'..prop..']' )
	end

	rain:bang( '!setOption', self.name, prop, value )
	return self
end



--- Base meter class.
--
-- All meter objects inherit from this prototype. It exposes
-- common functionality shared by all meter types.
--
-- Instances are created indirectly through the module factory:
--
-- @usage
-- local meter = require("meter")
-- local clock = meter("ClockMeter")
--
-- clock:left(100)
-- clock:top(50)
-- clock:show()
local meter = {}
meter.__index = meter


--- Register, dispatch, or remove meter events.
--
-- This method manages mouse event handlers attached to a meter.
--
-- Behavior depends on the arguments provided.
--
-- **Register event**
--
-- @usage
-- meter:event("leftup", function(self, event)
--     print(event.type)
-- end)
--
-- **Trigger existing event**
-- @usage
-- meter:event("leftup")
--
-- **Remove event**
-- @usage
-- meter:event("leftup", false)
--
-- @param events Event name or multiple events separated by spaces.
-- @param callback Function executed when the event occurs.
-- @return string|table Returns the callback identifier or meter instance.
function meter:event( events, callback )

	-- fire event
	if callback == nil and self.EVENTLISTER[ listEvents[ events ]] then
		rain:bang(
			'!commandMeasure',
			rain.name,
			"METERONMESSAGE('".. self.EVENTLISTER[ listEvents[ events ]] .."','".. events .."')"
		)

		return self
	end


	local callID = tostring( callback ):gsub( ' ', '_' )

	for action in string.gmatch( events, '[^%s]+' ) do
		-- obtain the current value and reorganize, if necessary
		local value = self:option( listEvents[ action ])
		if value then
			value = value:sub( 1, 1 ) ~= '[' and '['.. value or value
			value = value:sub( #value, #value ) ~= ']' and value ..']' or value
		end

		if not METEREVENTS[ callID ] then
			METEREVENTS[ callID           ] = callback
			METEREVENTS[ callID .. '_OUT' ] = self
		end

		self.EVENTLISTER[ listEvents[ action ]] = callID
		rain:bang(
			'!setOption',
			self.name,
			listEvents[ action ],
			( value or '' ) ..
			"[!commandMeasure ".. rain.name .." METERONMESSAGE('".. callID .."','".. action .."','$MouseX$','$MouseY$','$MouseX:%$','$MouseY:%$')]"
		)

	end

	return callID
end



--- Get or set a Rainmeter meter option.
--
-- Provides direct access to the underlying Rainmeter
-- `!SetOption` bang while also allowing option retrieval.
--
-- **Getter**
--
-- @usage
-- local text = meter:option("Text")
--
-- **Setter**
--
-- @usage
-- meter:option("Text","Hello World")
--
-- @param option Name of the meter option.
-- @param value Optional value to assign.
-- @param config Optional configuration scope.
-- @return string|table Returns option value when used as getter or meter instance when used as setter.
--
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
function meter:option( option, value, config )
	if value ~= nil then
		rain:bang( '!setOption', self.name, option, value )
		return self

	else
		local result = rain:option( self.name, option, value )
		return result
	end
end



--- Get or set the meter Y position.
--
-- @param value Optional new Y coordinate.
-- @return string|table Current value or meter instance.
function meter:top( value )
	return dimension( self, 'Y', value )
end



--- Get or set the meter X position.
--
-- @param value Optional new X coordinate.
-- @return string|table Current value or meter instance.
function meter:left( value )
	return dimension( self, 'X', value )
end



--- Get or set the meter width.
--
-- @param value Optional new width.
-- @return string|table Current value or meter instance.
function meter:width( value )
	return dimension( self, 'W', value )
end



--- Get or set the meter height.
--
-- @param value Optional new height.
-- @return string|table Current value or meter instance.
function meter:height( value )
	return dimension( self, 'H', value )
end



--- Update the meter.
--
-- Forces Rainmeter to re-evaluate variables and measures
-- referenced by the meter.
--
-- @param force When true, forces a redraw by resetting
-- the primary content property before updating.
--
-- @usage
-- meter:update()
--
-- @usage
-- meter:update(true)
--
-- @return meter
function meter:update( force )
	-- Force update
	if force then
		if self.type ~= 'string' then
			rain:bang( '!setOption', self.name, 'text', '""' )
		else
			rain:bang( '!setOption', self.name, 'imageName', '""' )
		end
	end


	rain:bang( '!updateMeter', self.name )
	return self
end




--- Hide the meter.
--
-- Equivalent to the Rainmeter `!HideMeter` bang.
--
-- @usage
-- meter:hide()
--
-- @return meter
function meter:hide()
	rain:bang( '!hideMeter', self.name )
	return self
end



--- Show the meter.
--
-- Equivalent to the Rainmeter `!ShowMeter` bang.
--
-- @usage
-- meter:show()
--
-- @return meter
function meter:show()
	rain:bang( '!showMeter', self.name )
	return self
end









return setmetatable({}, {

	-- @param index is for module shape
	__call = function( self, name, index )
		local ok, tipo = pcall( string.lower, rain:option( name, 'meter' ))

		if not ok then
			error( 'Invalid meter "'.. name ..'"' )
			return -1
		end


		local meta = {
			name = name,
			type = tipo,
			super = meter,
			EVENTLISTER = {}
		}


		if tipo == 'string' then
			return setmetatable( meta, setmetatable( require( 'meter.string' ), meter )) end
		if tipo == 'image' then
			return setmetatable( meta, setmetatable( require( 'meter.image' ), meter )) end

		if tipo == 'shape' then
			local module = require( 'meter.shape' )
			return module( setmetatable( meta, meter ), name, index )
		end

		return setmetatable( meta, meter )
	end
})
