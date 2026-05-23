--
-- Extensions to the Lua math library providing common numerical operations.
--
-- This module adds utility functions for rounding, percentage calculations,
-- number formatting (leading zeros, metric prefixes), scientific notation
-- expansion, clamping, and statistical measures (median, average).
--
-- It is designed for LuaJIT but compatible with Lua 5.1+.
--
-- @submodule math.utils
-- @author F4Jonatas
-- @version 1.3.0
--
-- @see https://github.com/likerRr/mathf-js
-- @see https://www.codecademy.com/resources/docs/lua/mathematical-library
-- @see https://www.lua.org/manual/5.1/manual.html#2.8
--



--- Largest integer representable exactly in double-precision float (2^53 - 1).
-- Provided for compatibility with Lua versions prior to 5.3.
--
math.maxinteger = math.pow( 2, 53 ) - 1



--- Rounds a number to a specified number of decimal places.
--
-- Uses the **round half away from zero** method. Values exactly at the
-- midpoint (.5) are rounded away from zero.
--
-- Due to IEEE-754 floating-point representation, some decimal values
-- may produce slightly unexpected results when rounding (e.g., 2.675).
--
-- @tparam number numb The number to be rounded.
-- @tparam[opt=0] number decimal Number of decimal places to round to.
--   If omitted, the number is rounded to the nearest integer.
-- @treturn number The rounded value.
-- @usage
--   math.round(12.345, 2) --> 12.35
--   math.round(10.4)      --> 10
--   math.round(-2.5)      --> -3
--
math.round = function( numb, decimal )
	if numb == nil then return nil end
	local multi = 10 ^ ( decimal or 0 )
	return math.floor( numb * multi + 0.5 * ( numb >= 0 and 1 or -1 )) / multi
end



--- Calculates a percentage of a given value.
--
-- Equivalent to `(value * percent) / 100`, rounded to the specified number
-- of decimal places (default 2). Useful for financial or statistical calculations.
--
-- @tparam number value The base value.
-- @tparam number percent The percentage to apply (e.g., 15 for 15%).
-- @tparam[opt=2] number decimals Number of decimal places for rounding.
-- @treturn number The resulting percentage value.
-- @usage
--   math.percentOf(200, 15)    --> 30
--   math.percentOf(200, 15, 0) --> 30
--   math.percentOf(250, 33.3)  --> 83.25
--
math.percentOf = function(value, percent, decimals)
	decimals = decimals or 2
	return math.round(value * percent / 100, decimals)
end



--- Formats an integer number with leading zeros to have at least `digits` digits.
-- If `numb` is not an integer, it is truncated toward zero.
--
-- @tparam number numb The number to format (will be converted to integer).
-- @tparam[opt=1] number digits Minimum number of digits (must be a positive integer). Defaults to 1.
-- @treturn string The formatted number with leading zeros.
-- @raise Error if `digits` is not a positive integer.
-- @usage
--   math.digit(42, 3)    --> "042"
--   math.digit(-7, 3)    --> "-07"
--   math.digit(5.8, 2)   --> "05"   (truncated toward zero)
--
math.digit = function(numb, digits)
	-- Default and validate digits
	digits = digits or 1
	if type(digits) ~= 'number' or digits < 1 or digits % 1 ~= 0 then
		error( 'math.digit: "digits" must be a positive integer (got '.. tostring( digits ) .. ')', 2 )
	end

	-- Validate numb and convert to integer (truncate toward zero)
	if type(numb) ~= 'number' then
		error( 'math.digit: "numb" must be a number (got '.. type( numb ) .. ')', 2 )
	end

	local int =
		numb >= 0
		and math.floor( numb )
		or math.ceil( numb )

	return string.format( '%0'.. digits ..'d', int )
end



