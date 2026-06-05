-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

-- Requeste every second for get current music
-- https://api.radio.de/stations/now-playing?stationIds=RADIOSTATION


local depot   = require( 'depot' )
local meter   = require( 'meter' )
local hotkey  = require( 'hotkey' )
local glass   = require( 'glass' )
local fetch   = require( 'fetch' )
local trident = require( 'webview.trident' )


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





-- local play = meter('controls-play_plause')
-- 	:path( 'M 20.003 12.6688 C 20.6708 13.1138 21.0046 13.3364 21.121 13.617 C 21.2226 13.8622 21.2226 14.1378 21.121 14.383 C 21.0046 14.6636 20.6708 14.8862 20.003 15.3312 L 12.4876 20.3416 C 11.6794 20.8804 11.2754 21.1498 10.9404 21.1298 C 10.6486 21.1122 10.3788 20.968 10.2024 20.7348 C 10 20.4672 10 19.9816 10 19.0104 L 10 8.9896 C 10 8.0185 10 7.5329 10.2024 7.2652 C 10.3788 7.032 10.6486 6.8877 10.9404 6.8703 C 11.2754 6.8503 11.6794 7.1196 12.4876 7.6583 L 20.003 12.6688 Z' )
-- 	:strokewidth(0)
-- 	:update()

-- rain:var("path", play.contentPath, rain:var("#CURRENTPATH#skin.ini"))
-- print(play.content)


local player = {}

-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
player.state = dp:get( 'radio-state', 3 )






local browser = trident.create({
	url      = './index.html',
	width    = rain:var( 'WIDTH' ),
	height   = 60,
	left     = 0,
	top      = 50,
	sanitize = { 'allow_local' },
	hide     = true,
	silent   = false,

	callback = function( self, event )
		if event.type == 'documentcomplete' then
			loadRadio()
		end

		if event.type == 'playstate' then
			player.state = event.data.state
			dp:set( 'radio-state', player.state )

			if event.data.status ~= '' then
				song:text( event.data.status ):update()
			end
		end
	end
})



local function loadTitle()
	-- print(player.state, browser.document.MediaPlayer.playState )
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
	-- local request = fetch( 'https://www.youtube.com/watch?v=m52ynxt1mOo')
	-- print(request.text)
	local data    = request:json()[1]
	radioTitle    = data.name

	browser.document.MediaPlayer.url = data.streams[1].url
	browser.document.MediaPlayer.settings.volume = dp:get( 'volume', 100 )

	if player.state ~= 3 then
		browser.document.MediaPlayer.controls.stop()
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
			browser.document.MediaPlayer.controls.stop()


		elseif event.vk == 'VK_MEDIA_PLAY_PAUSE' then
			-- print(player.state, browser.document.MediaPlayer.playState ))
			if player.state == 3 then
				browser.document.MediaPlayer.controls.pause()
			else
				browser.document.MediaPlayer.controls.play()
			end


		elseif event.vk == 'VK_VOLUME_UP' or event.vk == 'VK_VOLUME_DOWN' then
			local volume = dp:get( 'volume', 100 )
			volume =
				event.vk == 'VK_VOLUME_DOWN'
				and math.max( volume - 1, 0 )
				or math.min( volume + 1, 100 )

			browser.document.MediaPlayer.settings.volume = volume
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

end
