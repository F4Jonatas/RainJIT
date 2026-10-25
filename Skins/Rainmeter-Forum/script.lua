--- Rainmeter Forum feed viewer.
-- Fetches the authenticated Atom feed asynchronously, parses it and renders
-- the entries into the embedded Trident webview. `refreshFeed` is exposed so
-- the skin can call it from any action to force a refresh.
--
-- Performance notes:
--   * Download runs through `fetch.async`, so the skin never waits on the network.
--   * Entries are built in small batches, one batch per `rain:update` tick.
--   * The DOM is replaced only when the feed signature changes.
--   * The custom scrollbar is created once; later refreshes only call
--     `legacyScroll.update` (requires legacyScroll >= 0.1.3).
--
-- Depot keys: login, passw, forum_watch, glass, forum_max (30), batch (10),
--             reload (300), debug (false).
--
-- SPDX-License-Identifier: GPL-2.0-only
-- @license GPL v2.0 License
-- @copyright (c) Jonatas Peclat
--
-- This program is free software; you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation; version 2 of the License.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License along
-- with this program; if not, write to the Free Software Foundation, Inc.,
-- 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
--
-- Third-party components: the required modules (depot, fetch, glass, array,
-- datetime.*, webview.trident) and the files under ./web (e.g. legacyScroll)
-- keep their own licenses and MUST be GPL v2.0 compatible; replace, relicense
-- or isolate any that are not.

local depot   = require( 'depot' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )
local array   = require( 'array' )
local date    = require( 'datetime.date' )
local time    = require( 'datetime.time' )
local trident = require( 'webview.trident' )


local dp         = depot()
local login      = dp:get( 'login', 'user' )
local passw      = dp:get( 'passw', 'pass' )
local watch      = dp:get( 'forum_watch', { 'Lua Scripting', 'Community Plugins & Addons' })
local maxEntries = dp:get( 'forum_max', 30 )
local batchSize  = dp:get( 'batch', 10 )
local reload     = dp:get( 'reload', 300 )
local debugOn    = dp:get( 'debug', false )

local RETRY      = 60 -- seconds before retrying after a failure

local baseURL = 'https://%s:%s@forum.rainmeter.net/feed.php?auth=http'

local headers = {
	['User-Agent'] = 'Rainmeter WebParser plugin'
}


if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end


local browser = trident.create({
	url          = './web/index.html',
	width        = rain:var( 'WIDTH'  ) - 1,
	height       = rain:var( 'HEIGHT' ),
	left         = 1,
	top          = 45,
	sanitize     = false,
	cornerRadius = 12,
	contextMenu  = false,

	callback = function( self, event )
		if event.type == 'navigate' then
			rain:bang( event.data )
			return false -- block default navigation
		end
	end
})


local SCROLL_INIT = [[
	legacyScroll.scrollbar( 'main', { width: 8, buttons: false, autoHide: true });
]]

-- Rebinds the scrollbar to the new content (keeps events and scroll position);
-- falls back to creating it when it does not exist yet.
local SCROLL_UPDATE = [[
	if ( !legacyScroll.update( 'main' )) {
		legacyScroll.scrollbar( 'main', { width: 8, buttons: false, autoHide: true });
	}
]]



--- Safely reads the text of a child node.
-- @param (table) node - Parent node.
-- @param (string) path - Child XPath.
-- @return (string) Text, or `''` when the node is missing.
local function safeText( node, path )
	local child = node:select_single( path )
	return child and child:text() or ''
end



--- Safely reads an attribute of a child node.
-- @param (table) node - Parent node.
-- @param (string) path - Child XPath.
-- @param (string) attr - Attribute name.
-- @return (string) Attribute value, or `''` when the node/attribute is missing.
local function safeAttr( node, path, attr )
	local child = node:select_single( path )
	return child and child:attribute( attr ) or ''
end



--- Strips category prefix and normalizes `Re:` chains in a title.
-- @param (string) title - Raw title.
-- @param (string) category - Category label, used to build the prefix pattern.
-- @return (string) Cleaned title.
local function cleanTitle( title, category )
	if category and category ~= '' then
		local pat = '^%s*'.. category:gsub( '%-', '%%-' ) ..'%s*•%s*'
		title = title:gsub( '&amp;', '&' ):gsub( pat, '' )
	end

	title = title:gsub( '^[Rr][Ee]%s*:%s*[Hh][Ee][Ll][Pp]%s*:', 'Re: ' )

	return title
end



--- Builds the HTML block of one feed entry.
-- @param (table) entry - XML entry node.
-- @return (string) HTML fragment.
local function buildEntry( entry )
	local author     = safeText( entry, 'author/name' )
	local updtext    = safeText( entry, 'updated' )
	local _, updated = date.todate( updtext:match( '^%s*(.-)%s*$' ))
	local link       = safeAttr( entry, 'link',     'href'   )
	local category   = safeAttr( entry, 'category', 'label'  )
	local topic_url  = safeAttr( entry, 'category', 'scheme' )
	local title      = cleanTitle( safeText( entry, 'title' ), category )

	local now    = os.time()
	local tstamp = time.totime( updtext )
	local new    = tstamp >= time.sub( now, 30, 'minutes') and tstamp <= now

	return string.format([[
		<div class="feed-group">
			<div class="ico%s"></div>
			<a href="%s" title="%s" class="title">%s</a>
			<a href="%s" title="%s" class="category">%s</a>
			<div class="posted"><span class="author">%s</span><span> » </span><span class="updated">%s</span></div>
			<div class="content">%s</div>
		</div>]],
		( array.index( watch, category ) and ' whatching' or '' ) .. ( new and ' new' or '' ),
		link, title, title,
		topic_url, category, category,
		author,
		updated ..' '.. time.totimestring( updtext ),
		''
	)
