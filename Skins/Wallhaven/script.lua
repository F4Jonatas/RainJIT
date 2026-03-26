

local depot  = require( 'depot' )
local meter  = require( 'meter' )
local anima  = require( 'meter.animate' )
local glass  = require( 'glass' )
local fetch  = require( 'fetch.utils' )
local json   = require( 'json' ).decode
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
local effect     = dp:get( 'animation-effect', 'slideup' ):lower()
local tik        = 0
local reload     = 40000
local upgradable = false



local animaEffect = {
	slideup = {
		{ -- front
			from = { opacity = 255, y = 0 },
			to   = { opacity = 80 , y = -HEIGHT }
		},
		{ -- cover
			from = { y = HEIGHT },
			to   = { y = 0 }
		}
	},

	slideleft = {
		{ -- front
			from = { x = 0 },
			to   = { x = -WIDTH }
		},
		{ -- cover
			from = { x = WIDTH },
			to   = { x = 0 }
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
url.query.category    = dp:get( 'category', '111', true )
url.query.order       = dp:get( 'order'   , 'desc'      )
url.query.purity      = dp:get( 'purity'  , '100', true )
url.query.sorting     = dp:get( 'sorting' , 'random'    )
url.query.toprange    = dp:get( 'toprange', '1y'        )
url.query.q           = fetch.url.raw( dp:get( 'query' ))



-- Apply a acrylic effect
if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect  = 'acrylic', corners = 'round' })
end



local front    = meter( 'image-front' )
local cover    = meter( 'image-cover' )

front.anima = anima( front, aDuration, aFunc )
	:from( animaEffect[ effect ][1].from )
	:to( animaEffect[ effect ][1].to )
	:create()

cover.anima = anima( cover, aDuration, aFunc )
	:from( animaEffect[ effect ][2].from )
	:to( animaEffect[ effect ][2].to )
	:create()


if effect == 'slideleft' then
	cover:left( WIDTH )
elseif effect == 'slideup' then
	cover:top( HEIGHT )
end




function rain:init()
	upgrade()
end



-- @param (int)   au number accumulated Updates
-- @param (float) dt number deltaTime
function rain:update( au, dt )
	if fetching ~= 'done' then return end

	tik = tik + 1

	if front.anima.playState == 'finished' then
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
			upgradable = false
			upgrade()
		end


	-- make animation
	elseif tik >= tok then
		anima.updateAll( 1000 * dt )
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
		local filePath = '%sdownloadfile\\thumb%02d.png'
		response:save( filePath:format( rain:var( 'CURRENTPATH' ), index ))
	end

	fetching = 'done'
	front:update( true )
	dp:set( 'wall-url', thumbs[1].url )
end)




upgrade = function()
	fetching = 'working'

	-- Request sync method
	local response = fetch( url.href )

	if response.ok then
		local index = 1
		thumbs = json( response.text ).data

		for name, value in pairs( thumbs ) do
			promise( value.thumbs.large )

			index = index + 1
			if index > 20 then break end
		end
	end
end



function doubleClick()
	local response = msgbox( 'Do you want to open the image link?' )
	if response == 6 then
		rain:bang(
			#thumbs > 0 and
			thumbs[ actualPic ].url or
			dp:get( 'wall-url', 'https://wallhaven.cc' )
		)
	end
end





local function check( param, i, toggle )
	if not toggle then
		return url.query[ param ]:sub( i, i )
	end

	local currentChar  = url.query[ param ]:sub( i, i )
	local newChar      = currentChar == '1' and '0' or '1'

	url.query[ param ] =
		url.query[ param ]:sub( 1, i -1 ) ..
		newChar..
		url.query[ param ]:sub( i +1 )

	dp:set( param, url.query[ param ])
end


local function choice( param, i, toggle )
	local list

	if param == 'sorting' then
		list = { 'relevance', 'random', 'date_added', 'views', 'favorites', 'toplist' }
	elseif param == 'order' then
		list = { 'asc', 'desc' }
	elseif param == 'toprange' then
		list = { '1d','3d','1w','1M','3M','6M','1y' }
	end

	if not toggle then
		return url.query[ param ] == list[i]
	end

	url.query[ param ] = list[i]
	dp:set( param, url.query[ param ])
end



