
--- Calendar skin.
-- Builds a 6-week calendar grid and decorates it with holidays (OpenWorkdays),
-- moon phases (U.S. Naval Observatory) and custom events (`calendar.json`).
--
-- The calendar is rebuilt in place (no skin refresh) by `build`, which is
-- triggered by:
--
-- * `rain:init`: first load.
-- * `nextmonth` / `prevmonth`: month navigation.
-- * `checkday`: call it periodically; it only rebuilds when the day has changed.
--
-- Events from all sources are collected first and rendered once every source
-- has finished, so the dots can be centered on each cell. Every rebuild
-- cancels the requests still in progress from the previous build.
--
-- The day meters, the `subtitle` meter and the `body` meter must belong to the
-- skin group `GROUP`.
-- @license GPL v2.0 License

local depot   = require( 'depot' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )
local meter   = require( 'meter' )
local JSON    = require( 'json'  )
local i18n    = require( 'i18n'  )
local tooltip = require( 'tooltip' )
local utf8    = require( 'string.utf8' )
local DATE    = require( 'datetime.date' )


local dp           = depot()
local code         = dp:get( 'country-code', 'BR' ):lower()

-- Show events only for the current month (ignores prev/next month cells).
local SOCM         = dp:get( 'show-only-current-month', false )

local body         = meter( 'body' )

