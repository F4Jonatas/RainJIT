--
-- Extensions to the Lua math library providing common numerical operations.
--
-- This module adds utility functions for rounding, percentage calculations,
-- number formatting (leading zeros, metric prefixes), scientific notation
-- expansion, clamping, and statistical measures (median, average).
--
-- It is designed for LuaJIT but compatible with Lua 5.1+.
--
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
-- The function filters out non-numeric values and sorts the remaining numbers.
-- For an odd count, returns the middle element; for an even count, returns
-- the average of the two central elements.
--
-- @tparam table tbl A table (array) containing numbers (may include other types, which are ignored).
-- @treturn number|nil The median value, or `nil` if the filtered list is empty.
-- @usage
--   math.median({3, 1, 4, 1, 5})        --> 3
--   math.median({10, 20, 30, 40})       --> 25
--   math.median({})                     --> nil
--   math.median({1, "foo", 2, nil, 3})  --> 2
--
math.median = function( tbl )
	local numbers = {}
	for _, v in ipairs(tbl) do
		if type( v ) == 'number' then
			table.insert( numbers, v )
		end
	end

	table.sort( numbers )
	local n = #numbers

	if n == 0 then
		return nil
	end


	if n % 2 == 1 then
		-- Odd: return the middle element
		return numbers[ math.floor( n / 2 ) + 1 ]
	else
		-- Even: return the average of the two middle elements
		local mid1 = numbers[ n / 2 ]
		local mid2 = numbers[ n / 2 + 1 ]
		return ( mid1 + mid2 ) / 2
	end
end



--- Computes the arithmetic mean (average) of a list of numbers.
--
-- All elements in the table must be numbers; non-numeric values are ignored.
-- Returns `nil` for an empty table.
--
-- @tparam table numbers An array of numbers.
-- @treturn number|nil The average, or `nil` if the table is empty.
-- @usage
--   math.average({1, 2, 3, 4, 5})  --> 3
--   math.average({10, 20})         --> 15
--   math.average({})                --> nil
--   math.average({1, "foo", 3})    --> 2   ("foo" ignored)
--
math.average = function( numbers )
	local sum = 0
	local count = 0

	for _, val in ipairs( numbers ) do
		if type(val) == 'number' then
			sum = sum + val
			count = count + 1
		end
	end

	if count > 0 then
		return sum / count
	else
		return nil
	end
end


return math