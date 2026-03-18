
--- String meter extension.
--
-- Provides helper methods for manipulating **Rainmeter String meters**.
-- This submodule extends the base `meter` class with convenience
-- wrappers for common text-related options such as `Text`, `Prefix`,
-- and `Postfix`.
--
-- Instances of this module are created automatically by the main
-- `meter` factory when the Rainmeter meter type is `"String"`.
--
-- @submodule meter.string
-- @see meter
-- @see https://docs.rainmeter.net/manual/meters/string/

local M = {}
M.__index = M



--- Get or set the main text content of the meter.
--
-- This method wraps the Rainmeter `Text` option for **String meters**.
--
-- When called with a value, it updates the text displayed by the meter.
-- When called without arguments, it returns the current text value.
--
-- Internally this method uses the Rainmeter `!SetOption` bang.
--
-- If the text option is not defined, the getter returns `nil`
-- instead of an empty string.
--
-- @tparam meter self Meter instance.
-- @tparam[opt] string value Text to display.
--
-- @treturn meter When used as a setter, returns the meter instance
-- allowing method chaining.
--
-- @treturn[2] string|nil When used as a getter, returns the current
-- text value or `nil` if not defined.
--
-- @usage
-- local meter = require("meter")
-- local text = meter("ClockText")
--
-- -- set text
-- text:text("Hello Rainmeter")
--
-- -- read current text
-- local value = text:text()
--
-- @usage
-- -- chaining example
-- meter("ClockText")
--     :text("Loading...")
--     :update()
--
-- @see https://docs.rainmeter.net/manual/meters/string/
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
M.text = function( self, value )
	if value ~= nil then
		rain:bang( '!setoption', self.name, 'text', value )

	else
		local result = rain:option( self.name, 'text' )
		return result ~= '' and result or nil
	end

	return self
end



--- Get or set the text prefix of the meter.
--
-- This method wraps the Rainmeter `Prefix` option for **String meters**.
-- The prefix is displayed before the meter value or text content.
--
-- When called with a value, the prefix is updated using the
-- Rainmeter `!SetOption` bang. When called without arguments,
-- the current prefix is returned.
--
-- If the option is not defined, the getter returns `nil`
-- instead of an empty string.
--
-- @tparam meter self Meter instance.
-- @tparam[opt] string value Prefix text.
--
-- @treturn meter When used as a setter, returns the meter instance
-- allowing method chaining.
--
-- @treturn[2] string|nil When used as a getter, returns the current
-- prefix or `nil` if not defined.
--
-- @usage
-- local meter = require("meter")
-- local cpu = meter("CPUText")
--
-- cpu:prefix("CPU: ")
--
-- @usage
-- -- chaining example
-- meter("CPUText")
--     :prefix("CPU: ")
--     :text("14")
--     :postfix("%")
--     :update()
--
-- @see https://docs.rainmeter.net/manual/meters/string/
M.postfix = function( self, value )
	if value ~= nil then
		rain:bang( '!setoption', self.name, 'postfix', value )

	else
		local result = rain:option( self.name, 'postfix' )
		return result ~= '' and result or nil
	end

	return self
end



--- Get or set the text postfix of the meter.
--
-- This method wraps the Rainmeter `Postfix` option for **String meters**.
-- The postfix is displayed after the meter value or text content.
--
-- When called with a value, the postfix is updated using the
-- Rainmeter `!SetOption` bang. When called without arguments,
-- the current postfix is returned.
--
-- If the option is not defined, the getter returns `nil`
-- instead of an empty string.
--
-- @tparam meter self Meter instance.
-- @tparam[opt] string value Postfix text.
--
-- @treturn meter When used as a setter, returns the meter instance
-- allowing method chaining.
--
-- @treturn[2] string|nil When used as a getter, returns the current
-- postfix or `nil` if not defined.
--
-- @usage
-- local meter = require("meter")
-- local cpu = meter("CPUText")
--
-- cpu:postfix("%")
--
-- @usage
-- meter("CPUText")
--     :prefix("CPU: ")
--     :text("14")
--     :postfix("%")
--     :update()
--
-- @see https://docs.rainmeter.net/manual/meters/string/
M.prefix = function( self, value )
	if value ~= nil then
		rain:bang( '!setoption', self.name, 'prefix', value )

	else
		local result = rain:option( self.name, 'prefix' )
		return result ~= '' and result or nil
	end

	return self
end


return M