local WEEKDAYS     = { 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat' }
local DOT_RADIUS   = 2
local DOT_SPACING  = 6
local MAX_DOTS     = 3
local MOON_COLOR   = 'FAFAFA'
local ACCENT_COLOR = { 'A4C9C8', '00858F', 'C76C19', 'D98250', '93483b', '66903C', '735A5C' }

-- Skin group updated after each change (day meters, subtitle and body).
local GROUP        = 'Calendar'

-- Styles toggled on the day meters: added when the cell needs them, removed otherwise.
local DAY_STYLES   = { 'PreviousMonth', 'NextMonth', 'WeekendStyle', 'NewWk', 'currentday' }

-- Order of the events inside a tooltip.
local ORDER        = { 'holiday', 'event', 'moon' }

-- Kinds drawn as centered dots at the bottom of the cell (the moon has its own dot).
local STACKED      = { holiday = true, event = true }

-- Remote sources, requested once per year shown on the grid.
local SOURCES      = {
	{
		kind    = 'holiday',
		label   = 'OpenWorkdays',
		url     = function( year ) return 'https://openworkdays.com/api/v1/'.. code ..'/'.. year ..'.json' end,
		listkey = 'holidays',
		datefn  = function( e ) return e.date end,
		textfn  = function( e ) return e.name end
	},
	{
		kind    = 'moon',
		label   = 'U.S. Naval Observatory',
		url     = function( year ) return 'https://aa.usno.navy.mil/api/moon/phases/year?year='.. year end,
		listkey = 'phasedata',
		datefn  = function( e ) return string.format( '%d-%02d-%02d', e.year, e.month, e.day ) end,
		textfn  = function( e ) return i18n( e.phase ) end
	}
}


-- State
local offset       = 0   -- Months away from the current one (0 = current month).
local requests     = {}  -- Requests in progress: requests[ req ] = true.
local pending      = 0   -- Number of sources still loading.
local builtday     = nil -- Date (`YYYY-MM-DD`) of the last build.
local events       = {}  -- events[ cellIndex ][ kind ] = { text, ... }
local calendardict = {}  -- Current grid, see `calendargrid`.


-- Get and set current system language
i18n.language()


-- Apply the glass effect
if dp:get( 'glass', true ) then
	glass( rain.hwnd, {
		effect  = 'mica',
		dark    = true,
		corners = 'round'
	})
end


for i, key in ipairs( WEEKDAYS ) do
	rain:bang(
		'!SetOption',
		'weekday'.. i,
		'text',
		i18n( key )
	)
end



--- Returns the timestamp of the first day of the displayed month.
-- @return (number|nil) Timestamp, or `nil` when the current month is displayed.
local function viewstamp()
	if offset == 0 then return nil end

	return ( DATE.dateadd( DATE.startofmonth(), offset, 'month' ))
end



--- Returns the cell size defined in the skin variables.
-- @return (number) Cell size in pixels.
local function cellsize()
	return {
		w = tonumber( rain:var( 'CELL_W' )) or 0,
		h = tonumber( rain:var( 'CELL_H' )) or 0
	}
end



--- Returns the meter name of a calendar cell.
-- @param (number) index - Cell index (1-42).
-- @return (string) Meter name, e.g. `day7`.
local function dayname( index )
	return string.format( 'day%d', index )
end



--- Returns the top-left position of a calendar cell.
-- @param (number) index - Cell index (1-42).
-- @return (number) X position.
-- @return (number) Y position.
local function cellpos( index )
	local name = dayname( index )

	return tonumber( rain:var( '['.. name ..':X]' )) or 0,
	       tonumber( rain:var( '['.. name ..':Y]' )) or 0
end



--- Returns the accent color of an event.
-- The color is picked from `ACCENT_COLOR` using a hash of the text, so it looks
-- random between events but stays the same across rebuilds.
-- @param (string) text - Event text.
-- @return (string) Hex color, e.g. `00858F`.
local function eventcolor( text )
	local hash = 0

	for i = 1, #text do
		hash = ( hash * 31 + text:byte( i )) % 2147483647
	end

	return ACCENT_COLOR[ hash % #ACCENT_COLOR + 1 ]
end



--- Draws a filled circle on the `body` meter.
-- @param (number) x - Center X.
-- @param (number) y - Center Y.
-- @param (number) radius - Circle radius.
-- @param (string) color - Fill color.
local function drawdot( x, y, radius, color )
	body:add():ellipse( x, y, radius ):fill( color )
end



--- Clears every shape of the `body` meter, except shape 1 (the background).
local function clearbody()
	for i = 2, body:length() do
		body:option( 'shape'.. i, '' )
	end
end



--- Updates the skin group and redraws the skin.
local function refreshgroup()
	rain:bang( '!updateMeterGroup', GROUP )
	rain:bang( '!redraw' )
end



--- Registers an event for a calendar cell.
-- Ignored when `SOCM` is enabled and the cell belongs to another month.
-- @param (number) index - Cell index (1-42).
-- @param (table) day - Cell entry returned by `calendargrid`.
-- @param (string) kind - Event kind: `holiday`, `event` or `moon`.
-- @param (string) text - Text shown in the tooltip.
local function addevent( index, day, kind, text )
	if SOCM and day.month ~= 'current' then return end

	events[ index ]         = events[ index ] or {}
	events[ index ][ kind ] = events[ index ][ kind ] or {}

	table.insert( events[ index ][ kind ], text )
end



--- Writes tooltips and draws the dots of every cell with events.
-- All events appear in the tooltip. At most `MAX_DOTS` dots are drawn, centered
-- at the bottom of the cell; the moon dot goes in the top-right corner.
local function render()
	local c = cellsize()

	for index, list in pairs( events ) do
		local m       = meter( dayname( index ))
		local x, y    = cellpos( index )
		local lines   = {}
		local stacked = {}

		for _, kind in ipairs( ORDER ) do
			for _, text in ipairs( list[ kind ] or {} ) do
				table.insert( lines, text )

				if STACKED[ kind ] then
					table.insert( stacked, text )
				end
			end
		end

		m:tooltip( '• '.. table.concat( lines, '\n• ' ))

		-- Centered dots
		local total = math.min( #stacked, MAX_DOTS )
		for i = 1, total do
			local dx = x + ( c.w / 2 ) + ( i - ( total + 1 ) / 2 ) * DOT_SPACING
			drawdot( dx, y + c.h, DOT_RADIUS, eventcolor( stacked[ i ] ))
		end

		-- Moon dot
		if list.moon then
			drawdot( x + c.w - 2, y + 2, DOT_RADIUS, MOON_COLOR )
		end
	end

	refreshgroup()
end



--- Marks a source as loaded and renders once all sources are done.
local function sourcedone()
	pending = pending - 1

	if pending == 0 then
		render()
	end
end



--- Builds a 43-entry calendar grid (6 weeks x 7 days, Sunday-first) for the month of `timestamp`.
-- Leading and trailing cells outside the month are filled with the previous/next month's days.
-- If `timestamp` is not provided, the current date and time is used.
--
-- The table starts at index 0. Entry 0 is not displayed, because the grid is
-- iterated with `ipairs` (cells 1-42).
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (table) Array of entries `{ timestamp, date, day, month = "prev"|"current"|"next", weekday, istoday }`, in display order.
local calendargrid = function( timestamp )
	local start, weekday = DATE.startofmonth( timestamp )
	local lead           = weekday % 7
	local now            = os.date( '*t' )
	local month          = DATE.month( start )
	local year           = DATE.year( start )
	local dayscurrent    = tonumber( os.date( '%d', os.time({ year = year, month = month + 1, day = 0 }) ))
	local daysprev       = tonumber( os.date( '%d', os.time({ year = year, month = month, day = 0 }) ))
	local grid           = {}
	local index          = 0

	--- Appends a cell to the grid.
	local function push( timestp, date, day, kind, istoday )
		grid[ index ] = {
			timestamp = timestp,
			date      = date,
			day       = day,
			month     = kind,
			istoday   = istoday,
			weekday   = DATE.weekday( timestp )
		}

		index = index + 1
	end


	-- Previous month
	for i = lead, 1, -1 do
		local timestp, date = DATE.datesub( start, i, 'day' )
		push( timestp, date, daysprev - i + 1, 'prev' )
	end

	-- Current month
	for d = 1, dayscurrent do
		local timestp, date = DATE.dateadd( start, d - 1, 'day' )
		push( timestp, date, d, 'current', year == now.year and month == now.month and d == now.day )
	end

	-- Next month
	local nextday = 1
	local _, newtimestamp = DATE.endofmonth( start )

	while index <= 42 do
		local timestp, date = DATE.dateadd( newtimestamp, nextday, 'day' )
		push( timestp, date, nextday, 'next' )
		nextday = nextday + 1
	end


	return grid
end



--- Cancels every request still in progress.
local function cancelrequests()
	for req in pairs( requests ) do
		req:cancel()
		requests[ req ] = nil
	end
end



--- Fetches a JSON list from an API and groups its entries by date.
-- On failure the error is logged and the callback receives an empty table.
-- @param (string) label - API name, used in error messages.
-- @param (string) url - Request URL.
-- @param (string) listkey - Key of the list inside the JSON response.
-- @param (function) datefn - Receives an entry and returns its date (`YYYY-MM-DD`).
-- @param (function) callback - Receives a table `{ [date] = { entry, ... } }`.
local function fetchbydate( label, url, listkey, datefn, callback )
	local req = fetch.async( url )
	requests[ req ] = true

	req:callback( function( self, response )
		requests[ req ] = nil

		local dict = {}

		if response.ok then
			for _, entry in pairs( response:json()[ listkey ] or {} ) do
				local date = datefn( entry )
				dict[ date ] = dict[ date ] or {}
				table.insert( dict[ date ], entry )
			end
		else
			print( string.format( '%s API: %s. %s', label, tostring( response.status ), tostring( response.error )))
		end

		callback( dict )
	end)

	req:send()
end



--- Registers a date-grouped table in the calendar, then marks the source as done.
-- @param (table) dict - Table returned by `fetchbydate`.
-- @param (string) kind - Event kind: `holiday` or `moon`.
-- @param (function) textfn - Receives an entry and returns its tooltip text.
local function applyevents( dict, kind, textfn )
	for index, day in ipairs( calendardict ) do
		for _, entry in ipairs( dict[ day.date ] or {} ) do
			addevent( index, day, kind, textfn( entry ))
		end
	end

	sourcedone()
end



--- Rebuilds the whole calendar in place.
-- Resets the previous state (shapes, tooltips, events), updates the subtitle and
-- the day cells, requests the remote sources and loads `calendar.json`.
-- Each date in `calendar.json` may hold a single event (`{ title = "..." }`) or
-- a list of events; every event gets its own tooltip line and, up to `MAX_DOTS`,
-- its own dot.
local function build()
	-- Reset previous state
	cancelrequests()
	clearbody()

	events = {}


	-- Grid and subtitle
	local stamp  = viewstamp()
	calendardict = calendargrid( stamp )
	builtday     = DATE.today()

	local _, monthname = DATE.month( stamp )
	rain:bang( '!setOption', 'subtitle', 'text', i18n( monthname ) ..'  '.. DATE.year( stamp ) )


	-- Cell texts, styles and current day
	for index, day in ipairs( calendardict ) do
		local mday   = meter( dayname( index )):text( day.day )
		local wanted = {
			PreviousMonth = day.month == 'prev',
			NextMonth     = day.month == 'next',
			WeekendStyle  = day.weekday == 1 or day.weekday == 7,
			NewWk         = day.weekday == 1,
			currentday    = day.istoday
		}

		local toadd = {}

		for _, name in ipairs( DAY_STYLES ) do
			if wanted[ name ] then
				table.insert( toadd, name )
			else
				mday.style:remove( name )
			end
		end

		if #toadd > 0 then
			mday.style:add( toadd )
		end


		if day.istoday then
			local x, y = cellpos( index )
			local c    = cellsize()

			drawdot(
				x + ( c.w / 2 ),
				y + ( c.h / 2 ),
				math.min( c.w, c.h ) / 2,
				rain:var( 'FILL_VIBRANT' )
			)
		end
	end


	-- Clear the old tooltips (after the cell updates, so nothing reapplies them)
	for index = 1, 42 do
		rain:bang( '!setOption', dayname( index ), 'tooltipText', '' )
	end


	-- Years shown on the grid
	local years, nyears = {}, 0

	for _, day in ipairs( calendardict ) do
		if not SOCM or day.month == 'current' then
			local year = tonumber( tostring( day.date ):sub( 1, 4 ))

			if year and not years[ year ] then
				years[ year ] = true
				nyears        = nyears + 1
			end
		end
	end


	-- Remote sources + calendar.json
	pending = nyears * #SOURCES + 1

	for year in pairs( years ) do
		for _, src in ipairs( SOURCES ) do
			fetchbydate( src.label, src.url( year ), src.listkey, src.datefn, function( dict )
				applyevents( dict, src.kind, src.textfn )
			end)
		end
	end

	local json = JSON( rain:absPath( 'calendar.json' ))

	for index, day in ipairs( calendardict ) do
		local entry = json[ day.date ]
		if entry then
			local list = ( #entry > 0 ) and entry or { entry }

			for _, event in ipairs( list ) do
				addevent( index, day, 'event', event.title )
			end
		end
	end

	refreshgroup()
	sourcedone()
end



--- Moves the calendar by a number of months and rebuilds it.
-- @param (number) delta - Months to move (negative goes back).
local function navigate( delta )
	offset = offset + delta
	build()
end



--- Shows the next month. Call it from a skin action.
function nextmonth()
	navigate( 1 )
end



--- Shows the previous month. Call it from a skin action.
function prevmonth()
	navigate( -1 )
end



--- Rebuilds the calendar only if the day has changed since the last build.
-- Call it periodically from the skin. Does nothing while another month is displayed.
function checkday()
	if offset == 0 and DATE.today() ~= builtday then
		build()
	end
end



--- Called when the skin is initialized.
function rain:init()
	-- local tt = tooltip( 'text' )
	build()
end



local reload = 40000
function rain:update( au, dt )

	if math.fmod( au, reload ) == 0 then
		print('Calendar checkday')
		checkday()
	end
end
