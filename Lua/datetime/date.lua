--- Time utilities module.
-- Parses ISO-8601-like datetime strings into Unix timestamps and formats
-- timestamps back into date-only strings.
--
-- @module datetime.date
-- @release 0.1.3
-- @author F4Jonatas
-- @license GPL v2.0 License
-- @see https://github.com/stepelu/lua-time
-- @see https://github.com/Tieske/date

local M = {}
M.__index = M


--- Number of days in each month (index 1 = January), for non-leap years.
local EOM = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }




M.today = function()
	return os.date( '%Y-%m-%d' )
end




--- Parses an ISO-8601-like datetime string into a Unix timestamp (date only).
-- Accepts an optional time portion, optional fractional seconds and an
-- optional timezone offset (`+HH:MM`, `-HH:MM` or `Z`). Only the date
-- (year/month/day) is used to build the timestamp; time and offset are
-- validated but not applied to the returned value.
--
-- @usage
--   -- Examples of accepted input
--   time.todate("2026-09-26T20:02:19+00:00")
--   time.todate("2001-12-30T11:10:00.000000")
--   time.todate("2001-12-30T11:10:00-03:00")
--   time.todate("2001-12-30")
--
-- @param (string) content - ISO-8601-like datetime string.
-- @return (number) Unix timestamp corresponding to the date at midnight (local time).
-- @return (string) Date formatted as "YYYY-MM-DD".
-- @raise error if `content` does not match the expected format.
M.todate = function( content )
	local f, l, year, month, day, h, m, s, ms, tzsign, tzh, tzm = content:find(
		'(%d+)-(%d+)-(%d+)T?(%d*):?(%d*):?(%d*)%.?(%d*)([%+%-]?)(%d*):?(%d*)'
	)

	if year == nil or l ~= #content then
		error(
			'"' .. content .. '" is not a string representation of a date\n' ..
			'Expected string: 2001-12-30T11:10:00.000000[+/-HH:MM]'
		)
	end

	local timestamp = os.time({ year = year, month = month, day = day })
	return timestamp, M.todatestring( timestamp )
end



--- Formats a Unix timestamp as a date-only string ("YYYY-MM-DD").
-- @param (number) timestamp - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (string) Date formatted as "YYYY-MM-DD".
M.todatestring = function( timestamp )
	return os.date( '%Y-%m-%d', timestamp )
end



--- Returns the day of the week for a Unix timestamp.
-- Uses Sunday as the first day of the week.
-- @param (number) timestamp - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Day of the week (1 = Sunday, ..., 7 = Saturday).
-- @return (string) Full weekday name (e.g. "Sunday"), in the current locale.
M.weekday = function( timestamp )
	return tonumber( os.date( '%w', timestamp )) + 1, os.date( '%A', timestamp )
end



--- Returns the day of the month for a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Day of the month (1 to 31).
M.day = function( timestamp )
	timestamp = timestamp or os.time()
	return tonumber( os.date( '%d', timestamp ))
end



--- Returns the month for a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Month number (1 = January, ..., 12 = December).
-- @return (string) Full month name (e.g. "January"), in the current locale.
M.month = function( timestamp )
	timestamp = timestamp or os.time()
	return tonumber( os.date( '%m', timestamp )), os.date( '%B', timestamp )
end



--- Returns the year for a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Year (e.g. 2026).
M.year = function( timestamp )
	timestamp = timestamp or os.time()
	return tonumber( os.date( '%Y', timestamp ))
end


--- Returns the timestamp for the first day of the month.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Unix timestamp corresponding to the first day of the month at midnight (local time).
-- @return (number) Day of the week (1 = Monday, ..., 7 = Sunday).
-- @return (string) Full weekday name (e.g. "Monday"), in the current locale.
M.startofmonth = function( timestamp )
	local tstamp = os.time({
		year = os.date( '%Y', timestamp ),
		month = os.date( '%m', timestamp ),
		day = 1,
	})

	return tstamp, M.weekday( tstamp )
end



--- Subtracts a value from a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @param (number) value - Amount to subtract.
-- @param (string) type - Unit to subtract: "day", "month" or "year".
-- @return (number) Resulting Unix timestamp at midnight (local time).
-- @return (string) Date formatted as "YYYY-MM-DD".
-- @raise error if `type` is not "day", "month" or "year".
M.datesub = function( timestamp, value, type )
	local year = tonumber( os.date( '%Y', timestamp ))
	local month = tonumber( os.date( '%m', timestamp ))
	local day = tonumber( os.date( '%d', timestamp ))

	if type == 'day' then
		day = day - value
	elseif type == 'month' then
		month = month - value
	elseif type == 'year' then
		year = year - value
	else
		error( '"' .. tostring( type ) .. '" is not a valid type\nExpected: "day", "month" or "year"' )
	end

	local tstamp = os.time({ year = year, month = month, day = day })
	return tstamp, M.todatestring( tstamp )
end



--- Adds a value to a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @param (number) value - Amount to add.
-- @param (string) type - Unit to add: "day", "month" or "year".
-- @return (number) Resulting Unix timestamp at midnight (local time).
-- @return (string) Date formatted as "YYYY-MM-DD".
-- @raise error if `type` is not "day", "month" or "year".
M.dateadd = function( timestamp, value, type )
	return M.datesub( timestamp, -value, type )
end



--- Checks whether the year of a Unix timestamp is a leap year.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (boolean) `true` if the year is a leap year, `false` otherwise.
M.isleapyear = function( timestamp )
	local year = tonumber( os.date( '%Y', timestamp ))
	return year % 4 == 0 and ( year % 100 ~= 0 or year % 400 == 0 )
end



--- Returns the number of days in the month of a Unix timestamp.
-- If `timestamp` is not provided, the current date and time is used.
-- @param (number) [timestamp] - Unix timestamp, as returned by `M.todate` or `os.time`.
-- @return (number) Number of days in the month (28 to 31).
-- @return (number) Unix timestamp corresponding to the date at midnight (local time).
-- @return (string) Date formatted as "YYYY-MM-DD".
M.endofmonth = function( timestamp )
	local month = tonumber( os.date( '%m', timestamp ))
	local result = ( month == 2 and M.isleapyear( timestamp )) and 29 or EOM[ month ]

	-- get new timestamp
	local year = tonumber( os.date( '%Y', timestamp ))
	timestamp = os.time({ year = year, month = month, day = result })

	return result, timestamp, M.todatestring( timestamp )
end


return M
