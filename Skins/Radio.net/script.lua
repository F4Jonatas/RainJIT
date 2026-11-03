--- Streams online radio stations via Windows Media Player (WMPlayer.OCX).
-- Polls a "now playing" API once per second to display the current track
-- and artist. The player COM object is created on demand and released
-- (GC'd) on pause or stop, so it only exists while actually streaming.
--
-- The real `player.playState` is the only source of truth for what the
-- player is doing. The user's *intent* (play or pause) lives in
-- `desiredState` and is persisted in the depot; `syncState` reconciles the
-- two on every tick, so the skin recovers by itself when the stream dies
-- silently (suspend, hibernate, shutdown/boot, network loss...).
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

--- Name of the currently selected station.
-- @local (string) radioTitle
local radioTitle

--- Meter name constants.
-- Centralised so meter creation and `self.name` comparisons can never drift apart.
-- @local (string) METER_PLAY
-- @local (string) METER_NEXT
-- @local (string) METER_PREV
local METER_PLAY = 'controls-play_plause'
local METER_NEXT = 'controls-next'
local METER_PREV = 'controls-prev'

--- Maximum gap, in seconds, between two update ticks before the skin
-- assumes the machine was suspended/hibernated. The old stream is dead
-- after that even if WMPlayer still reports it as playing.
-- @local (number) SUSPEND_GAP
local SUSPEND_GAP = 10

--- Number of consecutive ticks the player may stay in a non-Playing state
-- (buffering, reconnecting, stopped, ended...) while playback is wanted,
-- before the stream is considered lost. Also covers the initial connection
-- right after the skin loads (e.g. at boot, before the network is up).
-- @local (number) STALL_TICKS
local STALL_TICKS = 15

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

-- Forward declarations
local player
local loadRadio
local loadTitle

--- What the person wants: 3 = playing, 2 = paused (WMPlayer `playState` values).
-- Restored from the depot; updated only by `setPlayIcon`.
-- @local (number) desiredState
local desiredState = dp:get( 'radio-state', 3 )

--- Last state drawn on the play/pause button, to skip redundant redraws.
-- @local (number) shownState
local shownState

--- Label shown in the `song` meter while no player exists.
-- @local (string) idleLabel
local idleLabel = PLAYSTATE_TEXT[ 2 ]

--- Consecutive ticks spent in a non-Playing state while `desiredState` is 3.
-- @local (number) stallTicks
local stallTicks = 0

--- `os.time()` of the last update tick, used to detect suspend/resume.
-- @local (number) lastTick
local lastTick = os.time()

luacom.config.abort_on_error     = false
luacom.config.abort_on_API_error = false

if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' } )
end



--- Creates the WMPlayer COM object if it does not exist yet.
-- @return (table) The player object.
local function ensurePlayer()
	if not player then
		player = luacom.CreateObject( 'WMPlayer.OCX' )
		player.uiMode = 'invisible'
		player.settings.autoStart = true
	end

	return player
end



--- Stops and releases the WMPlayer COM object, freeing its resources.
-- Safe to call when the player is already gone or its COM object is dead.
local function releasePlayer()
	if player then
		pcall( function() player.controls:stop() end )
		player = nil
		collectgarbage()
	end
end



--- Sets the play/pause button icon, records the wanted state and persists
-- it, without touching the COM object. Redraws only when the state changes.
-- @param (number) state - 2 = paused (play glyph), 3 = playing (pause glyph).
local function setPlayIcon( state )
	desiredState = state

	if state == shownState then return end
	shownState = state

	if state == 3 then
		play:text( '[\\XEB33]' ):update()
	else
		play:text( '[\\XEDDA]' ):update()
	end

	dp:set( 'radio-state', state )
end



--- Starts playback: creates the player, points it at the current
-- station's stream and plays. Does nothing if a player already exists.
local function startPlayback()
	if player then return end

	setPlayIcon( 3 )
	stallTicks = 0

	ensurePlayer()
	loadRadio()
	player.controls:play()
end



--- Pauses playback and releases the player, so nothing stays alive
-- while the radio isn't in use.
-- @param (string) [label] - Text shown in the `song` meter while idle (defaults to "Paused").
local function pausePlayback( label )
	idleLabel  = label or PLAYSTATE_TEXT[ 2 ]
	stallTicks = 0

	setPlayIcon( 2 )
	releasePlayer()
end



--- Toggles playback between playing and paused and updates the UI.
-- Shared by the on-skin play/pause button and the media-key hotkey, so
-- the toggle logic exists in exactly one place.
local function togglePlayPause()
	if desiredState == 3 then
		pausePlayback()
	else
		startPlayback()
	end
end



--- Moves the station selection forward or backward (clamped to the list
-- bounds) and reloads the stream. Shared by the on-skin prev/next
-- buttons and the media-key hotkey.
-- @param (number) direction - +1 to advance to the next station, -1 for the previous.
local function changeStation( direction )
	if direction > 0 then
		iRadio = math.min( iRadio + 1, #radio )
	else
		iRadio = math.max( iRadio - 1, 1 )
	end

	loadRadio()
	dp:set( 'radio-index', iRadio )
end



--- Reconciles the skin and the script variables with what the player is
-- really doing. Called at the start of every tick by `loadTitle`.
--
-- While playback is wanted (`desiredState == 3`), it falls back to paused
-- and releases the player when:
--
-- * a suspend/hibernate resume is detected (gap between ticks above `SUSPEND_GAP`);
-- * the player object is missing;
-- * the player stays out of the Playing state for `STALL_TICKS` ticks.
--
-- @return (number|nil) The current `playState`, or `nil` when no player exists.
local function syncState()
	local now     = os.time()
	local resumed = now - lastTick > SUSPEND_GAP
	lastTick = now

	if desiredState ~= 3 then
		return nil
	end

	if resumed or not player then
		pausePlayback()
		return nil
	end

	local playState = player.playState or 0

	if playState == 3 then
		stallTicks = 0
	else
		stallTicks = stallTicks + 1

		if stallTicks >= STALL_TICKS then
			pausePlayback( 'Stream lost' )
			return nil
		end
	end

	return playState
end



--- Rainmeter lifecycle hook: runs on every update cycle.
-- @param (number) au - Update accumulator.
-- @param (number) dt - Delta time.
function rain:update( au, dt )
	loadTitle()
end



--- Click handler shared by the play/pause, previous and next meters.
-- @param (table) self - The meter that was clicked.
-- @param (table) event - The click event.
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
-- creating the player. The player is only created here if the previous
-- session was playing (including after a shutdown/boot); `syncState` then
-- validates the real state on the following ticks. Otherwise it stays
-- uncreated until the person presses play.
function rain:init()
	if desiredState == 3 then
		startPlayback()
	else
		setPlayIcon( 2 )
		loadRadio()
	end

	lastTick = os.time()
end



play:event( 'leftdown', click )
next:event( 'leftdown', click )
prev:event( 'leftdown', click )

--- Syncs the state and displays the matching text for the `song` meter.
-- Called once per update tick.
--
-- Text is driven by the real WMPlayer `playState` (Playing, Buffering,
-- Reconnecting, etc.). Only Playing triggers the now-playing fetch;
-- every other state shows a fixed label. When no player exists (nothing
-- in use), the idle label and station name are shown without touching
-- the COM object.
function loadTitle()
	local playState = syncState()

	if not playState then
		song:text( idleLabel ..'\n'.. ( radioTitle or '' ) ):update()
		return
	end

	if playState ~= 3 then
		song:text( PLAYSTATE_TEXT[ playState ] or 'Unknown' ):update()
		return
	end

	-- It needs to be an asynchronous method. Otherwise, the entire Rainmeter process will freeze for a moment.
	fetch.async( 'https://api.radio.de/stations/now-playing?stationIds='.. radio[ iRadio ] )
		:send( function( self, response )
			-- The player may have been released while the request was in flight.
			if desiredState ~= 3 or not player then return end

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
					song:text( 'Listening\n'.. ( radioTitle or '' ) ):update()
				end
			end
		end )
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
-- and its volume restored; `loadRadio` never creates the player itself.
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
		end

		downloadCover( data )

		-- These requests are synchronous and can block for a while; that
		-- delay must not be mistaken for a suspend by `syncState`.
		lastTick = os.time()
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
			pausePlayback( PLAYSTATE_TEXT[ 1 ] )

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
