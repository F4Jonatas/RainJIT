-- cSpell:ignoreRegExp leftdouble|wheeldown|wheelup|lgradient|CURRENTPATH|listdir|getn|strokecolor

local lfs    = require( 'lfs.utils' )
local depot  = require( 'depot' )
local meter  = require( 'meter' )
local hotkey = require( 'hotkey' )
local tools  = require( 'tools' )
local glass  = require( 'glass' )


-- Forward declarations
local redraw
local keyboardEvent

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



local data = tools.listdir( path )
local currentItem = math.min( dp:get( 'currentItem', 1 ), #data )


hotkey.keyboard({
	vk = { 'VK_UP', 'VK_DOWN', 'VK_LEFT', 'VK_RIGHT' },
	on = 'press',
	focus = true,
	callback = function( event ) keyboardEvent( event ) end
})



function rain:update( cs, dt )
	if skinFocused and not rain:isFocused() then
		skinFocused = false
		body:event( 'leave' )
	end
end



-- Skin meter events
body:event( 'over leave leftdown leftdouble wheeldown wheelup', function( self, event )
	if event.type == 'over' then
		body:lgradient( 90, '0% 179, 204, 255, 100', '100% 199, 221, 255, 80' )
		if not useGlass then
			body:strokecolor( 227, 242, 252, 127.5 )
		end
		body:update()


	elseif event.type == 'leave' then
		if skinFocused then return end

		body:fill( 0, 0, 0, 130 )
		if not useGlass then
			body:strokecolor( 0, 0, 0, 1 )
		end
		body:update()


	elseif event.type == 'leftdown' then
		skinFocused = true

		body:lgradient( 90, '0% 172, 209, 242, 180', '100% 198, 229, 247, 180' )
		if not useGlass then body:strokecolor( 229, 236, 253, 204 ) end
		body:update()


	elseif event.type == 'leftdouble' then
		rain:bang( data[ currentItem ].filePath )

	elseif event.type == 'wheeldown' or event.type == 'wheelup' then
		keyboardEvent( event )
	end
end )




keyboardEvent = function( event )
	if data.length == 0 then return end

	if event.vk == 'VK_DOWN' or event.vk == 'VK_RIGHT' or event.type == 'wheeldown' then
		if currentItem == table.getn( data ) then return end
		currentItem = currentItem + 1

	else
		if currentItem == 1 then return end
		currentItem = currentItem - 1
	end

	dp:set( 'currentItem', currentItem )
	redraw( data[ currentItem ] )
end



redraw = function( item )
	if not item then
		data = tools.listdir( path )
		item = data[1]
	end

	meter( 'text' ):text( item.file ):update()
	local iconPath = rain:var( '#CURRENTPATH#icons/' )

	if item.type == 'folder' then
		meter( 'icon' ):image( iconPath .. 'folder' ):update()

	elseif lfs.exists( iconPath .. item.ext ..'.png' ) then
		meter( 'icon' ):image( iconPath .. item.ext ..'.png' ):update()

	elseif lfs.exists( iconPath .. 'cache/'.. item.ext ..'.png' ) then
		meter( 'icon' ):image( iconPath .. 'cache/' .. item.ext ):update()

	else
		meter( 'icon' ):image( iconPath .. 'unknown' ):update()
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



function contextMenu()
	tools.contextMenu( rain.hwnd, data[ currentItem ].filePath )
end


redraw( data[ currentItem ])
