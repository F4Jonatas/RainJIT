-- Streams one of several online radio stations via Windows Media Player
-- (WMPlayer.OCX) and polls a "now playing" API once per second to display
-- the current track and artist.
--
-- @see https://api.radio.de/stations/now-playing?stationIds=RADIOSTATION

local depot  = require( 'depot' )
local meter  = require( 'meter' )
local hotkey = require( 'hotkey' )
local glass  = require( 'glass' )
local fetch  = require( 'fetch' )
local lfs    = require( 'lfs.utils' )
local luacom = import( 'luacom' )

local dp     = depot()

--- Index (1-based) of the currently selected station in `radio`.
-- Persisted across sessions via the depot.
local iRadio = dp:get( 'radio-index', 1 )

--- List of available station IDs, as understood by the radio.de / radio-api.net API.
local radio  = { 'energy98', 'jbfm', 'hunterpop' }

--- Meter name constants.
-- Centralised so meter creation and `self.name` comparisons can never drift apart.
local METER_PLAY = 'controls-play_plause'
local METER_NEXT = 'controls-next'
local METER_PREV = 'controls-prev'

local body = meter( 'body' )
local song = meter( 'song' )
local play = meter( METER_PLAY, 2 )
local next = meter( METER_NEXT, 2 )
local prev = meter( METER_PREV, 2 )

-- next:path([[
-- M 10.5028 6.908
-- L 7.6516 6.908
-- L 7.6516 17.072
-- L 10.5028 17.072
-- L 10.5028 6.908
-- Z
-- M 19.1418 11.0546
-- C 19.6178 11.3714 19.8563 11.5306 19.939 11.7304
-- C 20.0112 11.9046 20.0112 12.1018 19.939 12.276
-- C 19.8563 12.4766 19.6178 12.635 19.1418 12.9518
-- L 13.7852 16.5238
-- C 13.2088 16.9074 12.921 17.0993 12.6826 17.0852
-- C 12.474 17.0729 12.2822 16.9699 12.1563 16.8036
-- C 12.012 16.6126 12.012 16.2668 12.012 15.5742
-- L 12.012 8.4322
-- C 12.012 7.7396 12.012 7.3938 12.1563 7.2028
-- C 12.2822 7.0365 12.474 6.9335 12.6826 6.9212
-- C 12.921 6.9071 13.2088 7.099 13.7852 7.4826
-- L 19.1418 11.0546
-- Z
-- ]])

-- rain:var( 'shape', next.contentPath, rain:var("#CURRENTPATH##CURRENTFILE#"))


-- Forward declarations
local radioTitle
local loadRadio
local player
local loadTitle

--- Last WMPlayer state observed by loadTitle(), used to avoid re-issuing
-- play()/pause() commands every tick when the player briefly passes
-- through transient states (buffering, transitioning, reconnecting, etc.).
local lastKnownState


luacom.config.abort_on_error = false
luacom.config.abort_on_API_error = false


if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end


--- Applies the play/pause button icon for the given state and syncs
-- the player + saved preference. Does not decide *when* to switch;
-- callers pass the desired target state.
-- @param state number WMPlayer state to apply (2 = paused, 3 = playing)
local function buttonPlayPause( state )
	if state == 3 then
		play:path( 'M 13.5 7 L 9.5 7 V 21 L 13.5 21 V 7 Z M 15.5 7 L 19.5 7 V 21 L 15.5 21 V 7 Z' ):update()
		dp:set( 'radio-state', 3 )
		player.controls:play()

	else
		play:path([[M 20.003 12.6688C 20.6708 13.1138 21.0046 13.3364 21.121 13.617C 21.2226 13.8622 21.2226 14.1378 21.121 14.383C 21.0046 14.6636 20.6708 14.8862 20.003 15.3312L 12.4876 20.3416C 11.6794 20.8804 11.2754 21.1498 10.9404 21.1298C 10.6486 21.1122 10.3788 20.968 10.2024 20.7348C 10 20.4672 10 19.9816 10 19.0104L 10 8.9896C 10 8.0185 10 7.5329 10.2024 7.2652C 10.3788 7.032 10.6486 6.8877 10.9404 6.8703C 11.2754 6.8503 11.6794 7.1196 12.4876 7.6583L 20.003 12.6688Z]])
		play:update()
		dp:set( 'radio-state', 2 )
		player.controls:pause()
	end
end


--- Toggles playback between playing (3) and paused (2) and updates the UI.
-- Shared by the on-skin play/pause button and the media-key hotkey, so
-- the toggle logic exists in exactly one place.
local function togglePlayPause()
	if player.state == 3 then
		player.state = 2
	else
		player.state = 3
	end

	buttonPlayPause( player.state )
end


