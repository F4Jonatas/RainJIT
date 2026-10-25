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







--- Moon phase calculation.
--
-- Computes the moon phase for a given Unix timestamp, using
-- Jean Meeus's algorithm (Astronomical Algorithms, 2nd ed., Ch. 49),
-- which gives principal-phase times accurate to within a few seconds
-- of USNO published values for dates roughly between 1900 and 2100.
--
-- Moon age is the number of days since the last New Moon
-- (range [0, ~29.8)). New Moon = age 0, Full Moon = age ~14.77, etc.
--
-- A principal phase (New Moon, First Quarter, Full Moon, Last Quarter)
-- is reported for every timestamp whose local calendar day — computed
-- with the timezone offset given by `tz_offset` — is the same as the
-- local calendar day of the exact phase moment. This matches how USNO
-- publishes phases (one date per phase event). All other days receive
-- an intermediate phase name.


--- Mean synodic month (new moon to new moon), in days (IAU value).
local SYNODIC = 29.530588861

--- Mean JDE of the reference new moon (Meeus's k = 0): 2000-01-06 18:14 TT.
local J2000 = 2451550.09766

--- Julian Date of the Unix epoch (1970-01-01 00:00 UTC).
local JD_UNIX_EPOCH = 2440587.5

--- Seconds per day.
local DAY = 86400

--- Approximate Delta T (TT - UT) in seconds (~2026).
-- A few seconds of error is negligible for determining the
-- calendar day of a phase.
local DELTA_T = 69

--- Moon phase names, in cycle order (index 1 = New Moon).
local PHASES = {
	'New Moon',
	'Waxing Crescent',
	'First Quarter',
	'Waxing Gibbous',
	'Full Moon',
	'Waning Gibbous',
	'Last Quarter',
	'Waning Crescent',
}

local RAD = math.pi / 180
local function sind(x) return math.sin(x * RAD) end
local function cosd(x) return math.cos(x * RAD) end

local function unix_to_jd(ts) return ts / DAY + JD_UNIX_EPOCH end
local function jd_to_unix(jd) return (jd - JD_UNIX_EPOCH) * DAY end

-- Phase fractions of the lunation: NM, FQ, FM, LQ.
local FRACS = { 0, 0.25, 0.5, 0.75 }

-- Principal phase index in the 8-phase sequence.
local PRINCIPAL = { 1, 3, 5, 7 }

--- JDE (TT) of the k-th moon phase.
-- k must be n (New Moon), n+0.25 (First Quarter),
-- n+0.5 (Full Moon) or n+0.75 (Last Quarter), n integer.
local function phase_jde(k)
	local T  = k / 1236.85
	local T2 = T * T
	local T3 = T2 * T
	local T4 = T3 * T

	local JDE = J2000
		+ 29.530588861  * k
		+ 0.00015437    * T2
		- 0.000000150   * T3
		+ 0.00000000073 * T4

	local E = 1 - 0.002516 * T - 0.0000074 * T2

	-- M = Sun's mean anomaly (Meeus's M).
	local M_sun  = (2.5534 + 29.10535670 * k
		- 0.0000014 * T2 - 0.00000011 * T3) % 360

		-- M' = Moon's mean anomaly (Meeus's M').
	local M_moon = (201.5643 + 385.81693528 * k
		+ 0.0107582 * T2 + 0.00001238 * T3
		- 0.000000058 * T4) % 360

		-- F = Moon's argument of latitude.
	local F = (160.7108 + 390.67050284 * k
		- 0.0016118 * T2 - 0.00000227 * T3
		+ 0.000000011 * T4) % 360

		-- Omega = longitude of the ascending node.
	local Om = (124.7746 - 1.56375588 * k
		+ 0.0020672 * T2 + 0.00000215 * T3) % 360

	local frac = k - math.floor(k)
	local corr

	if frac == 0 or frac == 0.5 then
		local nm = (frac == 0)  -- New Moon vs Full Moon
		corr =
				(nm and -0.40720 or -0.40614) * sind(M_moon)
			+ (nm and  0.17241 or  0.17302) * E * sind(M_sun)
			+ (nm and  0.01608 or  0.01614) * sind(2*M_moon)
			+ (nm and  0.01039 or  0.01043) * sind(2*F)
			+ (nm and  0.00739 or  0.00734) * E * sind(M_moon - M_sun)
			+ (nm and -0.00514 or -0.00515) * E * sind(M_moon + M_sun)
			+ (nm and  0.00208 or  0.00209) * E*E * sind(2*M_sun)
			- 0.00111 * sind(M_moon - 2*F)
			- 0.00057 * sind(M_moon + 2*F)
			+ 0.00056 * E * sind(2*M_moon + M_sun)
			- 0.00042 * sind(3*M_moon)
			+ 0.00042 * E * sind(M_sun + 2*F)
			+ 0.00038 * E * sind(M_sun - 2*F)
			- 0.00024 * E * sind(2*M_moon - M_sun)
			- 0.00017 * sind(Om)
			- 0.00007 * sind(M_moon + 2*M_sun)
			+ 0.00004 * sind(2*M_moon - 2*F)
			+ 0.00004 * sind(3*M_sun)
			+ 0.00003 * sind(M_moon + M_sun - 2*F)
			+ 0.00003 * sind(2*M_moon + 2*F)
			- 0.00003 * sind(M_moon + M_sun + 2*F)
			+ 0.00003 * sind(M_moon - M_sun + 2*F)
			- 0.00002 * sind(M_moon - M_sun - 2*F)
			- 0.00002 * sind(3*M_moon + M_sun)
			+ 0.00002 * sind(4*M_moon)

	else
		corr = -0.62801 * sind(M_moon)
			+ 0.17172 * E * sind(M_sun)
			- 0.01183 * E * sind(M_moon + M_sun)
			+ 0.00862 * sind(2*M_moon)
			+ 0.00804 * sind(2*F)
			+ 0.00454 * E * sind(M_moon - M_sun)
			+ 0.00204 * E*E * sind(2*M_sun)
			- 0.00180 * sind(M_moon - 2*F)
			- 0.00070 * sind(M_moon + 2*F)
			- 0.00040 * sind(3*M_moon)
			- 0.00034 * E * sind(2*M_moon - M_sun)
			+ 0.00032 * E * sind(M_sun + 2*F)
			+ 0.00032 * E * sind(M_sun - 2*F)
			- 0.00028 * E*E * sind(M_moon + 2*M_sun)
			+ 0.00027 * E * sind(2*M_moon + M_sun)
			- 0.00017 * sind(Om)
			- 0.00005 * sind(M_moon - M_sun - 2*F)
			+ 0.00004 * sind(2*M_moon + 2*F)
			- 0.00004 * sind(M_moon + M_sun + 2*F)
			+ 0.00004 * sind(M_moon - 2*M_sun)
			+ 0.00003 * sind(M_moon + M_sun - 2*F)
			+ 0.00003 * sind(3*M_sun)
			+ 0.00002 * sind(2*M_moon - 2*F)
			+ 0.00002 * sind(M_moon - M_sun + 2*F)
			- 0.00002 * sind(3*M_moon + M_sun)

		local W = 0.00306
			- 0.00038 * E * cosd(M_sun)
			+ 0.00026 * cosd(M_moon)
			- 0.00002 * cosd(M_moon - M_sun)
			+ 0.00002 * cosd(M_moon + M_sun)
			+ 0.00002 * cosd(2*F)

		if frac == 0.25 then
			corr = corr + W
		else
			corr = corr - W
		end
	end

	-- Additional planetary corrections (Meeus, p. 352).
	local A = {
		299.77 + 0.107408   * k - 0.009173 * T2,
		251.88 + 0.016321   * k,
		251.83 + 26.651886  * k,
		349.42 + 36.412478  * k,
		84.66 + 18.206239  * k,
		141.74 + 53.303771  * k,
		207.14 +  2.453732  * k,
		154.84 +  7.306860  * k,
		34.52 + 27.261239  * k,
		207.19 +  0.121824  * k,
		291.34 +  1.844379  * k,
		161.72 + 24.198154  * k,
		239.56 + 25.513099  * k,
		331.55 +  3.592518  * k,
	}

	local Ac = {
		0.000325, 0.000165, 0.000164, 0.000126, 0.000110,
		0.000062, 0.000060, 0.000056, 0.000047, 0.000042,
		0.000040, 0.000037, 0.000035, 0.000023,
	}

	for i = 1, 14 do
		corr = corr + Ac[i] * sind(A[i])
	end

	return JDE + corr