function openMenu()
	local menuPurity = menu()
		:add( 'SFW\t\t'.. ( check( 'purity', 1 ) == '1' and '☑' or '☐' ),
			function() check( 'purity', 1, true ) end
		)
		:add( 'Sketchy\t\t'.. ( check( 'purity', 2 ) == '1' and '☑' or '☐' ),
			function() check( 'purity', 2, true ) end
		)
		:add( 'NSFW\t\t'.. ( check( 'purity', 3 ) == '1' and '☑' or '☐' ),
			function() check( 'purity', 3, true ) end
		)

	local menuCategory = menu()
		:add( 'General\t\t'.. ( check( 'category', 1 ) == '1' and '☑' or '☐' ),
			function() check( 'category', 1, true ) end
		)
		:add( 'Anime\t\t'.. ( check( 'category', 2 ) == '1' and '☑' or '☐' ),
			function() check( 'category', 2, true ) end
		)
		:add( 'People\t\t'.. ( check( 'category', 3 ) == '1' and '☑' or '☐' ),
			function() check( 'category', 3, true ) end
		)

	local menuSorting = menu()
			:add( 'Relevance\t\t'.. ( choice( 'sorting', 1 ) and '◉' or '○' ),
				function() choice( 'sorting', 1, true ) end
			)
			:add( 'Random\t\t'.. ( choice( 'sorting', 2 ) and '◉' or '○' ),
				function() choice( 'sorting', 2, true ) end
			)
			:add( 'Date Added\t\t'.. ( choice( 'sorting', 3 ) and '◉' or '○' ),
				function() choice( 'sorting', 3, true ) end
			)
			:add( 'Views\t\t'.. ( choice( 'sorting', 4 ) and '◉' or '○' ),
				function() choice( 'sorting', 4, true ) end
			)
			:add( 'Favorites\t\t'.. ( choice( 'sorting', 5 ) and '◉' or '○' ),
				function() choice( 'sorting', 5, true ) end
			)
			:add( 'Toplist\t\t'.. ( choice( 'sorting', 6 ) and '◉' or '○' ),
				function() choice( 'sorting', 6, true ) end
			)
			:add( 'Hot\t\t'.. ( choice( 'sorting', 7 ) and '◉' or '○' ),
				function() choice( 'sorting', 7, true ) end
			)

	local menuOrder = menu()
		:add( 'Ascending\t\t'.. ( choice( 'order', 1 ) and '◉' or '○' ),
			function() choice( 'order', 1, true ) end
		)
		:add( 'Descending\t\t'.. ( choice( 'order', 2 ) and '◉' or '○' ),
			function() choice( 'order', 2, true ) end
		)

	local menuTopRange = menu()
			:add( '1d\t\t'.. ( choice( 'toprange', 1 ) and '◉' or '○' ),
				function() choice( 'toprange', 1, true ) end
			)
			:add( '3d\t\t'.. ( choice( 'toprange', 2 ) and '◉' or '○' ),
				function() choice( 'toprange', 2, true ) end
			)
			:add( '1w\t\t'.. ( choice( 'toprange', 3 ) and '◉' or '○' ),
				function() choice( 'toprange', 3, true ) end
			)
			:add( '1M\t\t'.. ( choice( 'toprange', 4 ) and '◉' or '○' ),
				function() choice( 'toprange', 4, true ) end
			)
			:add( '3M\t\t'.. ( choice( 'toprange', 5 ) and '◉' or '○' ),
				function() choice( 'toprange', 5, true ) end
			)
			:add( '6M\t\t'.. ( choice( 'toprange', 6 ) and '◉' or '○' ),
				function() choice( 'toprange', 6, true ) end
			)
			:add( '1y\t\t'.. ( choice( 'toprange', 7 ) and '◉' or '○' ),
				function() choice( 'toprange', 7, true ) end
			)


	menu( rain.hwnd )
		:add( 'Copy Wallpaper URL', function()
			if not thumbs[ actualPic ] then return end

			rain:bang( '!setClip', thumbs[ actualPic ].url )
			print( thumbs[ actualPic ].url, actualPic )
		end)

		:add( '---' )
		:add( 'Settings' )

		:sub(
			menu()
				:sub( menuPurity  , 'Purity'    )
				:sub( menuCategory, 'Category'  )
				:sub( menuSorting , 'Sorting'   )
				:sub( menuOrder   , 'Order'     )
				:sub( menuTopRange, 'Top Range' )
		, 'Filters' )

		:add( '---' )
		:add( 'Restart Skin', function() rain:bang( '!refresh' ) end )
		:add( 'Default Menu', function() rain:bang( '!skinMenu' ) end )
		:show()
end