--- Moves the station selection forward or backward (clamped to the list
-- bounds) and reloads the stream. Shared by the on-skin prev/next
-- buttons and the media-key hotkey.
-- @param direction number +1 to advance to the next station, -1 for the previous
local function changeStation( direction )
	if direction > 0 then
		iRadio = math.min( iRadio + 1, #radio )
	else
		iRadio = math.max( iRadio - 1, 1 )
	end

	loadRadio()
	dp:set( 'radio-index', iRadio )
end



--- Rainmeter lifecycle hook: runs on every update cycle.
-- @param au number update accumulator
-- @param dt number delta time
function rain:update( au, dt )
	loadTitle()
end


--- Click handler shared by the play/pause, previous and next meters.
-- @param self table the meter that was clicked
-- @param event table the click event
local function click( self, event )
	if self.name == METER_PLAY then
		togglePlayPause()

	elseif self.name == METER_PREV then
		changeStation( -1 )

	elseif self.name == METER_NEXT then
		changeStation( 1 )
	end
end



--- Rainmeter lifecycle hook: runs once when the skin is (re)loaded.
-- Creates the invisible WMPlayer.OCX instance, loads the current
-- station and restores the previously saved play/pause state.
function rain:init()
	player = luacom.CreateObject( 'WMPlayer.OCX' )
	player.uiMode = 'invisible'
	loadRadio()

	-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
	player.state = dp:get( 'radio-state', 3 )
	if player.state == 3 then
		player.settings.autoStart = true
		buttonPlayPause( player.state )
	end
end


play:event( 'leftdown', click )
next:event( 'leftdown', click )
prev:event( 'leftdown', click )



--- Fetches and displays the currently playing track/artist for the
-- selected station. Called once per update tick.
--
-- Skips the network request entirely while paused. UI-sync calls to
-- buttonPlayPause() only fire on an actual state *change*, not on
-- every tick, so transient WMPlayer states (buffering, transitioning,
-- reconnecting, etc.) no longer trigger repeated play()/pause() calls.
function loadTitle()
	local state = player.state

	if state ~= lastKnownState then
		lastKnownState = state

		-- Only paused/playing map to a known button icon; other states
		-- are left alone rather than being forced into "paused".
		if state == 2 or state == 3 then
			buttonPlayPause( state )
		end
	end


	if state == 2 then
		if radioTitle then
			song:text( 'Paused\n'.. radioTitle ):update()
		end
		return

	elseif state ~= 3 then
		return
	end

	-- It needs to be an asynchronous method. Otherwise, the entire Rainmeter process will freeze for a moment.
	fetch.async( 'https://api.radio.de/stations/now-playing?stationIds='.. radio[ iRadio ])
		:callback( function( self, response )
			if not response.ok then
				error( 'Failed to fetch radio.net data.\nError: '.. response.error )

			else
				local data = response:json()
				if not data then
					error( 'Failed to parse JSON data.\nError: '.. response.error )
				end

				if #data > 0 then
					local music  = data[1].title:match( '%s*-%s*(.*)$' )
					local artist = data[1].title:match( '^(.*)%s*-%s*' )
					song:text( music ..'\n'.. artist ):update()

				else
					song:text( 'Listening\n'.. radioTitle ):update()
				end
			end
		end)
		:send()
end


--- Downloads (and caches) the station's cover art and applies its
-- brand colours to the skin's background gradient.
-- @param data table station details payload from loadRadio()'s request
local function downloadCover( data )
	local picture = '#CURRENTPATH#icons/'.. radio[ iRadio ] ..'.png'
	local request = fetch( data.logo100x100 )

	if not lfs.exists( rain:var( picture )) then
		request:save( picture )
	end

	meter( 'image' ):image( radio[ iRadio ]):update()

	body:lgradient(
		('240 | %s40 ; 0.0 | %s40 ; 1.0'):format(
			data.strikingColor1:gsub( '^#', '' ),
			data.strikingColor2 == ''
				and 'ffffff'
				or data.strikingColor2:gsub( '^#', '' )
		))
	:update()
end


--- Loads the currently selected station: fetches its details, points
-- WMPlayer at its stream URL, restores the saved volume and refreshes
-- the cover art / gradient. Does not itself start or stop playback —
-- callers are responsible for that.
function loadRadio()
	if iRadio == #radio then
		next:fill( 'ffffff64'):update()
	else
		next:fill( 'ffffff'):update()
	end

	if iRadio == 1 then
		prev:fill( 'ffffff64'):update()
	else
		prev:fill( 'ffffff'):update()
	end


	local request = fetch( 'https://prod.radio-api.net/stations/details?stationIds='.. radio[ iRadio ])
	if not request.ok then
		error( 'Failed to fetch radio.net data.\nError: '.. request.error )

	else
		local data = request:json()
		if not data then
			error( 'Failed to parse JSON data.\nError: '.. request.error )
		else
			data = data[1]
		end

		radioTitle = data.name
		player.url = data.streams[1].url
		player.settings.volume = dp:get( 'volume', 100 )

		if player.state ~= 3 then
			player.controls:stop()
		end

		downloadCover( data )
	end
end


--- Registers global media-key hotkeys so playback can be controlled
-- even when the skin doesn't have focus.
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

	-- @param event table key event with a `vk` field naming the virtual key pressed
	callback = function( event )
		if event.vk == 'VK_MEDIA_STOP' then
			player.controls:stop()


		elseif event.vk == 'VK_MEDIA_PLAY_PAUSE' then
			togglePlayPause()


		elseif event.vk == 'VK_VOLUME_UP' or event.vk == 'VK_VOLUME_DOWN' then
			local volume = dp:get( 'volume', 100 )
			volume =
				event.vk == 'VK_VOLUME_DOWN'
				and math.max( volume - 1, 0 )
				or math.min( volume + 1, 100 )

			player.settings.volume = volume
			dp:set( 'volume', volume )


		elseif event.vk == 'VK_MEDIA_NEXT_TRACK' or event.vk == 'VK_MEDIA_PREV_TRACK' then
			changeStation( event.vk == 'VK_MEDIA_PREV_TRACK' and -1 or 1 )
		end

		return false
	end
})
