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
--   :image("cover.png")
--   :update()
--
-- @see https://docs.rainmeter.net/manual/meters/image/
-- @see https://docs.rainmeter.net/manual/bangs/#SetOption
--
M.image = function( self, value )
	if value ~= nil then
		rain:bang( '!setOption', self.name, 'imageName', value )

	else
		local result = rain:option( self.name, 'imageName' )
		return result ~= '' and result or nil
	end

	return self
end



-- @see https://docs.rainmeter.net/manual/meters/general-options/image-options/#ImageAlpha
--
function M:opacity( value )
	if value == nil then
		local result = rain:option( self.name, 'ImageAlpha' )
		return result ~= '' and result or nil
	end

	-- assert( type( value ) == 'number', 'value must be number' )
	rain:bang( '!setOption', self.name, 'ImageAlpha', value )

	return self
end



-- TESTING
function M:scale( value )
	if value == nil then
		-- Getter: return the current scale factor stored in the meter's option
		local result = rain:option( self.name, 'scale' )
		return result ~= '' and result or nil
	end

	-- assert(type(value) == 'number', 'value must be a number')

	-- If the original dimensions have not yet been stored
	if not self.originalRect.x then
		-- Get the current (unscaled) dimensions from the skin file or from initial settings
		-- Option 1: Read from the meter's initial options (if they are static)
		-- Option 2: Store the first time this function is called (assuming no scaling before)
		self.originalRect = {
			x = rain:var( '[&'.. self.name ..':X]' ),
			y = rain:var( '[&'.. self.name ..':Y]' ),
			w = rain:var( '[&'.. self.name ..':W]' ),
			h = rain:var( '[&'.. self.name ..':H]' )
		}

		local container = self:option( 'container' )
		if container then
			self.originalRect.y = self.originalRect.y - rain:var( '[&'.. container ..':Y]' )
			self.originalRect.x = self.originalRect.x - rain:var( '[&'.. container ..':X]' )
		end

		-- If any are nil, fall back to a default (e.g., 0) or warn
	end

	local newW = self.originalRect.w * value
	local newH = self.originalRect.h * value

	-- Calculate new X,Y to keep the center fixed
	local centerX = self.originalRect.x + self.originalRect.w / 2
	local centerY = self.originalRect.y + self.originalRect.h / 2
	local newX = centerX - newW / 2
	local newY = centerY - newH / 2


	-- Apply the changes
	rain:bang(
		'[!setOption '.. self.name ..' scale '.. value ..']'..
		'[!setOption '.. self.name ..' x '.. newX ..']'..
		'[!setOption '.. self.name ..' y '.. newY ..']'..
		'[!setOption '.. self.name ..' w '.. newW ..']'..
		'[!setOption '.. self.name ..' h '.. newH ..']'
	)

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