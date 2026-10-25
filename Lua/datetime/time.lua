
--- Time utilities.
-- Provides functions to extract and manipulate hours, minutes, and seconds
-- from Unix timestamps.
-- @module detetime.time

local M = {}

--- Returns the local time zone offset in seconds for a timestamp.
-- @param (number) timestamp - Unix timestamp.
-- @return (number) Time zone offset in seconds.
local function timezoneoffset( timestamp )
	local localdate = os.date( '*t', timestamp )
	local utcdate   = os.date( '!*t', timestamp  )

	localdate.isdst = false
	utcdate.isdst   = false

	return os.difftime( os.time( localdate ), os.time( utcdate ))
end



--- Parses an ISO 8601 datetime string into a Unix timestamp.
-- Supports timezone offsets, UTC suffixes, and fractional seconds.
-- Datetimes without a timezone are interpreted as local time.
-- @param (string) value - ISO 8601 datetime string.
-- @return (number) Unix timestamp.
local function parsedatetime( value )
	local year, month, day, hour, minute, second, zone =
		value:match( '^(%d%d%d%d)%-(%d%d)%-(%d%d)[Tt ](%d%d):(%d%d):(%d%d)(.*)$' )

	assert( year, 'Invalid ISO 8601 datetime string' )

	zone = zone:gsub( '^%.%d+', '' )

	local offset

	if zone == '' then
		return os.time({
			year  = tonumber( year   ),
			month = tonumber( month  ),
			day   = tonumber( day    ),
			hour  = tonumber( hour   ),
			min   = tonumber( minute ),
			sec   = tonumber( second )
		})

	elseif zone:upper() == 'Z' then
		offset = 0

	else
		local sign, hours, minutes = zone:match( '^([+-])(%d%d):?(%d%d)$' )

		assert( sign, 'Invalid ISO 8601 timezone offset' )

		hours   = tonumber( hours   )
		minutes = tonumber( minutes )

		assert( hours <= 23 and minutes <= 59, 'Invalid ISO 8601 timezone offset' )

		offset = hours * 60 + minutes

		if sign == '-' then
			offset = -offset
		end
	end

	local timestamp = os.time({
		year  = tonumber( year   ),
		month = tonumber( month  ),
		day   = tonumber( day    ),
		hour  = tonumber( hour   ),
		min   = tonumber( minute ),
		sec   = tonumber( second )
	})

	return timestamp + timezoneoffset( timestamp ) - offset * 60
end


--- Returns the hour component of a timestamp.
-- Uses the local time zone.
-- @param (number) timestamp - Unix timestamp. Defaults to the current time.
-- @return (number) Hour component (0-23).
function M.hours( timestamp )
	return tonumber( os.date( '%H', timestamp or os.time() ))
end



--- Returns the minute component of a timestamp.
-- Uses the local time zone.
-- @param (number) timestamp - Unix timestamp. Defaults to the current time.
-- @return (number) Minute component (0-59).
function M.minutes( timestamp )
	return tonumber( os.date( '%M', timestamp or os.time() ))
end



--- Returns the second component of a timestamp.
-- Uses the local time zone.
-- @param (number) timestamp - Unix timestamp. Defaults to the current time.
-- @return (number) Second component (0-59).
function M.seconds( timestamp )
	return tonumber( os.date( '%S', timestamp or os.time() ))
end



--- Adds a duration to a Unix timestamp.
-- The operation adds elapsed time in seconds.
-- @param (number) timestamp - Unix timestamp.
-- @param (number) value - Duration to add.
-- @param (string) unit - Duration unit: 'hours', 'minutes', or 'seconds'.
-- @return (number) Adjusted Unix timestamp.
function M.add( timestamp, value, unit )
	local units = {
		hours   = 3600,
		minutes = 60,
		seconds = 1
	}

	assert( type( timestamp ) == 'number', 'timestamp must be a number'              )
	assert( type( value )     == 'number', 'value must be a number'                  )
	assert( units[ unit ]                , 'unit must be hours, minutes, or seconds' )

	return timestamp + value * units[ unit ]
end



--- Subtracts a duration from a Unix timestamp.
-- The operation subtracts elapsed time in seconds.
-- @param timestamp Unix timestamp.
-- @param value Duration to subtract.
-- @param unit Duration unit: 'hours', 'minutes', or 'seconds'.
-- @return Adjusted Unix timestamp.
function M.sub( timestamp, value, unit )
	return M.add( timestamp, -value, unit )
end



--- Converts a Unix timestamp or ISO 8601 datetime string to a time string.
-- Uses the local time zone.
-- @param (number|string) timestamp - Unix timestamp or ISO 8601 datetime string. Defaults to the current time.
-- @return (string) Time - formatted as "HH:MM:SS".
function M.totimestring( timestamp )
	timestamp = timestamp or os.time()

	if type( timestamp ) == 'string' then
		timestamp = parsedatetime( timestamp )
	end

	assert(
		type( timestamp ) == 'number',
		'timestamp must be a number or ISO 8601 string'
	)

	return os.date( '%H:%M:%S', timestamp )
end


-- @param (string) content - ISO-8601-like datetime string.
-- @return (number) Unix timestamp.
function M.totime( content )
	return parsedatetime( content )
end


return M
