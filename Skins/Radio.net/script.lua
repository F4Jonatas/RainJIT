--- Streams online radio stations via Windows Media Player (WMPlayer.OCX).
-- Polls a "now playing" API once per second to display the current track
-- and artist. The player COM object is created on demand and released
-- (GC'd) on pause or stop, so it only exists while actually streaming.
--
-- @author F4Jonatas
-- @license GPL v2.0 License

local depot  = require( 'depot' )
local meter  = require( 'meter' )
local hotkey = require( 'hotkey' )
local glass  = require( 'glass' )
local fetch  = require( 'fetch' )
local lfs    = require( 'lfs.utils' )
local luacom = import( 'luacom' )

local dp = depot()

--- Index (1-based) of the currently selected station in `radio`.
-- Persisted across sessions via the depot.
-- @local (number) iRadio
local iRadio = dp:get( 'radio-index', 1 )

--- List of available station IDs, as understood by the radio.de / radio-api.net API.
-- @local (table) radio
local radio = { 'energy98', 'jbfm', 'hunterpop' }

-- @local (string)
local radioTitle

--- Meter name constants.
-- Centralised so meter creation and `self.name` comparisons can never drift apart.
-- @local (string) METER_PLAY
-- @local (string) METER_NEXT
-- @local (string) METER_PREV
local METER_PLAY = 'controls-play_plause'
local METER_NEXT = 'controls-next'
local METER_PREV = 'controls-prev'

--- Display text for each WMPlayer `playState` that isn't Playing (3).
-- Playing is handled separately in `loadTitle`, since it shows the live
-- now-playing title/artist instead of a fixed label.
-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
-- @local (table) PLAYSTATE_TEXT
local PLAYSTATE_TEXT = {
	[0]  = 'Undefined',
	[1]  = 'Stopped',
	[2]  = 'Paused',
	[4]  = 'Seeking',
	[5]  = 'Seeking',
	[6]  = 'Buffering',
	[7]  = 'Connecting',
	[8]  = 'Stream ended',
	[9]  = 'Loading',
	[10] = 'Ready',
	[11] = 'Reconnecting',
}

local body = meter( 'body' )
local song = meter( 'song' )
local play = meter( METER_PLAY, 2 )
local next = meter( METER_NEXT, 2 )
local prev = meter( METER_PREV, 2 )

--- Last WMPlayer `playState` observed by `loadTitle`.
-- Used to avoid re-issuing `play()`/`pause()` commands every tick when the
-- player briefly passes through transient states (buffering, transitioning,
-- reconnecting, etc.).
-- @local (number) lastKnownState
local lastKnownState

luacom.config.abort_on_error     = false
luacom.config.abort_on_API_error = false

if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' } )
end



--- Creates the WMPlayer COM object if it does not exist yet.
-- @return table The player object.
local function ensurePlayer()
	if not player then
		player = luacom.CreateObject( 'WMPlayer.OCX' )
		player.uiMode = 'invisible'
		player.settings.autoStart = true
	end

	return player
end



--- Releases the WMPlayer COM object, freeing its resources.
-- Called once playback is paused or stopped, i.e. as soon as the player
-- is no longer actually in use.
local function releasePlayer()
	if player then
		player = nil
		collectgarbage()
	end
end



--- Sets the play/pause button icon and persists the preference, without
-- touching the COM object. Used when the UI needs to reflect a state
-- (e.g. on skin load) without that alone justifying creating the player.
-- @param state number WMPlayer state to represent (2 = paused, 3 = playing)
local function setPlayIcon( state )
	if state == 3 then
		play:text( '[\\XEB33]' ):update()
	else
		play:text( '[\\XEDDA]' ):update()
	end

	dp:set( 'radio-state', state )
end



--- Updates the play/pause icon and issues the matching command to the
-- player. Unlike `setPlayIcon`, this does create the player if needed,
-- since issuing play()/pause() is itself a sign it's being used.
-- @param state number WMPlayer state to apply (2 = paused, 3 = playing)
local function buttonPlayPause( state )
	setPlayIcon( state )

	if state == 3 then
		ensurePlayer().controls:play()
	else
		ensurePlayer().controls:pause()
	end
end



--- Toggles playback between playing (3) and paused (2) and updates the UI.
-- Shared by the on-skin play/pause button and the media-key hotkey, so
-- the toggle logic exists in exactly one place. Creates the player the
-- first time playback starts, and releases it as soon as it's paused.
local function togglePlayPause()
	if player and player.state == 3 then
		player.state = 2
		buttonPlayPause( player.state )
		releasePlayer()

	else
		local isNewPlayer = not player
		ensurePlayer()

		-- A freshly created player has no stream assigned yet.
		if isNewPlayer then
			loadRadio()
		end

		player.state = 3
		buttonPlayPause( player.state )
	end
end



--- Stops playback and releases the player, if one exists.
-- Shared by the media-key stop hotkey.
local function stopAndRelease()
	if not player then return end

	player.controls:stop()
	setPlayIcon( 2 )
	releasePlayer()
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
-- @param au number Update accumulator.
-- @param dt number Delta time.
function rain:update( au, dt )
	loadTitle()
end



--- Click handler shared by the play/pause, previous and next meters.
-- @param self table The meter that was clicked.
-- @param event table The click event.
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
-- Restores the station info (title, cover, next/prev affordance) without
-- creating the player. The player itself is only created here if the
-- previous session was actually playing when the skin was last closed;
-- otherwise it stays uncreated until the person presses play.
function rain:init()
	local savedState = dp:get( 'radio-state', 3 )

	if savedState == 3 then
		ensurePlayer()
	end

	loadRadio()

	if savedState == 3 then
		-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-playstate
		player.settings.autoStart = true
		player.state = 3
		buttonPlayPause( player.state )
	else
		setPlayIcon( 2 )
	end
end



play:event( 'leftdown', click )
next:event( 'leftdown', click )
prev:event( 'leftdown', click )

--- Fetches and displays the currently playing track/artist for the
-- selected station. Called once per update tick.
--
-- Text is driven by the real WMPlayer `playState` (Playing, Paused,
-- Buffering, Reconnecting, etc.) rather than the state we last requested,
-- so the displayed title always reflects what the player is actually
-- doing. Only Playing triggers the now-playing fetch; every other state
-- shows a fixed label. When no player exists at all (nothing in use),
-- it shows "Stopped" without touching the COM object.
function loadTitle()
	if not player then
		song:text( PLAYSTATE_TEXT[ 1 ] ..'\n'.. radioTitle ):update()
		return
	end

	local playState = player.playState

	if playState ~= lastKnownState then
		lastKnownState = playState

		-- Only Paused/Playing map to a known button icon; other states
		-- are left alone rather than being forced into "paused".
		if playState == 2 or playState == 3 then
			buttonPlayPause( playState )
		end
	end

	if playState == 3 then
		-- It needs to be an asynchronous method. Otherwise, the entire Rainmeter process will freeze for a moment.
		fetch.async( 'https://api.radio.de/stations/now-playing?stationIds='.. radio[ iRadio ] )
			:send( function( self, response )
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
			end )

		return
	end

	local text = PLAYSTATE_TEXT[ playState ] or 'Unknown'
	if playState == 2 and radioTitle then
		text = text ..'\n'.. radioTitle
	end

	song:text( text ):update()
end



--- Downloads (and caches) the station's cover art and applies its
-- brand colours to the skin's background gradient.
-- @param (table) data - Station details payload from `loadRadio()`'s request.
local function downloadCover( data )
	local picture = '#CURRENTPATH#icons/'.. radio[ iRadio ] ..'.png'
	local request = fetch( data.logo100x100 )

	if not lfs.exists( rain:var( picture ) ) then
		request:save( picture )
	end

	meter( 'image' ):image( radio[ iRadio ] ):update()

	body:lgradient(( '240 | %s40 ; 0.0 | %s40 ; 1.0' ):format(
		data.strikingColor1:gsub( '^#', '' ),
		data.strikingColor2 == '' and 'ffffff' or data.strikingColor2:gsub( '^#', '' )
	)):update()
end



--- Refreshes the station's own info: name, cover art/gradient and the
-- next/prev affordance for the list bounds. If a player already exists
-- (i.e. playback is in use), it is also pointed at the new stream URL
-- and its volume restored; loadRadio never creates the player itself.
function loadRadio()
	if iRadio == #radio then
		next:fontalpha( 100 ):update()
	else
		next:fontalpha( 255 ):update()
	end

	if iRadio == 1 then
		prev:fontalpha( 100 ):update()
	else
		prev:fontalpha( 255 ):update()
	end


	local request = fetch( 'https://prod.radio-api.net/stations/details?stationIds='.. radio[ iRadio ] )
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

		if player then
			player.url = data.streams[1].url
			player.settings.volume = dp:get( 'volume', 50 )

			if player.state ~= 3 then
				player.controls:stop()
			end
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

	-- @param (table) event - Key event with a `vk` field naming the virtual key pressed.
	callback = function( event )
		if event.vk == 'VK_MEDIA_STOP' then
			stopAndRelease()

		elseif event.vk == 'VK_MEDIA_PLAY_PAUSE' then
			togglePlayPause()

		elseif event.vk == 'VK_VOLUME_UP' or event.vk == 'VK_VOLUME_DOWN' then
			local volume = dp:get( 'volume', 100 )
			volume = event.vk == 'VK_VOLUME_DOWN'
				and math.max( volume - 1, 0 )
				or math.min( volume + 1, 100 )

			if player then
				player.settings.volume = volume
			end

			dp:set( 'volume', volume )


		elseif event.vk == 'VK_MEDIA_NEXT_TRACK' or event.vk == 'VK_MEDIA_PREV_TRACK' then
				changeStation( event.vk == 'VK_MEDIA_PREV_TRACK' and -1 or 1 )
		end

		return false
	end
})
