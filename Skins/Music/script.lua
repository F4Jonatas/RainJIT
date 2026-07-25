-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

-- Requeste every second for get current music
-- https://api.radio.de/stations/now-playing?stationIds=RADIOSTATION


local depot   = require( 'depot' )
local meter   = require( 'meter' )
local hotkey  = require( 'hotkey' )
local glass   = require( 'glass' )
local fetch   = require( 'fetch' )
local luacom  = import( 'luacom' )

local dp       = depot()
local body     = meter( 'body' )
local song     = meter( 'song' )
local radio    = { 'energy98', 'jbfm', 'hunterpop' }
local iRadio   = dp:get( 'radio-index', 1 )


-- Forward declarations
local downloadCover
local streamLink
local radioTitle
local keyboardEvent
local loadRadio

if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect  = 'acrylic', corners = 'round' })
end





-- luacom.config.abort_on_API_error = true
-- luacom.config.abort_on_error = false
local player = luacom.CreateObject( 'WMPlayer.OCX' )
player.uiMode = 'invisible'






local play = meter( 'controls-play_plause', 2 )
-- rain:var("path", play.contentPath, rain:var("#CURRENTPATH#skin.ini"))



-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
player.state = dp:get( 'radio-state', 3 )
if player.state == 3 then
	player.settings.autoStart = true

	play
		:path( 'M 13 7 L 9 7 V 21 L 13 21 V 7 M 15 7 L 19 7 V 21 L 15 21 V 7 Z' )
		:update()
end

local function loadTitle()
	-- print( player.state, player.playState )
	if player.state ~= 3 then return end

	-- Precisa ser o async metodo, caso contrário sempre vai ter um momento de freeze em todo processo do rainmeter
	fetch.async( 'https://api.radio.de/stations/now-playing?stationIds='.. radio[ iRadio ])
		:callback( function( self, response )
			local data = response:json()

			if #data > 0 then
				local music  = data[1].title:match( '%s*-%s*(.*)$' )
				local artist = data[1].title:match( '^(.*)%s*-%s*' )
				song:text( music ..'\n'.. artist ):update()

			else
				song:text( 'Listing\n'.. radioTitle ):update()
			end
		end)
		:send()
end



function loadRadio()
	local request = fetch( 'https://prod.radio-api.net/stations/details?stationIds='.. radio[ iRadio ])
	-- print( fetch( 'https://www.youtube.com/watch?v=m52ynxt1mOo' ):text() )

	local data = request:json()[1]
	radioTitle = data.name

	player.url = data.streams[1].url
	player.settings.volume = dp:get( 'volume', 100 )

	if player.state ~= 3 then
		player.controls:stop()
	end


	downloadCover( data )
end




function downloadCover( data )
	local request = fetch( data.logo100x100 )

	request:save( '#CURRENTPATH#icons/'.. radio[ iRadio ] ..'.png' )
	meter( 'image' ):image( radio[ iRadio ]):update()

	body:lgradient(
		('240 | %s50 ; 0.0 | %s50 ; 1.0'):format(
			data.strikingColor1:gsub( '^#', '' ),
			data.strikingColor2 == ''
				and 'ffffff'
				or data.strikingColor2:gsub( '^#', '' )
		))
	:update()
end




hotkey.keyboard({
	on    = 'press',
	focus = true,
	vk    = {
		'VK_MEDIA_STOP',
		'VK_MEDIA_PLAY_PAUSE',
		'VK_VOLUME_UP',
		'VK_VOLUME_DOWN',
		'VK_MEDIA_NEXT_TRACK',
		'VK_MEDIA_PREV_TRACK'
	},

	callback = function( event )
		if event.vk == 'VK_MEDIA_STOP' then
			player.controls:stop()


		elseif event.vk == 'VK_MEDIA_PLAY_PAUSE' then
			if player.state == 3 then
				player.controls:pause()
			else
				player.controls:play()
			end


		elseif event.vk == 'VK_VOLUME_UP' or event.vk == 'VK_VOLUME_DOWN' then
			local volume = dp:get( 'volume', 100 )
			volume =
				event.vk == 'VK_VOLUME_DOWN'
				and math.max( volume - 1, 0 )
				or math.min( volume + 1, 100 )

			player.settings.volume = volume
			dp:set( 'volume', volume )


		elseif event.vk == 'VK_MEDIA_NEXT_TRACK' or event.vk == 'VK_MEDIA_PREV_TRACK' then
			iRadio =
				event.vk == 'VK_MEDIA_PREV_TRACK'
				and math.max( iRadio - 1, 1 )
				or math.min( iRadio + 1, #radio )

				loadRadio()
				dp:set( 'radio-index', iRadio )
		end

		return false
	end
})



function rain:update( au, dt )
	loadTitle()
end


function rain:init()
	loadRadio()
end
