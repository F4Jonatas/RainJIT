
local depot   = require( 'depot' )
local meter   = require( 'meter' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )
local html    = require( 'html'  )
local trident = require( 'webview.trident'  )



local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )

local dp  = depot()


-- Forward declarations
local upgrade



if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end




local browser = trident.create({
	url          = './web/index.html',
	width        = rain:var( 'WIDTH' ) - 1,
	height       = rain:var( 'HEIGHT' ),
	left         = 1,
	top          = 45,
	sanitize     = false,
	cornerRadius = 12,
	contextMenu  = false,

	callback = function( self, event )
		if event.type == 'documentcomplete' then
			upgrade()
		end

		if event.type == 'navigate' then
			rain:bang( event.data )
			return false
		end
	end
})



function rain:init()
	-- local title = meter( 'title' )
	-- title:inline( 5, 'size', rain:formula( title:option( 'fontSize' )) - 2 )
	-- upgrade()
end


function rain:update( au, dt )
end



function upgrade()
	local data    = fetch( 'https://uxplanet.org/feed' )
	local headers = data:headers()
	local ext     = headers['content-type']:match( '/(%w+)' ):lower()

	if ext == 'xml' then
		local from = headers['x-powered-by']:lower()
		-- if from == 'medium' then
		-- 	require( 'medium' ).init( data.text )
		-- end

		local doc, err = data:xml()
		assert( doc ~= nil, 'Error parsing: '.. tostring( err ))

		local root = doc:root()
		assert( root:name() == 'rss', 'Invalid feed. Root found: '.. root:name())


		local channelTitle = doc:select_single( '//channel/title' ):text()

		local item = doc:select_single( '//item' )
		assert( item ~= false, 'No articles found.' )

		-- Navegar dentro do item
		local title = item:select_single( 'title'       ):text()
		-- local link  = item:select_single( 'link'        ):text()
		local date  = item:select_single( 'pubDate'     ):text()
		local inner = item:select_single( 'description' ):text()
		local html, err = html.parse( inner )

		-- dc:creator usa namespace, usar XPath com wildcard local-name()
		local creator_node = item:select_single( '*[local-name()="creator"]' )
		local creator = creator_node and creator_node:text() or ''

		-- Categorias (pode haver múltiplas)
		local categories = {}
		for cat in item:select( 'category' ):iter() do
			table.insert( categories, cat:text() )
		end


		inner =
			'<div id="title">' ..title.. '</div>' ..
			'<div id="subHead">' ..
				'<span id="channelTitle">' ..channelTitle.. '</span>' ..
				'<span id="creator">' ..creator.. '</span>' ..
			'</div>' ..
			-- '<div id="date">' ..date.. '</div>' ..
			inner


		local snippet = html:find( 'p.medium-feed-snippet' )
		if snippet then
			local el = browser.document.getElementById( 'main' )
			el.innerHTML = inner
			-- print( browser.window.legacyScroll, browser.legacyScroll )
			browser:execScript(([[
				var el = document.getElementById('main');
				if (el)
					legacyScroll.scrollbar('main',{ width:8,buttons:false,autoHide:true});
			]]))
		end
		-- print("Tags:", table.concat(categories, ", "))
	end
end