--- Shortens a number using metric prefixes (k, M, G, T, P, E, Z, Y).
--
-- For numbers with absolute value >= 1000, the function returns a string
-- with the appropriate prefix and the number rounded to the specified digits.
-- Otherwise, the original number is returned unchanged.
--
-- @tparam number num The number to shorten.
-- @tparam[opt=0] number digits Number of digits after the decimal point.
-- @treturn string|number Shortened representation or original number.
-- @usage
--   math.shorten(12543, 1)   --> "12.5k"
--   math.shorten(-12567)     --> "-13k"
--   math.shorten(51000000)   --> "51M"
--   math.shorten(651)        --> 651
--   math.shorten(0.12345)    --> 0.12345
--
math.shorten = function( num, digits )
	local units = { 'k', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y' }
	local decimal

	for index, val in ipairs( units ) do
		decimal = math.pow( 1000, index )
		if num <= -decimal or num >= decimal then
			return math.round(( num / decimal ), digits ) .. units[ index ]
		end
	end


	return num
end




--- Converts a number in scientific notation to a plain decimal string without exponent.
--
-- Useful for environments that do not support scientific notation (e.g., Rainmeter).
-- Handles both positive and negative exponents correctly.
-- If the input is a string, it is converted to a number first; if conversion fails, returns nil.
-- The output is always a string, even for numbers that were not in scientific notation.
--
-- @tparam number|string value The number (or numeric string) to convert.
-- @treturn string|nil The decimal representation without exponent, or nil if conversion fails.
-- @usage
--   math.toDecimalString(1.23e4)      --> "12300"
--   math.toDecimalString(1.23e-5)     --> "0.0000123"
--   math.toDecimalString(123)         --> "123"
--   math.toDecimalString("5.67e+2")   --> "567"
--   math.toDecimalString("not a number") --> nil
--
math.toDecimalString = function(value)
	-- Ensure we have a number
	if type( value ) ~= 'number' then
		value = tonumber( value )
		if not value then
			return nil   -- Could not convert to number
		end
	end

	local s = tostring( value )
	-- If there is no exponent, return as is
	if not s:find( '[eE]' ) then
		return s
	end

	-- Parse mantissa and exponent
	local mantissa, sign, exp = s:match( '^([^eE]+)[eE]([+-]?)(%d+)$' )
	if not mantissa then
		-- Unexpected format, fallback to string representation
		return s
	end

	exp = tonumber(sign .. exp)  -- exponent with sign

	-- Split mantissa into integer and fractional parts
	local intPart, fracPart = mantissa:match( '^(%d*)%.?(%d*)$' )
	if not intPart then
		intPart, fracPart = mantissa, ''
	end

	-- Combine digits without decimal point
	local digits = intPart .. fracPart
	local originalPointPos = #intPart  -- position of decimal point (0-based after integer part)

	-- New position of decimal point after moving by exponent
	local newPointPos = originalPointPos + exp

	-- Need leading zeros
	if newPointPos <= 0 then
		return '0.'.. string.rep( '0', -newPointPos ) .. digits

	-- Need trailing zeros
	elseif newPointPos >= #digits then
		return digits .. string.rep( '0', newPointPos - #digits ) .. ( fracPart == '' and '' or '' )

	-- Insert decimal point inside the digits
	else
		return digits:sub( 1, newPointPos ) ..'.'.. digits:sub( newPointPos + 1 )
	end
end



--- Clamps a value between a minimum and maximum (inclusive).
--
-- @tparam number value The value to clamp.
-- @tparam number min The lower bound.
-- @tparam number max The upper bound.
-- @treturn number The clamped value: `min` if `value < min`, `max` if `value > max`, otherwise `value`.
-- @usage
--   math.clamp(5, 1, 10)   --> 5
--   math.clamp(-2, 1, 10)  --> 1
--   math.clamp(15, 1, 10)  --> 10
--
math.clamp = function( value, min, max )
	if value < min then
		return min
	elseif value > max then
		return max
	else
		return value
	end
end



--- Computes the median of a list of numbers.
--
-- The function accepts any number of arguments, which can be numbers or tables.
-- Tables are expected to contain only numbers (they are traversed via ipairs).
-- If any argument is not a number or a table, or if any table contains a non-number,
-- an error is raised.
-- For an odd count, returns the middle element; for an even count, returns
-- the average of the two central elements.
--
-- @param ... Numbers or tables of numbers.
-- @treturn number|nil The median value, or `nil` if no valid numbers are provided.
-- @usage
--   math.median(3, 1, 4, 1, 5)                 --> 3
--   math.median({10, 20, 30, 40})              --> 25
--   math.median({1, 2}, {3, 4})                --> 2.5
--   math.median({1, "foo", 2})                 --> error: non-number in table
--   math.median({1, 2}, 3)                     --> 2
--   math.median(42)                            --> 42
--   math.median()                              --> nil
--
math.median = function( ... )
	local numbers = {}

	for _, arg in ipairs( {...} ) do
		if type( arg ) == 'number' then
			table.insert( numbers, arg )

		elseif type( arg ) == 'table' then
			for _, v in ipairs( arg ) do
				if type( v ) ~= 'number' then
					error( 'math.median: table contains non-number value' )
				end

				table.insert( numbers, v )
			end
		else

			error( 'math.median: argument must be a number or a table, got '.. type( arg ))
		end
	end


	table.sort(numbers)

	local n = #numbers
	if n == 0 then
		return nil
	end

	if n % 2 == 1 then
		return numbers[ math.ceil( n / 2 )]

	else
		local mid1 = n / 2
		local mid2 = mid1 + 1
		return ( numbers[ mid1 ] + numbers[ mid2 ]) / 2
	end
end



--- Computes the arithmetic mean (average) of a list of numbers.
--
-- The function accepts any number of arguments, which can be numbers or tables.
-- Tables are expected to contain only numbers (they are traversed via ipairs).
-- If any argument is not a number or a table, or if any table contains a non-number,
-- an error is raised.
-- Returns `nil` if no valid numbers are provided.
--
-- @param ... Numbers or tables of numbers.
-- @treturn number|nil The average, or `nil` if no numbers are given.
-- @usage
--   math.average(1, 2, 3, 4, 5)                --> 3
--   math.average({10, 20})                     --> 15
--   math.average({1, 2}, {3, 4})               --> 2.5
--   math.average({1, 2}, 3)                    --> 2
--   math.average(42)                           --> 42
--   math.average()                             --> nil
--   math.average({1, "foo", 2})                --> error: non-number in table
--   math.average(1, "bar")                     --> error: argument must be number or table
--
math.average = function( ... )
	local sum   = 0
	local count = 0

	for _, arg in ipairs( {...} ) do
		if type( arg ) == 'number' then
			sum   = sum + arg
			count = count + 1

		elseif type( arg ) == 'table' then
			for _, v in ipairs( arg ) do
				if type(v) ~= 'number' then
					error( 'math.average: table contains non-number value' )
				end

				sum   = sum + v
				count = count + 1
			end

		else
			error( 'math.average: argument must be a number or a table, got '.. type( arg ))
		end
	end

	if count > 0 then
		return sum / count
	else
		return nil
	end
end



--- Finds the maximum value among a set of numbers.
--
-- The function accepts any number of arguments, which can be numbers or tables.
-- Tables are expected to contain only numbers (they are traversed via ipairs).
-- If any argument is not a number or a table, or if any table contains a non-number,
-- an error is raised.
-- Returns `nil` if no valid numbers are provided.
--
-- @param ... Numbers or tables of numbers.
-- @treturn number|nil The maximum value, or `nil` if no numbers are given.
-- @usage
--   math.maximo(3, 1, 4, 1, 5)                 --> 5
--   math.maximo({10, 20, 30, 40})              --> 40
--   math.maximo({1, 2}, {3, 4})                --> 4
--   math.maximo({1, 2}, 3)                     --> 3
--   math.maximo(42)                            --> 42
--   math.maximo()                              --> nil
--   math.maximo({1, "foo", 2})                 --> error: non-number in table
--   math.maximo(1, "bar")                      --> error: argument must be number or table
--
math.maximo = function(...)
	local max = nil

	for _, arg in ipairs({...}) do
		if type(arg) == 'number' then
			if max == nil or arg > max then
				max = arg
			end
		elseif type(arg) == 'table' then
			for _, v in ipairs(arg) do
				if type(v) ~= 'number' then
					error("math.maximo: table contains non-number value")
				end
				if max == nil or v > max then
					max = v
				end
			end
		else
			error("math.maximo: argument must be a number or a table, got " .. type(arg))
		end
	end

	return max
end



--- Finds the minimum value among a set of numbers.
--
-- The function accepts any number of arguments, which can be numbers or tables.
-- Tables are expected to contain only numbers (they are traversed via ipairs).
-- If any argument is not a number or a table, or if any table contains a non-number,
-- an error is raised.
-- Returns `nil` if no valid numbers are provided.
--
-- @param ... Numbers or tables of numbers.
-- @treturn number|nil The minimum value, or `nil` if no numbers are given.
-- @usage
--   math.minimo(3, 1, 4, 1, 5)                 --> 1
--   math.minimo({10, 20, 30, 40})              --> 10
--   math.minimo({1, 2}, {3, 4})                --> 1
--   math.minimo({1, 2}, 3)                     --> 1
--   math.minimo(42)                            --> 42
--   math.minimo()                              --> nil
--   math.minimo({1, "foo", 2})                 --> error: non-number in table
--   math.minimo(1, "bar")                      --> error: argument must be number or table
--
math.minimo = function(...)
	local min = nil

	for _, arg in ipairs({...}) do
		if type(arg) == 'number' then
			if min == nil or arg < min then
				min = arg
			end
		elseif type(arg) == 'table' then
			for _, v in ipairs(arg) do
				if type(v) ~= 'number' then
					error("math.minimo: table contains non-number value")
				end
				if min == nil or v < min then
					min = v
				end
			end
		else
			error("math.minimo: argument must be a number or a table, got " .. type(arg))
		end
	end

	return min
end



return math