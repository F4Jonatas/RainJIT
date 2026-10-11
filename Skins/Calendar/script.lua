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

local core   = require( 'core'   )
local render = require( 'render' )
local fetch  = require( 'fetch'  )
local JSON   = require( 'json'   )
local i18n   = require( 'i18n'   )
local meter  = require( 'meter'  )
local DATE   = require( 'datetime.date' )



--- Registers an event for a calendar cell.
-- Ignored when `SOCM` is enabled and the cell belongs to another month.
-- @param (number) index - Cell index (1-42).
-- @param (table) day - Cell entry returned by `calendargrid`.
-- @param (string) kind - Event kind: `holiday`, `event` or `moon`.
-- @param (string) text - Text shown in the tooltip.
local function addevent( index, day, kind, text )
	if core.SOCM and day.month ~= 'current' then return end

	core.events[ index ]         = core.events[ index ] or {}
	core.events[ index ][ kind ] = core.events[ index ][ kind ] or {}

	table.insert( core.events[ index ][ kind ], text )
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
	for req in pairs( core.requests ) do
		req:cancel()
		core.requests[ req ] = nil
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
	core.requests[ req ] = true

	req:callback( function( self, response )
		core.requests[ req ] = nil

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



--- Marks a source as loaded and renders once all sources are done.
local function sourcedone()
	core.pending = core.pending - 1

	if core.pending == 0 then
		render.render()
	end
end



--- Registers a date-grouped table in the calendar, then marks the source as done.
-- @param (table) dict - Table returned by `fetchbydate`.
-- @param (string) kind - Event kind: `holiday` or `moon`.
-- @param (function) textfn - Receives an entry and returns its tooltip text.
local function applyevents( dict, kind, textfn )
	for index, day in ipairs( core.calendardict ) do
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
	render.clearbody()

	core.tooltipText = {}
	core.events      = {}


	-- Grid and subtitle
	local stamp       = core.viewstamp()
	core.calendardict = calendargrid( stamp )
	core.builtday     = DATE.today()

	local _, monthname = DATE.month( stamp )
	rain:bang(
		'!setOption',
		'subtitle',
		'text', i18n( monthname ) ..'  '.. DATE.year( stamp )
	)


	-- Cell texts, styles and current day
	for index, day in ipairs( core.calendardict ) do
		local mday   = meter( core.dayname( index )):text( day.day )
		local wanted = {
			PreviousMonth = day.month == 'prev',
			NextMonth     = day.month == 'next',
			WeekendStyle  = day.weekday == 1 or day.weekday == 7,
			NewWk         = day.weekday == 1,
			currentday    = day.istoday
		}

		local toadd = {}

		for _, name in ipairs( core.DAY_STYLES ) do
			if wanted[ name ] then
				table.insert( toadd, name )
			else
				mday.inherit:remove( name )
			end
		end

		if #toadd > 0 then
			mday.inherit:add( toadd )
		end


		if day.istoday then
			local x, y = core.cellpos( index )
			local c    = core.cellsize()

			render.drawdot(
				x + ( c.w / 2 ),
				y + ( c.h / 2 ),
				math.min( c.w, c.h ) * 0.7,
				rain:var( 'FILL_VIBRANT' )
			)
		end
	end


	-- Clear the old tooltips and events (after the cell updates, so nothing reapplies them)
	for index = 1, 42 do
		meter( core.dayname( index )):event( 'over leave', false )
	end


	-- Years shown on the grid
	local years, nyears = {}, 0

	for _, day in ipairs( core.calendardict ) do
		if not core.SOCM or day.month == 'current' then
			local year = tonumber( tostring( day.date ):sub( 1, 4 ))

			if year and not years[ year ] then
				years[ year ] = true
				nyears        = nyears + 1
			end
		end
	end


	-- Remote sources + calendar.json
	core.pending = nyears * #core.SOURCES + 1

	for year in pairs( years ) do
		for _, src in ipairs( core.SOURCES ) do
			fetchbydate( src.label, src.url( year ), src.listkey, src.datefn, function( dict )
				applyevents( dict, src.kind, src.textfn )
			end)
		end
	end


	local json = JSON( rain:absPath( 'calendar.json' ))

	-- Recurring events by weekday pattern, e.g. "Monday:2" = 2nd Monday of the month.
	-- The year is ignored; matching is month + weekday + occurrence index.
	for _, pattern in ipairs( json.event or {} ) do
		local wname, nth = tostring( pattern.day ):match( '^(%a+):(%d+)$' )
		local wd          = wname and core.WD_INDEX[ wname:lower() ]
		local month       = tonumber( pattern.month )

		if wd and month and nth then
			nth = tonumber( nth )

			for index, day in ipairs( core.calendardict ) do
				local dmonth = tonumber( day.date:sub( 6, 7 ))

				if dmonth == month
				and day.weekday == wd
				and math.floor(( day.day - 1 ) / 7 ) + 1 == nth then
					addevent( index, day, 'event', pattern.title )
				end
			end
		end
	end


	for index, day in ipairs( core.calendardict ) do
		local entry = json[ day.date ]
		if entry then
			local list = ( #entry > 0 ) and entry or { entry }

			for _, event in ipairs( list ) do
				addevent( index, day, 'event', event.title )
			end
		end
	end

	render.refreshgroup()
	sourcedone()
end



--- Moves the calendar by a number of months and rebuilds it.
-- Shows the next or previous month. Call it from a skin action.
-- @param (number) delta - Months to move (negative goes back).
function navigate( delta )
	core.offset = core.offset + delta
	build()
end



--- Rebuilds the calendar only if the day has changed since the last build.
-- Call it periodically from the skin. Does nothing while another month is displayed.
function checkday()
	if core.offset == 0 and DATE.today() ~= core.builtday then
		build()
	end
end



--- Called when the skin is initialized.
function rain:init()
	build()
end



local reload = 4000
function rain:update( au, dt )

	if math.fmod( au, reload ) == 0 then
		print('Calendar checkday')
		checkday()
	end
end
