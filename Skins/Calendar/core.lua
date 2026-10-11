--- Shared state, constants and helpers for the calendar skin.
-- Both `script.lua` (model) and `render.lua` (view) require this module.
-- @license GPL v2.0 License

local depot = require( 'depot' )
local i18n  = require( 'i18n'  )
local DATE  = require( 'datetime.date' )

local core  = {}


core.dp     = depot()
core.code   = core.dp:get( 'country-code', 'BR' ):lower()

-- Show events only for the current month (ignores prev/next month cells).
core.SOCM   = core.dp:get( 'show-only-current-month', false )


core.WEEKDAYS     = { 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat' }
core.DOT_RADIUS   = 2.5
core.DOT_SPACING  = 6
core.MAX_DOTS     = 3
core.MOON_COLOR   = 'FAFAFA'
core.ACCENT_COLOR = { 'A4C9C8', '00858F', 'C76C19', 'D98250', '93483b', '66903C', '735A5C' }

-- Skin group updated after each change (day meters, subtitle and body).
core.GROUP        = 'Calendar'

-- Styles toggled on the day meters: added when the cell needs them, removed otherwise.
core.DAY_STYLES   = { 'previousmonth', 'nextmonth', 'weekendstyle', 'newwk', 'currentday' }

-- Order of the events inside a tooltip.
core.ORDER        = { 'holiday', 'event', 'moon' }

-- Kinds drawn as centered dots at the bottom of the cell (the moon has its own dot).
core.STACKED      = { holiday = true, event = true }

-- Remote sources, requested once per year shown on the grid.
core.SOURCES      = {
	{
		kind    = 'holiday',
		label   = 'OpenWorkdays',
		url     = function( year ) return 'https://openworkdays.com/api/v1/'.. core.code ..'/'.. year ..'.json' end,
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
core.offset       = 0   -- Months away from the current one (0 = current month).
core.requests     = {}  -- Requests in progress: requests[ req ] = true.
core.pending      = 0   -- Number of sources still loading.
core.builtday     = nil -- Date (`YYYY-MM-DD`) of the last build.
core.events       = {}  -- events[ cellIndex ][ kind ] = { text, ... }
core.calendardict = {}  -- Current grid, see `calendargrid`.
core.tooltipText  = {}  -- tooltipText[ meterName ] = string



-- Full weekday name → DATE.weekday index (1=Sun ... 7=Sat)
core.WD_INDEX = {
	sunday    = 1,
	monday    = 2,
	tuesday   = 3,
	wednesday = 4,
	thursday  = 5,
	friday    = 6,
	saturday  = 7
}


-- Get and set current system language
i18n.language()



--- Returns the timestamp of the first day of the displayed month.
-- @return (number|nil) Timestamp, or `nil` when the current month is displayed.
function core.viewstamp()
	if core.offset == 0 then return nil end

	return ( DATE.dateadd( DATE.startofmonth(), core.offset, 'month' ))
end



--- Returns the cell size defined in the skin variables.
-- @return (number) Cell width in pixels.
-- @return (number) Cell height in pixels.
function core.cellsize()
	return {
		w = tonumber( rain:var( 'CELL_W' )) or 0,
		h = tonumber( rain:var( 'CELL_H' )) or 0
	}
end



--- Returns the meter name of a calendar cell.
-- @param (number) index - Cell index (1-42).
-- @return (string) Meter name, e.g. `day7`.
function core.dayname( index )
	return string.format( 'day%d', index )
end



--- Returns the top-left position of a calendar cell.
-- @param (number) index - Cell index (1-42).
-- @return (number) X position.
-- @return (number) Y position.
function core.cellpos( index )
	local name = core.dayname( index )

	return tonumber( rain:var( '['.. name ..':X]' )) or 0,
	       tonumber( rain:var( '['.. name ..':Y]' )) or 0
end



return core