end



---------------------------------------------------------------------------
-- State
---------------------------------------------------------------------------

local busy          = false -- a request or a render job is in progress
local job           = nil   -- pending render job (see `onResponse`)
local lastSignature = nil   -- signature of the content currently displayed
local scrollReady   = false -- scrollbar already created
local nextRun       = 0     -- timestamp of the next scheduled refresh



--- Ends the current cycle and schedules the next refresh.
-- @param (boolean) ok - Whether the cycle succeeded.
local function finish( ok )
	job     = nil
	busy    = false
	nextRun = os.time() + ( ok and reload or RETRY )
end



--- Returns the `main` element once the webview has finished loading.
-- @return (table|nil) Element, or `nil` while the page is still loading.
local function getMain()
	local ok, el = pcall( function()
		return browser.document.getElementById( 'main' )
	end )

	return ok and el or nil
end



--- Cheap first pass: reads only link/updated of the newest entries and builds
-- the signature used to detect changes (includes the 30-minute "new" state).
-- @param (table) entries - XML node list.
-- @param (number) n - Number of entries to scan.
-- @return (string) Signature.
local function scan( entries, n )
	local now       = os.time()
	local threshold = time.sub( now, 30, 'minutes' )
	local sig       = {}

	for i = 1, n do
		local entry   = entries:get( i )
		local updtext = safeText( entry, 'updated' )
		local okT, ts = pcall( time.totime, updtext )
		local isNew   = okT and ts and threshold and ts >= threshold and ts <= now

		sig[ i ] = safeAttr( entry, 'link', 'href' ) ..'|'.. updtext ..'|'.. ( isNew and '1' or '0' )
	end

	return table.concat( sig, '\n' )
end



--- Handles the downloaded feed: parses it and queues the render job.
-- Raises on any failure; the caller is responsible for catching it.
-- @param (table) response - Response from `fetch.async`.
local function onResponse( response )
	local t0 = os.clock()

	assert( response.ok, 'Error retrieving the XML: '.. tostring( response.error ))

	local doc, perr = response:xml()
	assert( doc ~= nil, 'Error parsing: '.. tostring( perr ))

	local root = doc:root()
	assert(
		root:name() == 'rss' or root:name() == 'feed',
		'Invalid feed. Root found: '.. root:name()
	)

	local entries = doc:select( '//entry' )
	local n       = math.min( entries:size(), maxEntries )
	assert( n > 0, 'Feed has no entries; previous content kept' )

	local signature = scan( entries, n )

	if debugOn then
		print( string.format( '[Feed] parse+scan %.0f ms', ( os.clock() - t0 ) * 1000 ))
	end

	if signature == lastSignature then
		finish( true ) -- nothing changed: keep the current DOM
		return
	end

	-- `doc` is kept in the job so the XML nodes stay alive between ticks.
	job = {
		doc       = doc,
		entries   = entries,
		count     = n,
		index     = 1,
		parts     = {},
		signature = signature,
		started   = os.clock(),
	}
end



--- Processes one batch of the pending job. On the last batch the HTML is
-- committed to the webview in a single DOM write.
-- Raises on failure; the caller is responsible for catching it.
local function renderStep()
	local j    = job
	local last = math.min( j.index + batchSize - 1, j.count )

	for i = j.index, last do
		local okE, htmlE = pcall( buildEntry, j.entries:get( i ))

		if okE then
			j.parts[ #j.parts + 1 ] = htmlE
		else
			print( 'Entry '.. i ..' failed: '.. tostring( htmlE ))
		end
	end

	j.index = last + 1

	if j.index <= j.count then
		return -- more batches pending
	end

	assert( #j.parts > 0, 'No entry could be built; previous content kept' )

	local main = getMain()
	assert( main ~= nil, 'Webview not ready' )

	local t0 = os.clock()

	main.innerHTML = table.concat( j.parts )

	browser:execScript( scrollReady and SCROLL_UPDATE or SCROLL_INIT )
	scrollReady   = true
	lastSignature = j.signature

	if debugOn then
		print( string.format( '[Feed] %d entries | build %.0f ms | dom %.0f ms',
			#j.parts, ( t0 - j.started ) * 1000, ( os.clock() - t0 ) * 1000 ))
	end

	finish( true )
end



--- Starts an asynchronous refresh of the feed.
-- The request is non-blocking; parsing and rendering happen later (callback
-- and `rain:update` ticks). Any failure is caught and logged, and the previous
-- contents of the webview are preserved. Does nothing while a cycle is already
-- running or while the webview is still loading (retried on the next update).
-- @return (boolean) `true` when a request was started, `false` otherwise.
function refreshFeed()
	if busy or not getMain() then
		return false
	end

	busy = true

	local ok, err = pcall( function()
		fetch.async( baseURL:format( login, passw ), { headers = headers })
			:send( function( self, response )
				local okR, errR = pcall( onResponse, response )

				if not okR then
					print( 'refreshFeed failed: '.. tostring( errR ))
					finish( false )
				end
			end )
	end )

	if not ok then
		print( 'refreshFeed failed: '.. tostring( err ))
		finish( false )
		return false
	end

	return true
end



--- Called when the skin is initialized.
function rain:init()
	refreshFeed()
end



--- Periodic callback: drives the render batches and schedules refreshes by
-- wall-clock time.
function rain:update( au, dt )
	if job then
		local ok, err = pcall( renderStep )

		if not ok then
			print( 'refreshFeed failed: '.. tostring( err ))
			finish( false )
		end

	elseif not busy and os.time() >= nextRun then
		refreshFeed()
	end
end
