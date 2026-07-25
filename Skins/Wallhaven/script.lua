

local depot  = require( 'depot' )
local meter  = require( 'meter' )
local anima  = require( 'meter.animate' )
local glass  = require( 'glass' )
local fetch  = require( 'fetch.utils' )
local msgbox = require( 'winapi.msgbox' )
local menu   = require( 'winapi.popupmenu' )


local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )

local dp         = depot()
local thumbs     = {}
local fetching   = 'stopped'
local actualPic  = 1
local nextPic    = 0
local totalPic   = 20
local aDuration  = dp:get( 'animation-duration', 1500 )
local aFunc      = dp:get( 'animation-function', 'OutExpo' )
local tok        = dp:get( 'animation-delay', 2200 )
local effect     = dp:get( 'animation-effect', 'zoomfade' ):lower()
local tik        = 0
local reload     = 40000
local upgradable = false



local animaEffect = {
	slideup = {
		{ -- front
			from = { translateY = 0 },
			to   = { translateY = -HEIGHT }
		},
		{ -- cover
			from = { translateY = HEIGHT },
			to   = { translateY = 0 }
		}
	},

	slideleft = {
		{ -- front
			from = { translateX = 0 },
			to   = { translateX = -WIDTH }
		},
		{ -- cover
			from = { translateX = WIDTH },
			to   = { translateX = 0 }
		}
	},

	zoomfade = {
		{ -- front
			from = { opacity = 255, scale = 1 },
			to   = { opacity = 0  , scale = 1.5 }
		},
		{ -- cover
			from = { opacity = 150, scale = 1.2 },
			to   = { opacity = 255, scale = 1 }
		}
	}
}



-- Forward declarations
local fetchState



-- The entire URL string can be concatenated with the parameters,
-- but I prefer to use a parser to avoid empty parameters, which can corrupt the request.
local url = fetch.url( 'https://wallhaven.cc/api/v1/search' )
url.query.apikey      = dp:get( 'apikey'      )
url.query.atleast     = dp:get( 'atleast'     )
url.query.colors      = dp:get( 'colors'      )
url.query.page        = dp:get( 'page'        )
url.query.ratios      = dp:get( 'ratios'      )
url.query.resolutions = dp:get( 'resolutions' )
url.query.seed        = dp:get( 'seed'        )
url.query.toprange    = dp:get( 'toprange'    )
url.query.category    = dp:get( 'category', '111', true )
url.query.order       = dp:get( 'order'   , 'desc'      )
url.query.purity      = dp:get( 'purity'  , '100', true )
url.query.sorting     = dp:get( 'sorting' , 'random'    )
url.query.q           = fetch.url.raw( dp:get( 'query' ))
-- print(url.href)


-- Apply a acrylic effect
if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect  = 'acrylic', corners = 'round' })
end



local front = meter( 'image-front' )
local cover = meter( 'image-cover' )

front.anima = anima( front, aDuration, aFunc )
	:from( animaEffect[ effect ][1].from )
	:to( animaEffect[ effect ][1].to )
	:create()

cover.anima = anima( cover, aDuration, aFunc )
	:from( animaEffect[ effect ][2].from )
	:to( animaEffect[ effect ][2].to )
	:create()



function rain:init()
	upgrade()
end



-- @param (int)   au number accumulated Updates
-- @param (float) dt number deltaTime
function rain:update( au, dt )
	-- Evitar atualizar o meter repetidamente, consumindo hardware.
	local resChanged = false

	-- make animation
	if tik >= tok then
		anima.updateAll( 1000 * dt )

		if not resChanged and math.min( front.anima.tween.progress, 0.5 ) == 0.5 then
			resChanged = true
			meter( 'resolution' ):text( thumbs[ actualPic +1 ].resolution:gsub( 'x', '×' ) ):update()
		end
	end


	tik = tik + 1

	if front.anima.playState == 'finished' then
		resChanged = false
		actualPic  = actualPic == totalPic and 1 or actualPic + 1
		nextPic    = nextPic   == totalPic and 1 or actualPic + 1
		tik        = 0

		-- change pictures for next step
		front:image(('thumb%02d'):format( actualPic )):update()
		cover:image(('thumb%02d'):format( nextPic )):update()

		front.anima:restart()
		cover.anima:restart()


		-- update thumbs
		if upgradable then
			if fetching ~= 'done' then return end

			upgradable = false
			upgrade()
		end
	end


	-- update thumbs
	if math.fmod( au, reload ) == 0 then
		upgradable = true
	end
end





--- Crete one callback for all requests
-- @param (table) list is a all response
local promise = fetch.promiseAll( function( list )
	for index, response in ipairs( list ) do
		local filePath = '#CURRENTPATH#/downloadfile/thumb%02d.png'
		response:save( filePath:format( index ))
	end

	fetching = 'done'
	front:update( true )
	dp:set( 'wall-url', thumbs[1].url )
	meter( 'resolution' ):text( thumbs[1].resolution ):update()
end)




upgrade = function()
	fetching = 'working'

	-- Request sync method
	local response = fetch( url.href )

	if not response.ok then
		error( 'Problem performing the fetch.\nERROR: '.. response.error )
	end


	local index = 1
	thumbs = response:json().data

	for name, value in pairs( thumbs ) do
		promise( value.thumbs.large )

		index = index + 1
		if index > 20 then break end
	end
