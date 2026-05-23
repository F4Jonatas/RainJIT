
--[[
	Author: F4Jonatas
	Version: 2.6.2
	https://docs.rainmeter.net/manual/measures/
--]]

local MEASUREEVENTS = {}
function MEASUREONMESSAGE( target, action )
	if MEASUREEVENTS[ target ] then
		local response = {
			type = action
		}

		MEASUREEVENTS[ target ]( MEASUREEVENTS[ target ..'_OUT' ], response )
	end
end





local listEvents = {
	-- General
	update        = 'OnUpdateAction',
	change        = 'OnChangeAction',

	-- WebParser
	finish        = 'FinishAction',
	connecterror  = 'OnConnectErrorAction',
	regexperror   = 'OnRegExpErrorAction',
	downloaderror = 'OnDownloadErrorAction',

	-- https://github.com/brianferguson/HotKey.dll
	keydown       = 'keydownaction',
	keyup         = 'keyupaction',

	-- NowPlaying
	trackchange   = 'TrackChangeAction'
}





local measure = {}
measure.__index = measure
measure.EVENTLISTER = {}



function measure:bang( command, ... )
	rain:bang(
		command:match( '^(!)' ) and command or '!'.. command,
		self.name,
		...
	)
end



function measure:command( command )
	rain:bang( '!commandMeasure', self.name, command )
end



--[[
	Create events, send events or remove events

	Create event
	Meter:Event( "LeftUp", function( event )
		print( event )
	end )

	Send function, if já added in meter
	Meter:Event( "LeftUp" )

	Remove event added
	Meter:Event( "LeftUp", false )
--]]
function measure:event( actions, callback )
	if callback == nil then
		if self.super.EVENTLISTER[ listEvents[ actions ]] then
			rain:bang(
				'!commandMeasure',
				self.name,
				"_G[1].MEASUREONMESSAGE('" .. self.super.EVENTLISTER[ listEvents[ actions ]] .. "','" .. actions .. "')" )
			return self

		else
			return -1

		end
	end


	actions = actions:lower()
	local callID = tostring( callback ):gsub( ' ', '_' )
	for Action in string.gmatch( actions, '[^%s]+' ) do
		MEASUREEVENTS[ callID ] = callback
		MEASUREEVENTS[ callID ..'_OUT' ] = self

		self.super.EVENTLISTER[ listEvents[ Action ]] = callID
		rain:bang(
			'!setOption',
			self.name,
			listEvents[ Action ],
			"!commandMeasure ".. rain.name .." MEASUREONMESSAGE('" .. callID .. "','" .. Action .. "')"
		)

	end
end



--[[
	https://docs.rainmeter.net/manual/lua-scripting/#GetValue
--]]
function measure:value()
	return rain:var( '[&'..self.name..']' )
end



--[[
	!DisableMeasure, !ToggleMeasure
--]]
function measure:disable()
	rain:bang( '!disableMeasure', self.name )
end



--[[
--]]
function measure:update()
	rain:bang( '!updateMeasure', self.name )
	return self
end



--[[
--]]
function measure:enable()
	rain:bang( '!enableMeasure', self.name )
	return self
end



--[[
	Set or change options in measure.

	Measure:option(
		[string/table/required] Name of the option.
		[string/integer/optional] New value to be set.
		[string/optional]
	)

	Get:
		Measure:option( "Text" )

	Set:
		Measure:option( "Text", "Module Measure" )
		Measure:option({ Text = "Module Measure", W = 200 })

	Docs:
		https://docs.rainmeter.net/manual/bangs/#SetOption
--]]
function measure:option( option, value )
	if value ~= nil then
		rain:bang( '!setOption', self.name, option, value )
		return self

	else
		return self:option( option )

	end
end



return setmetatable( {}, {
	__call = function( _, name )
		local ok, TYPE = pcall( string.lower, rain:option( name, 'measure' ))

		if not ok then
			error( 'Invalid measure "'.. name ..'"' )
			return -1
		end

		local plugin = rain:option( name, 'plugin' )
		local meta = {
			name = name,
			type = TYPE,
			super = measure,
			plugin = plugin and plugin:lower():gsub( '((%.dll)?)$', '' ) or nil
		}

		return setmetatable( meta, measure )
	end
})