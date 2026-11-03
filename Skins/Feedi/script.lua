--- Multi-format RSS/Atom feed viewer for a Rainmeter skin.
--
-- Fetches a feed, normalizes up to N entries (RSS 2.0, RSS 1.0/RDF or Atom)
-- and renders them inside a Trident webview. N defaults to 5 and can be set
-- with the skin variable `FeedMax`.
--
-- The format handling (root-element detection and per-field fallback chains)
-- follows the general approach documented in onikun94/obsidian-local-rss
-- (0BSD). No source code was copied from that project.
--
-- @script rssfeed
-- @license GPL-2.0-only
-- @copyright 2026 F4Jonatas

--[[
SPDX-License-Identifier: GPL-2.0-only

Copyright (C) 2026 F4Jonatas

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as published
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
]]


local depot   = require( 'depot' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )
local trident = require( 'webview.trident' )


local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )

--- Default feed URL, used when the skin does not provide one.
-- @field FEED_URL
local FEED_URL = 'https://uxplanet.org/feed'

--- Default maximum number of entries rendered.
-- Can be overridden by the skin variable `FeedMax`.
-- @field DEFAULT_MAX_ITEMS
local DEFAULT_MAX_ITEMS = 5

--- Hard upper bound for the entry limit.
-- @field ABSOLUTE_MAX_ITEMS
local ABSOLUTE_MAX_ITEMS = 50

local dp = depot()

-- Forward declarations
local upgrade
local browser

--- Guards against overlapping updates (documentcomplete may fire repeatedly).
local busy = false


if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end


--- Trim leading and trailing whitespace.
-- @tparam string s Input string.
-- @treturn string Trimmed string.
local function trim( s )
	return ( s:gsub( '^%s+', '' ):gsub( '%s+$', '' ))
end


--- Escape text for safe insertion into HTML.
-- @tparam string s Plain text.
-- @treturn string HTML-escaped text.
local function escape( s )
	local map = { ['&'] = '&amp;', ['<'] = '&lt;', ['>'] = '&gt;', ['"'] = '&quot;' }
	return ( s:gsub( '[&<>"]', map ))
end


--- Build a case-insensitive Lua pattern for a literal word.
-- @tparam string word Literal word (letters only).
-- @treturn string Pattern matching the word in any letter case.
local function ci( word )
	return ( word:gsub( '%a', function( c )
		return '[' .. c:lower() .. c:upper() .. ']'
	end ))
end


--- Best-effort sanitizer for HTML coming from a remote feed.
-- Removes active content (script, style, iframe, object, embed, ...),
-- inline event handlers and javascript: URLs. This is not a full HTML
-- sanitizer; the webview is created with sanitize = false.
-- @tparam string s Raw HTML.
-- @treturn string Sanitized HTML.
local function sanitize( s )
	for _, tag in ipairs{ 'script', 'style', 'iframe', 'object', 'embed', 'form' } do
		s = s:gsub( '<' .. ci( tag ) .. '.-</' .. ci( tag ) .. '%s*>', '' )
		s = s:gsub( '</?' .. ci( tag ) .. '[^>]*>', '' )
	end

	-- on*="..." / on*='...' / on*=value
	s = s:gsub( '%s[oO][nN]%a+%s*=%s*"[^"]*"', '' )
	s = s:gsub( "%s[oO][nN]%a+%s*=%s*'[^']*'", '' )
	s = s:gsub( '%s[oO][nN]%a+%s*=%s*[^%s>]+', '' )

	-- javascript: in href/src
	s = s:gsub( '(' .. ci( 'href' ) .. '%s*=%s*["\']?)%s*' .. ci( 'javascript' ) .. ':', '%1#' )
	s = s:gsub( '(' .. ci( 'src' )  .. '%s*=%s*["\']?)%s*' .. ci( 'javascript' ) .. ':', '%1#' )

	return s
end


