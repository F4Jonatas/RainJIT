-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

local lfs    = require( 'lfs.utils' )
local depot  = require( 'depot' )
local meter  = require( 'meter' )
-- local hotkey = require( 'hotkey' )
local tools  = require( 'tools' )
local dict   = require( 'dict' )
local glass  = require( 'glass' )


-- Forward declarations
local redraw

local iconPath    = rain:var( '#CURRENTPATH#icons/' )
local dp          = depot()
local body        = meter( 'body' )
local useGlass    = dp:get( 'glass', true )
local skinFocused = false


-- Select a folder
local path = dp:get( 'path' )
if not path then
	selectFolder()
end




if useGlass then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



-- Skin meter events
body:event( 'leftdouble', function( self, event )
	rain:bang( path )
end )




local function format_size( bytes )
	local units = {"B", "KB", "MB", "GB", "TB", "PB"}
	if bytes <= 0 then
		return "0 B"
	end

	-- Calculate the exponent scale using log base 1024
	local i = math.floor(math.log(bytes) / math.log(1024))

	-- Keep scale bounded to the size of the units table
	i = math.min(i, #units - 1)

	-- Convert bytes into the target unit size
	local value = bytes / (1024 ^ i)

	-- Return formatted string with 2 decimal places (or 0 decimal places for raw bytes)
	if i == 0 then
		return string.format("%d B", value)
	else
		return string.format("%.2f %s", value, units[i + 1])
	end
end



redraw = function()
	local info = tools.folderInfo( path )
	-- print( info.size, format_size( info.size ))
	-- print( info.files   )
	-- print( info.folders )

	local sortDesc = dict.sortvalue( info.groups, false )
	for index, item in ipairs( sortDesc ) do
		print( index,item.key, item.value )
		if index == 5 break end
	end
end



function selectFolder()
	path = lfs.selectFolder(
		'Please select a folder for the Shortcut Skin.\n\n' ..
		'It will scan all folders and files that aren\'t protected by the system.'
	)

	if path then
		dp:set( 'path', path )
		redraw()
	end
end


redraw()
