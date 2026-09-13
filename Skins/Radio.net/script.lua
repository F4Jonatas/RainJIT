
-- Requeste every second for get current music
-- https://api.radio.de/stations/now-playing?stationIds=RADIOSTATION


local depot  = require( 'depot' )
local meter  = require( 'meter' )
local hotkey = require( 'hotkey' )
local glass  = require( 'glass' )
local fetch  = require( 'fetch' )
local luacom = import( 'luacom' )

local dp     = depot()
local iRadio = dp:get( 'radio-index', 1 )
local radio  = { 'energy98', 'jbfm', 'hunterpop' }
local body   = meter( 'body' )
local song   = meter( 'song' )
local play   = meter( 'controls-play_plause', 2 )
local next   = meter( 'controls-next', 2 )
local prev   = meter( 'controls-prev', 2 )

next:path('M 11.935 7.77 H 8.695 V 19.11 H 11.935 V 7.77 Z M 21.7524 12.3617 C 22.2933 12.7222 22.5637 12.9025 22.658 13.1298 C 22.7403 13.3284 22.7403 13.5516 22.658 13.7502 C 22.5637 13.9775 22.2933 14.1578 21.7524 14.5183 L 15.665 18.5767 C 15.0103 19.0131 14.6831 19.2313 14.4117 19.2151 C 14.1754 19.2009 13.9568 19.0841 13.8139 18.8952 C 13.65 18.6784 13.65 18.2851 13.65 17.4984 V 9.3816 C 13.65 8.595 13.65 8.2016 13.8139 7.9848 C 13.9568 7.7959 14.1754 7.679 14.4117 7.6649 C 14.6831 7.6487 15.0103 7.8669 15.665 8.3032 L 21.7524 12.3617 Z' )
prev:path([[
M 17.6477 7.2295
L 21.0211 7.2295
L 21.0211 19.0363
L 17.6477 19.0363
L 17.6477 7.2295
Z
M 7.4254 12.0079
C 6.8621 12.3856 6.5805 12.5712 6.4815 12.8094
C 6.3979 13.0169 6.3979 13.249 6.4815 13.4562
C 6.5805 13.6914 6.8621 13.8804 7.4254 14.2548
L 13.7636 18.4823
C 14.4445 18.9342 14.785 19.1632 15.0698 19.1446
C 15.3142 19.1323 15.5432 19.0085 15.6916 18.8135
C 15.8619 18.5876 15.8619 18.1759 15.8619 17.3589
L 15.8619 8.9067
C 15.8619 8.0867 15.8619 7.6782 15.6916 7.4523
C 15.5432 7.2542 15.3142 7.1336 15.0698 7.118
C 14.785 7.1025 14.4445 7.3285 13.7636 7.7834
L 7.4254 12.0079
Z
]])
-- print(next.contentPath)
-- Forward declarations
local radioTitle
local keyboardEvent
local loadRadio

if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect  = 'acrylic', corners = 'round' })
end



luacom.config.abort_on_error = false
luacom.config.abort_on_API_error = false

local player = luacom.CreateObject( 'WMPlayer.OCX' )
player.uiMode = 'invisible'



local function buttonPlayPause( state )
	if state == 3 then
		play:path( 'M 13.5 7 L 9.5 7 V 21 L 13.5 21 V 7 Z M 15.5 7 L 19.5 7 V 21 L 15.5 21 V 7 Z' ):update()
		dp:set( 'radio-state', 3 )
		player.controls:play()

	else
		play:path([[
			M 20.003 12.6688
			C 20.6708 13.1138 21.0046 13.3364 21.121 13.617
			C 21.2226 13.8622 21.2226 14.1378 21.121 14.383
			C 21.0046 14.6636 20.6708 14.8862 20.003 15.3312
			L 12.4876 20.3416
			C 11.6794 20.8804 11.2754 21.1498 10.9404 21.1298
			C 10.6486 21.1122 10.3788 20.968 10.2024 20.7348
			C 10 20.4672 10 19.9816 10 19.0104
			L 10 8.9896
			C 10 8.0185 10 7.5329 10.2024 7.2652
			C 10.3788 7.032 10.6486 6.8877 10.9404 6.8703
			C 11.2754 6.8503 11.6794 7.1196 12.4876 7.6583
			L 20.003 12.6688
			Z
		]])
		play:update()
		dp:set( 'radio-state', 2 )
		player.controls:pause()
	end
end





-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
player.state = dp:get( 'radio-state', 3 )
if player.state == 3 then
	player.settings.autoStart = true

	buttonPlayPause( player.state )
end




play:event( 'leftdown', function( self, event )
	if player.state == 3 then
		player.state = 2
		buttonPlayPause( player.state )

	else
		player.state = 3
		buttonPlayPause( player.state )
	end
end)




local function loadTitle()
	-- print( player.state, player.playState )

	if player.state == 2 then
		song:text( 'Paused\n'.. radioTitle ):update()

	elseif player.state ~= 3 then
		return buttonPlayPause( player.state )
	end

	-- Precisa ser o async metodo, caso contrário sempre vai ter um momento de freeze em todo processo do rainmeter
	fetch.async( 'https://api.radio.de/stations/now-playing?stationIds='.. radio[ iRadio ])
		:callback( function( self, response )
			local data = response:json()

			if #data > 0 then
				local music  = data[1].title:match( '%s*-%s*(.*)$' )
				local artist = data[1].title:match( '^(.*)%s*-%s*' )
				song:text( music ..'\n'.. artist ):update()

			else
				song:text( 'Listening\n'.. radioTitle ):update()
			end
		end)
		:send()
end




local function downloadCover( data )
	local request = fetch( data.logo100x100 )
	if request.ok then
		request:save( '#CURRENTPATH#icons/'.. radio[ iRadio ] ..'.png' )
		meter( 'image' ):image( radio[ iRadio ]):update()
	end

	body:lgradient(
		('240 | %s40 ; 0.0 | %s40 ; 1.0'):format(
			data.strikingColor1:gsub( '^#', '' ),
			data.strikingColor2 == ''
				and 'ffffff'
				or data.strikingColor2:gsub( '^#', '' )
		))
	:update()
end




function loadRadio()
	local request = fetch( 'https://prod.radio-api.net/stations/details?stationIds='.. radio[ iRadio ])

	local data = request:json()[1]
	radioTitle = data.name

	player.url = data.streams[1].url
	player.settings.volume = dp:get( 'volume', 100 )

	if player.state ~= 3 then
		player.controls:stop()
	end


	downloadCover( data )
end








--- Creating a hotkey for keyboard interaction
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
				player.state = 2
				buttonPlayPause( player.state )

			else
				player.state = 3
				buttonPlayPause( player.state )
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
