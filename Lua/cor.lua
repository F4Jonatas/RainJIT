--[[
	Author: F4Jonatas
	Version: 1.1.0
	https://forum.rainmeter.net/viewtopic.php?t=26402
--]]

--https://github.com/luapower/color/blob/master/color.lua
--https://github.com/EmmanuelOga/columns/blob/master/utils/color.lua
--[[
		Converts an HSL color value to RGB. Conversion formula
		adapted from http://en.wikipedia.org/wiki/HSL_color_space.
		Assumes h, s, and l are contained in the set [0, 1] and
		returns r, g, and b in the set [0, 255].

		@param   {number}  h       The hue
		@param   {number}  s       The saturation
		@param   {number}  l       The lightness
		@return  {Array}           The RGB representation
 ]]--
local function HSL2RGB( hue, saturation, lightness, alpha )
	if hue < 0 or hue > 360 then
		return 0, 0, 0, alpha
	end


	if saturation < 100 then
		saturation = tonumber( '0.' .. saturation ) or saturation
	elseif saturation == 100 then
		saturation = 1
	end

	if saturation < 0 or saturation > 1 then return "saturation error" end


	if lightness < 100 then
		lightness = tonumber( "0." .. lightness ) or lightness

	elseif lightness == 100 then
		lightness = 1
	end

	if lightness < 0 or lightness > 1 then return "lightness error" end


	local chroma = ( 1 - math.abs( 2 * lightness - 1 )) * saturation
	local h = hue / 60
	local x = ( 1 - math.abs( h % 2 - 1 )) * chroma
	local r, g, b = 0, 0, 0

	if h < 1 then r, g, b = chroma, x, 0
	elseif h < 2 then r, g, b = x, chroma, 0
	elseif h < 3 then r, g, b = 0, chroma, x
	elseif h < 4 then r, g, b = 0, x, chroma
	elseif h < 5 then r, g, b = x, 0, chroma
	else r, g, b = chroma, 0, x end

	local m = lightness - chroma / 2

	return
		r + m == 1 and 255 or tonumber( string.format( "%.0f", 255 * r + m / 100 )),
		g + m == 1 and 255 or tonumber( string.format( "%.0f", 255 * g + m / 100 )),
		b + m == 1 and 255 or tonumber( string.format( "%.0f", 255 * b + m / 100 )),
		alpha
end

-- exports
return {
	hsl2rgb = HSL2RGB
}
