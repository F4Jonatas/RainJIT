
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
--  String
--  Image
--  Shape
--
-- Additional meter types can be supported through submodules.
--
-- @module meter
-- @release 2.4.3
-- @author F4Jonatas
-- @license GPL v2.0 License


local METEREVENTS = {}


--- Internal Rainmeter event dispatcher.
-- This function is invoked indirectly through Rainmeter
-- `!commandMeasure` bangs when a mouse action occurs on a meter.
-- It resolves the stored callback and invokes it with a structured
-- event object.
--
-- The event object contains information about the action type and,
-- when available, cursor coordinates.
--
-- @param (string) target - Unique callback identifier
-- @param (string) action - Event type (eg: `"leftup"`, `"over"`, `"wheelup"`)
-- @param (number) mousex - Cursor X position in pixels
-- @param (number) mousey - Cursor Y position in pixels
-- @param (number) mousexs - Cursor X position in percentage
-- @param (number) mouseys - Cursor Y position in percentage
function METERONMESSAGE( target, action, mousex, mousey, mousexs, mouseys )
	if METEREVENTS[ target ] then
		local response = {
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


		METEREVENTS[ target ]( METEREVENTS[ target ..'_OUT' ], response )
	end
end




--- Rainmeter mouse event mapping.
-- Maps simplified event identifiers used by this module
-- to the corresponding Rainmeter meter options.
--
-- @see https://docs.rainmeter.net/manual/mouse-actions/
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
-- Provides getter/setter behavior for position and size
-- attributes (`X`, `Y`, `W`, `H`) of a Rainmeter meter.
--
-- @param (table) self - Meter instance.
-- @param (string|number) prop - Meter property (`X`, `Y`, `W`, `H`).
-- @param (string) [value] - value to assign.
-- @return (string|table) Current value when used as getter, or meter instance when used as setter.
local function dimension( self, prop, value )
	if value == nil then
		return rain:var( '[' ..self.name.. ':' ..prop.. ']' )
	end

	rain:bang( '!setOption', self.name, prop, value )
	return self
end


--- Internal helper to compute and apply the transformation matrix.
-- Combines uniform scale (applied from the meter's center) with
-- user-defined X/Y translation into a single Rainmeter
-- `TransformationMatrix` value, then applies it via `!SetOption`.
--
-- Reads `self._scale`, `self._origW`, `self._origH`, `self._userTx`
-- and `self._userTy` (all default to a neutral value when unset),
-- so it can be called safely even before `scale()` or
-- `translatex()`/`translatey()` have been used.
--
-- @param (table) self - Meter instance.
-- @return (string) The resulting matrix string that was applied.
local function updateMatrix( self )
	local scale  = self._scale  or 1
	local width  = self._origW  or 0
	local height = self._origH  or 0
	local uTX    = self._userTx or 0
	local uTY    = self._userTy or 0

	-- Translation required to maintain the center (scale from the center)
	local centerTX = width  * ( 1 - scale ) / 2
	local centerTY = height * ( 1 - scale ) / 2

	local TX = centerTX + uTX
	local TY = centerTY + uTY

	local matrix = string.format( '%s;0;0;%s;%s;%s', scale, scale, TX, TY )
	rain:bang( '!setOption', self.name, 'transformationMatrix', matrix )
	return matrix
end



--- Base meter class.
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
-- This method manages mouse event handlers attached to a meter.
--
-- Behavior depends on the arguments provided.
--
-- @param (string) events - Event name or multiple events separated by spaces
-- @param (function|nil) [callback] - Function executed when the event occurs
-- @return (string|table) Returns the callback identifier or meter instance
--
-- **Register event**
-- @usage
-- meter:event("leftup", function(self, event)
--   print(event.type)
-- end)
--
-- **Trigger existing event**
-- @usage meter:event("leftup")
--
-- **Remove event**
-- @usage meter:event("leftup", false)
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


	-- remove event
	if callback == false then
		for action in string.gmatch( events, '[^%s]+' ) do
			local callID = self.EVENTLISTER[ listEvents[ action ]]

			if callID then
				METEREVENTS[ callID           ] = nil
				METEREVENTS[ callID .. '_OUT' ] = nil
				self.EVENTLISTER[ listEvents[ action ]] = nil
			end

			rain:bang( '!setOption', self.name, listEvents[ action ], '' )
		end

		return self
	end


	local callID = ( tostring( callback ) ..'_'.. self.name ):gsub( '[%s:]', '_' )

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
-- Provides direct access to the underlying Rainmeter
-- `!SetOption` bang while also allowing option retrieval.
--
-- @param (string) option - Name of the meter option.
-- @param (string|number|nil) [value] - Optional value to assign.
-- @param (string) [config] - Optional configuration scope.
-- @return (string|table) Returns option value when used as getter or meter instance when used as setter.
--
-- **Getter**
-- @usage local text = meter:option("Text")
--
-- **Setter**
-- @usage meter:option("Text","Hello World")
--
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
function meter:option( option, value, config )
	if value ~= nil then
		rain:bang( '!setOption', self.name, option, value )
		return self

	else
		return rain:option( self.name, option )
	end

end



--- Get or set the meter Y position.
-- @param (string|number) value - Optional new Y coordinate.
-- @return (string|table) Current value or meter instance.
function meter:top( value )
	return dimension( self, 'Y', value )
end



--- Get or set the meter X position.
-- @param (string|number) value - Optional new X coordinate.
-- @return (string|table) Current value or meter instance.
function meter:left( value )
	return dimension( self, 'X', value )
end



--- Get or set the meter width.
-- @param (string|number) value - Optional new width.
-- @return (string|table) Current value or meter instance.
function meter:width( value )
	return dimension( self, 'W', value )
end



--- Get or set the meter height.
-- @param (string|number) value - Optional new height.
-- @return (string|table) Current value or meter instance.
function meter:height( value )
	return dimension( self, 'H', value )
end



--- Get or set the vertical translation offset.
-- Translation composes with any active `scale()` — updating this
-- value recalculates the full transformation matrix rather than
-- overwriting it, so scale and translation stay in sync.
--
-- @param (number) [value] - Vertical offset in pixels. Omit to use as getter.
-- @return (number|table) Current offset (default `0`) when used as getter,
--   or the meter instance when used as setter.
--
-- @usage
-- meter:translatey(10)     -- setter
-- local ty = meter:translatey() -- getter
function meter:translatey( value )
	if value ~= nil then
		self._userTy = value
		updateMatrix( self )
		return self

	else
		return self._userTy or 0
	end
end




--- Get or set the horizontal translation offset.
-- Translation composes with any active `scale()` — updating this
-- value recalculates the full transformation matrix rather than
-- overwriting it, so scale and translation stay in sync.
--
-- @param (number) [value] - Horizontal offset in pixels. Omit to use as getter.
-- @return (number|table) Current offset (default `0`) when used as getter,
--   or the meter instance when used as setter.
--
-- @usage
-- meter:translatex(10)     -- setter
-- local tx = meter:translatex() -- getter
function meter:translatex( value )
	if value ~= nil then
		self._userTx = value
		updateMatrix( self )
		return self

	else
		return self._userTx or 0
	end
end




--- Get or set uniform scale, applied from the meter's center.
-- On first use, captures the meter's original width/height so the
-- scale transformation can be centered correctly. Composes with any
-- active translation (`translatex()`/`translatey()`) via the shared
-- transformation matrix.
--
-- @param (number) [value] - Scale factor (e.g. `1.5`). Omit to use as getter.
-- @return (number|table) Current scale factor when used as getter,
--   or the meter instance when used as setter.
--
-- @usage
-- meter:scale(1.5)     -- setter
-- local s = meter:scale() -- getter
function meter:scale( value )
	if value == nil then
		return self._scale
	end

	-- The first time, it captures the original dimensions of the meter.
	if not self._origW then
		self._origW = tonumber( rain:var( '[&'.. self.name ..':W]' )) or 0
		self._origH = tonumber( rain:var( '[&'.. self.name ..':H]' )) or 0
	end

	self._scale = value
	updateMatrix( self )
	return self
end




--- Update the meter.
-- Forces Rainmeter to re-evaluate variables and measures
-- referenced by the meter.
--
-- @param (boolean|nil) force - When true, forces a redraw by resetting
--   the primary content property before updating.
-- @return (table) meter
--
-- @usage
-- meter:update()
-- meter:update(true)
--
-- https://docs.rainmeter.net/manual/bangs/#UpdateMeter
function meter:update( force )
	-- Force update
	if force == true then
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
-- Equivalent to the Rainmeter `!HideMeter` bang.
--
-- @return (table) meter
--
-- @usage meter:hide()
-- @see https://docs.rainmeter.net/manual/bangs/#ShowHideToggleMeter
function meter:hide()
	rain:bang( '!hideMeter', self.name )
	return self
end



--- Show the meter.
-- Equivalent to the Rainmeter `!ShowMeter` bang.
--
-- @return (table) meter
--
-- @usage meter:show()
-- @see https://docs.rainmeter.net/manual/bangs/#ShowHideToggleMeter
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
		end


		local meta = {
			name = name,
			type = tipo,
			super = meter,
			EVENTLISTER = {},
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