end

--- Unix timestamp (UT) of the phase identified by `k`.
local function phase_unix(k)
	return jd_to_unix(phase_jde(k) - DELTA_T / DAY)
end


--- Retorna o offset do fuso horário local em relação ao UTC, em horas.
-- Calcula a diferença entre a hora UTC e a hora local para o timestamp fornecido.
-- Isso lida corretamente com fusos de meia hora (ex: +5.5) e horário de verão.
local function get_local_tz_offset(ts)
	-- os.date("!*t", ts) pega a hora em UTC.
	-- os.time() interpreta essa tabela como se fosse a hora local,
	-- resultando na diferença exata em segundos.
	local utc_ts = os.time(os.date("!*t", ts))
	return (ts - utc_ts) / 3600
end



--- Returns the moon phase for a Unix timestamp.
--
-- The time of each principal phase is computed with Meeus's algorithm
-- (Astronomical Algorithms, 2nd ed., Ch. 49), accurate to within a few
-- seconds of USNO data for dates roughly between 1900 and 2100.
--
-- Moon age is the number of days since the last New Moon
-- (range [0, ~29.8)).
--
-- A principal phase (New Moon, First Quarter, Full Moon, Last Quarter)
-- is reported for every timestamp whose **local calendar day** — given
-- by `tz_offset` — is the same as the local calendar day of the exact
-- phase moment. This matches how USNO publishes phases (one date per
-- phase event). Every other day receives an intermediate phase name.
--
-- With `phases = 4`, only principal phases are reported; on any other
-- day the index and name are `nil`.
--
-- If `timestamp` is not provided, the current date and time is used.
-- If `tz_offset` is not provided (nil), the local timezone of the machine
-- running the script is detected automatically. Use `0` to force UTC.
--
-- @usage
--   -- 8 phases (default), UTC:
--   local age, idx, name = moon.moonphase( ts )
--     --> 14.77, 5, "Full Moon"
--
--   -- 8 phases, local timezone (Brazil, UTC-3):
--   local age, idx, name = moon.moonphase( ts, 8, -3 )
--     --> 14.77, 5, "Full Moon"
--
--   -- 4 phases: only principal phases; nil on other days
--   moon.moonphase( ts,  4 )     --> 14.77, 3, "Full Moon"
--   moon.moonphase( ts2, 4 )     --> 17.77, nil, nil
--
-- @param (number) [timestamp] Unix timestamp, as returned by `os.time`.
-- @param (number) [phases=8] Number of phases to consider: `4` or `8`.
-- @param (number) [tz_offset=nil] Timezone offset in hours from UTC. Defaults to the machine's local timezone.
--
-- 8 phases: New Moon, Waxing Crescent, First Quarter, Waxing Gibbous,
-- Full Moon, Waning Gibbous, Last Quarter, Waning Crescent.
--
-- 4 phases: New Moon, First Quarter, Full Moon, Last Quarter.
--
-- @return (number) Moon age in days since the last New Moon.
-- @return (number|nil) Phase index (1 to `phases`). With `phases = 4`
--   it is `nil` when the day is not a principal phase.
-- @return (string|nil) Phase name in English (e.g. "Full Moon").
--   With `phases = 4` it is `nil` when the day is not a principal phase.
-- @raise error if `phases` is not 4 or 8.
M.moonphase = function( timestamp, phases, tz_offset )
	timestamp = timestamp or os.time()
	phases    = phases    or 8

	-- Se o usuário não passar o tz_offset (ou passar nil), pega o fuso da máquina local
	if tz_offset == nil then
		tz_offset = get_local_tz_offset( timestamp )
	end

	if phases ~= 4 and phases ~= 8 then
		error( '"' .. tostring( phases ) .. '" is not a valid number of phases\nExpected: 4 or 8' )
	end

	-- Approximate lunation number for this timestamp.
	local k_approx = ( unix_to_jd( timestamp ) - J2000 ) / SYNODIC
	local k_base   = math.floor( k_approx )

	-- Pre-compute the four principal phases of lunations k_base-2 .. k_base+2.
	-- phase_ts[kk][i] : Unix timestamp of phase i (1=NM, 2=FQ, 3=FM, 4=LQ)
	--                  of lunation kk.
	local phase_ts = {}
	for kk = k_base - 2, k_base + 2 do
		phase_ts[ kk ] = {}
		for index = 1, 4 do
			phase_ts[ kk ][ index ] = phase_unix( kk + FRACS[ index ])
		end
	end

	-- Last New Moon at or before `timestamp`.
	local last_nm_ts = phase_ts[ k_base ][1]
	for kk = k_base + 2, k_base - 2, -1 do
		if phase_ts[ kk ][1] <= timestamp then
			last_nm_ts = phase_ts[ kk ][1]
			break
		end
	end

	local age = ( timestamp - last_nm_ts ) / DAY

	-- Local calendar day of the input timestamp.
	local given_day = math.floor(( timestamp + tz_offset * 3600 ) / DAY)

	-- Is today the local calendar day of a principal phase?
	local principal = nil
	for kk = k_base - 2, k_base + 2 do
		for index = 1, 4 do
			local ts   = phase_ts[ kk ][ index ]
			local day  = math.floor(( ts + tz_offset * 3600 ) / DAY )
			if day == given_day then
				principal = PRINCIPAL[ index ]
				break
			end
		end
		if principal then break end
	end

	if principal then
		if phases == 4 then
			return age, math.floor( principal / 2 ) + 1, PHASES[ principal ]
		end

		return age, principal, PHASES[ principal ]
	end

	if phases == 4 then
		return age, nil, nil
	end

	-- Intermediate phase: find the interval containing `timestamp`.
	local inter = nil
	for kk = k_base - 1, k_base + 1 do
		local nm, fq, fm, lq = phase_ts[ kk ][1], phase_ts[ kk ][2],
			phase_ts[ kk ][3], phase_ts[ kk ][4]
		local next_nm = phase_ts[ kk + 1] and phase_ts[ kk + 1 ][1]

		if next_nm then
			if     timestamp >= nm and timestamp < fq      then inter = 2
			elseif timestamp >= fq and timestamp < fm      then inter = 4
			elseif timestamp >= fm and timestamp < lq      then inter = 6
			elseif timestamp >= lq and timestamp < next_nm then inter = 8
			end

			if inter then break end
		end
	end

	-- Defensive fallback (should not be reached with the k-range above).
	if not inter then
		inter = math.floor( age / ( SYNODIC / 4 )) * 2 + 2
		if inter > 8 then inter = 8 end
	end

	return age, inter, PHASES[ inter ]
end



return M
