--- Image meter extension.
--
-- Provides helper methods for manipulating **Rainmeter Image meters**.
-- This submodule extends the base `meter` class with convenience
-- wrappers for common image-related options such as `ImageName`
-- and `ImagePath`.
--
-- Instances of this module are created automatically by the main
-- `meter` factory when the Rainmeter meter type is `"Image"`.
--
-- @submodule meter.image
-- @see meter
-- @see https://docs.rainmeter.net/manual/meters/image/

local M = {}
M.__index = M



--- Get or set the image file used by the meter.
--
-- This method is a convenience wrapper around the Rainmeter
-- `ImageName` option for **Image meters**.
--
-- When called with a value, it sets the image file associated
-- with the meter. When called without arguments, it returns
-- the currently configured image.
--
-- Internally this method uses the Rainmeter `!SetOption` bang
-- to update the meter configuration.
--
-- If the image option is not defined, the getter returns `nil`
-- instead of an empty string.
--
-- @tparam meter self Meter instance.
-- @tparam[opt] string value Image filename or path.
--
-- @treturn meter When used as a setter, returns the meter instance
-- to allow method chaining.
--
-- @treturn[2] string|nil When used as a getter, returns the image
-- filename or `nil` if not defined.
--
-- @usage
-- local meter = require("meter")
-- local img = meter("AlbumArt")
--
-- -- set image
-- img:image("cover.png")
--
-- -- get current image
-- local file = img:image()
--
-- @usage
-- -- chaining with other meter operations
-- meter("AlbumArt")
--     :image("cover.png")
--     :update()
--
-- @see https://docs.rainmeter.net/manual/meters/image/
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
M.image = function( self, value )
	if value ~= nil then
		rain:bang( '!setOption', self.name, 'imageName', value )

	else
		local result = rain:option( self.name, 'imageName' )
		return result ~= '' and result or nil
	end

	return self
end



--- Get or set the base directory used to resolve image files.
--
-- This method wraps the Rainmeter `ImagePath` option for
-- **Image meters**.
--
-- The path defined here acts as the base directory used
-- by Rainmeter when resolving the `ImageName` value.
--
-- When called with a value, the path is updated using the
-- Rainmeter `!SetOption` bang. When called without arguments,
-- the current path is returned.
--
-- If the option is not defined, the getter returns `nil`
-- instead of an empty string.
--
-- @tparam meter self Meter instance.
-- @tparam[opt] string value Directory path used for image lookup.
--
-- @treturn meter When used as a setter, returns the meter instance
-- allowing method chaining.
--
-- @treturn[2] string|nil When used as a getter, returns the current
-- path or `nil` if not defined.
--
-- @usage
-- local meter = require("meter")
-- local img = meter("AlbumArt")
--
-- -- define image directory
-- img:path("#@#images/")
--
-- -- read current path
-- local directory = img:path()
--
-- @usage
-- -- typical usage with ImageName
-- img:path("#@#images/")
-- img:image("cover.png")
-- img:update()
--
-- @see https://docs.rainmeter.net/manual/meters/image/
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
M.path = function( self, value )
	if value ~= nil then
		rain:bang( '!setOption', self.name, 'imagePath', value )

	else
		local result = rain:option( self.name, 'imagePath' )
		return result ~= '' and result or nil
	end

	return self
end

return M