end



function doubleClick()
	local response = msgbox( 'Do you want to open the image link?' )
	if response == 6 then
		rain:bang(
			#thumbs > 0
			and thumbs[ actualPic ].url
			or  dp:get( 'wall-url', 'https://wallhaven.cc' )
		)
	end
end





local SORTING_LIST  = { 'relevance', 'random', 'date_added', 'views', 'favorites', 'toplist', 'hot' }
local ORDER_LIST    = { 'asc', 'desc' }
local TOPRANGE_LIST = { '1d', '3d', '1w', '1M', '3M', '6M', '1y' }


local function isBitOn( param, i )
	return url.query[ param ]:sub( i, i ) == '1'
end


local function toggleBit( param, i )
	local current = url.query[ param ]
	local newChar = current:sub( i, i ) == '1' and '0' or '1'

	url.query[ param ] = current:sub( 1, i - 1 ) .. newChar .. current:sub( i + 1 )
	dp:set( param, url.query[ param ] )
end


local function isSelected( param, list, i )
	return url.query[ param ] == list[ i ]
end


local function select( param, list, i )
	if isSelected( param, list, i ) then
		url.query[ param ] = ''
		dp:remove( param )
	else
		url.query[ param ] = list[ i ]
		dp:set( param, url.query[ param ] )
	end
end




function openMenu()
	local menuPurity = menu()
		:add( 'SFW\t\t'     .. ( isBitOn( 'purity', 1 ) and '☑' or '☐' ), function() toggleBit( 'purity', 1 ) end )
		:add( 'Sketchy\t\t' .. ( isBitOn( 'purity', 2 ) and '☑' or '☐' ), function() toggleBit( 'purity', 2 ) end )
		:add( 'NSFW\t\t'    .. ( isBitOn( 'purity', 3 ) and '☑' or '☐' ), function() toggleBit( 'purity', 3 ) end )

	local menuCategory = menu()
		:add( 'General\t\t' .. ( isBitOn( 'category', 1 ) and '☑' or '☐' ), function() toggleBit( 'category', 1 ) end )
		:add( 'Anime\t\t'   .. ( isBitOn( 'category', 2 ) and '☑' or '☐' ), function() toggleBit( 'category', 2 ) end )
		:add( 'People\t\t'  .. ( isBitOn( 'category', 3 ) and '☑' or '☐' ), function() toggleBit( 'category', 3 ) end )

	local menuSorting = menu()
		:add( 'Relevance\t\t'  .. ( isSelected( 'sorting', SORTING_LIST, 1 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 1 ) end )
		:add( 'Random\t\t'     .. ( isSelected( 'sorting', SORTING_LIST, 2 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 2 ) end )
		:add( 'Date Added\t\t' .. ( isSelected( 'sorting', SORTING_LIST, 3 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 3 ) end )
		:add( 'Views\t\t'      .. ( isSelected( 'sorting', SORTING_LIST, 4 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 4 ) end )
		:add( 'Favorites\t\t'  .. ( isSelected( 'sorting', SORTING_LIST, 5 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 5 ) end )
		:add( 'Toplist\t\t'    .. ( isSelected( 'sorting', SORTING_LIST, 6 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 6 ) end )
		:add( 'Hot\t\t'        .. ( isSelected( 'sorting', SORTING_LIST, 7 ) and '◉' or '○' ), function() select( 'sorting', SORTING_LIST, 7 ) end )

	local menuOrder = menu()
		:add( 'Ascending\t\t'  .. ( isSelected( 'order', ORDER_LIST, 1 ) and '◉' or '○' ), function() select( 'order', ORDER_LIST, 1 ) end )
		:add( 'Descending\t\t' .. ( isSelected( 'order', ORDER_LIST, 2 ) and '◉' or '○' ), function() select( 'order', ORDER_LIST, 2 ) end )

	local menuTopRange = menu()
		:add( '1d\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 1 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 1 ) end )
		:add( '3d\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 2 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 2 ) end )
		:add( '1w\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 3 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 3 ) end )
		:add( '1M\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 4 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 4 ) end )
		:add( '3M\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 5 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 5 ) end )
		:add( '6M\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 6 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 6 ) end )
		:add( '1y\t\t' .. ( isSelected( 'toprange', TOPRANGE_LIST, 7 ) and '◉' or '○' ), function() select( 'toprange', TOPRANGE_LIST, 7 ) end )


	menu( rain.hwnd )
		:add( 'Copy Wallpaper URL', function()
			if not thumbs[ actualPic ] then return end
			rain:bang( '!setClip', thumbs[ actualPic ].url )
		end)
		:add( '---' )
		:add( 'Settings' )
		:sub(
			menu()
				:sub( menuPurity,   'Purity'    )
				:sub( menuCategory, 'Category'  )
				:sub( menuSorting,  'Sorting'   )
				:sub( menuOrder,    'Order'     )
				:sub( menuTopRange, 'Top Range' )
		, 'Filters' )
		:add( '---' )
		:add( 'Restart Skin', function() rain:bang( '!refresh'  ) end )
		:add( 'Default Menu', function() rain:bang( '!skinMenu' ) end )
		:show()
end
