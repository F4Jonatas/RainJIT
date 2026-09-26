local depot   = require( 'depot' )
-- local meter   = require( 'meter' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )
local trident = require( 'webview.trident'  )



local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )

local dp      = depot()
local login   = dp:get( 'login', 'user' )
local passw   = dp:get( 'passw', 'pass' )
local baseURL = 'https://%s:%s@forum.rainmeter.net/feed.php?auth=http'
local headers = {
	['User-Agent'] = 'Rainmeter WebParser plugin'
}




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
		if event.type == 'navigate' then
			rain:bang( event.data )
			return false
		end
	end
})




function rain:init()
	local response = fetch( baseURL:format( login, passw ), { headers = headers })
	assert( response.ok, 'Erro ao obter o XML' )
	response:save( 'feeds.xml' )

	local doc, err = response:xml()
	assert( doc ~= nil, 'Error parsing: '.. tostring( err ))

	local root = doc:root()
	assert(
		root:name() == 'rss' or root:name() == 'feed',
		'Invalid feed. Root found: '.. root:name()
	)


	local entries = doc:select( '//entry' )
	local inner   = ''

	for i = 1, entries:size() do
		local entry = entries:get( i )

		local author_name = entry:select_single( 'author/name' ):text()
		local updated     = entry:select_single( 'updated' ):text()
		local link        = entry:select_single( 'link' ):attribute( 'href' )
		local category    = entry:select_single( 'category' )
		local topic_url   = category:attribute( 'scheme' )
		local title       = entry:select_single( 'title' ):text()
		category          = category:attribute( 'label' )

		title = title:gsub( '^%s*'.. category:gsub( '%-', '%%-' ) ..'%s*•%s*', '' )

		inner = inner .. string.format([[
			<div class="feed-group">
				<div class="ico"></div>
				<a href="%s" title="%s" class="title">%s</a>
				<a href="%s" title="%s" class="category">%s</a>
				<div class="posted"><span class="author">%s</span><span>  »  </span><span class="updated">%s</span></div>
			</div>]],
			link, link,
			title,
			topic_url,
			topic_url, category,
			author_name,
			updated
		)
	end

	browser.document.getElementById( 'main' ).innerHTML = inner
	browser:execScript([[
		legacyScroll.scrollbar( 'main', { width: 8, buttons: false, autoHide: true })
	]])


	local file = io.open( rain:absPath( 'example.txt' ), 'w' )
	if file then
		file:write( inner ) -- Write content to the file
		file:close() -- Close the file handle
	else
		print( 'Could not create file.' )
	end
end