--- Build an XPath that matches nested children by local name.
-- Using local-name() makes the lookup independent of namespace prefixes
-- (content:encoded, dc:creator, atom:link, ...).
-- @tparam string ... One or more element names, outermost first.
-- @treturn string XPath relative to the context node.
local function ln( ... )
	local parts = {}
	for _, name in ipairs{ ... } do
		parts[#parts + 1] = '*[local-name()="' .. name .. '"]'
	end
	return table.concat( parts, '/' )
end


--- Return the text of the first path that yields a non-empty value.
-- Errors raised by the XML library are swallowed and treated as "not found".
-- @param node XML node used as the XPath context.
-- @tparam {string,...} paths XPath expressions, tried in order.
-- @treturn string Trimmed text, or an empty string if nothing matched.
local function pick( node, paths )
	for _, path in ipairs( paths ) do
		local ok, found = pcall( function() return node:select_single( path ) end )
		if ok and found then
			local ok2, text = pcall( function() return found:text() end )
			if ok2 and type( text ) == 'string' then
				text = trim( text )
				if text ~= '' then
					return text
				end
			end
		end
	end
	return ''
end


--- Field fallback chains (first non-empty wins).
-- Covers RSS 2.0, RSS 1.0 (RDF) and Atom.
local FIELDS = {
	title   = { ln( 'title' ) },
	creator = { ln( 'creator' ), ln( 'author' ), ln( 'author', 'name' ), ln( 'contributor', 'name' ) },
	body    = { ln( 'encoded' ), ln( 'content' ), ln( 'description' ), ln( 'summary' ) },
	date    = { ln( 'pubDate' ), ln( 'published' ), ln( 'updated' ), ln( 'date' ) },
}

--- Feed title paths, relative to the document root.
local CHANNEL_TITLE = { ln( 'channel', 'title' ), ln( 'title' ) }


--- Normalized feed entry.
-- @table Entry
-- @field channel string Feed title.
-- @field title   string Entry title (plain text).
-- @field creator string Author name (plain text).
-- @field date    string Publication date as found in the feed.
-- @field body    string Entry HTML (full content if the feed provides it).

--- Resolve how many entries should be rendered.
-- Reads the skin variable `FeedMax` when available; otherwise (or when the
-- value is not a positive number) falls back to DEFAULT_MAX_ITEMS. The result
-- is clamped to ABSOLUTE_MAX_ITEMS.
-- @treturn number Integer limit, at least 1.
local function get_limit()
	local ok, value = pcall( function() return rain:var( 'FeedMax' ) end )
	local n = ok and tonumber( value ) or nil

	if not n or n < 1 then
		n = DEFAULT_MAX_ITEMS
	end

	return math.min( math.floor( n ), ABSOLUTE_MAX_ITEMS )
end


--- Parse a feed document and return up to `limit` entries.
-- Supports RSS 2.0 (rss), RSS 1.0 (rdf:RDF) and Atom (feed). Entries are
-- returned in feed order (usually newest first). Entries with neither a
-- title nor content are skipped and do not count toward the limit.
-- @param doc XML document returned by the fetch module.
-- @tparam number limit Maximum number of entries to return.
-- @treturn {Entry,...}|nil List of normalized entries, or nil on failure.
-- @treturn string|nil Error message when the first return value is nil.
local function parse_feed( doc, limit )
	local root = doc:root()
	if not root then
		return nil, 'Empty document.'
	end

	local rootName = tostring( root:name() ):lower()
	local known = rootName == 'rss' or rootName == 'feed' or rootName:match( 'rdf$' )
	if not known then
		return nil, 'Unsupported feed. Root found: ' .. rootName
	end

	local ok, nodes = pcall( function()
		return doc:select( '//*[local-name()="item" or local-name()="entry"]' )
	end )
	if not ok or not nodes then
		return nil, 'No articles found.'
	end

	local channel = pick( root, CHANNEL_TITLE )
	local entries = {}

	for item in nodes:iter() do
		local entry = {
			channel = channel,
			title   = pick( item, FIELDS.title   ),
			creator = pick( item, FIELDS.creator ),
			date    = pick( item, FIELDS.date    ),
			body    = pick( item, FIELDS.body    ),
		}

		if entry.title ~= '' or entry.body ~= '' then
			entries[#entries + 1] = entry
			if #entries >= limit then
				break
			end
		end
	end

	if #entries == 0 then
		return nil, 'No articles found.'
	end

	return entries
end


--- Convert one entry into HTML for the skin's index.html.
-- The existing ids (#title, #subHead, #channelTitle, #creator) are kept so the
-- current CSS keeps working; matching classes are added as well. Ids repeat
-- once per entry, which Trident tolerates for styling purposes.
-- @tparam Entry entry Normalized entry.
-- @treturn string HTML block for a single post.
local function render_entry( entry )
	return
		'<div class="post">' ..
			'<div id="title" class="title">' .. escape( entry.title ) .. '</div>' ..
			'<div id="subHead" class="subHead">' ..
				'<span id="channelTitle" class="channelTitle">' .. escape( entry.channel ) .. '</span>' ..
				'<span id="creator" class="creator">'           .. escape( entry.creator ) .. '</span>' ..
			'</div>' ..
			-- '<div id="date">' .. escape( entry.date ) .. '</div>' ..
			'<div class="body">' .. sanitize( entry.body ) .. '</div>' ..
		'</div>'
end


--- Convert a list of entries into the HTML for the #main element.
-- @tparam {Entry,...} entries Normalized entries.
-- @treturn string Concatenated HTML blocks, in order.
local function render( entries )
	local out = {}
	for i, entry in ipairs( entries ) do
		out[i] = render_entry( entry )
	end
	return table.concat( out )
end


--- Write HTML into #main and (re)initialize the custom scrollbar.
-- @tparam string markup HTML to display.
local function show( markup )
	local el = browser.document.getElementById( 'main' )
	el.innerHTML = markup
	browser:execScript([[
		var el = document.getElementById('main');
		if (el)
			legacyScroll.scrollbar('main',{ width:8,buttons:false,autoHide:true});
	]])
end


-- Webview instance hosting ./web/index.html.
browser = trident.create({
	url          = './web/index.html',
	width        = WIDTH - 1,
	height       = HEIGHT,
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
end


function rain:update( au, dt )
end


--- Fetch the feed and render up to the configured number of entries.
-- Failures are reported inside the skin instead of raising errors.
function upgrade()
	if busy then return end
	busy = true

	local ok, err = pcall( function()
		local data = fetch( FEED_URL )

		-- Do not rely on Content-Type: servers send application/rss+xml,
		-- application/atom+xml, text/xml, etc. Detect the format by root element.
		local doc, perr = data:xml()
		if not doc then
			error( 'Error parsing: ' .. tostring( perr ), 0 )
		end

		local entries, ferr = parse_feed( doc, get_limit() )
		if not entries then
			error( ferr, 0 )
		end

		show( render( entries ))
	end )

	if not ok then
		pcall( show, '<div id="title">' .. escape( tostring( err )) .. '</div>' )
	end

	busy = false
end
