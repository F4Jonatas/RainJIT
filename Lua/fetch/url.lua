--- URL parsing and manipulation library for Lua.
-- This library provides comprehensive URL parsing, manipulation, and construction
-- following RFC 3986 standards. It supports parsing all URL components including
-- scheme, host, port, path, query parameters, and fragments.
--
-- @submodule fetch.url
-- @author F4Jonatas
-- @release 1.6.0
-- @license MIT


--- inspirations:
-- https://github.com/LuaDist/lua-uri/tree/master
-- https://help.interfaceware.com/code/details/urlcode-lua
-- https://github.com/chuan-yun/lua-tracing/blob/master/resty/url.lua
-- https://stackoverflow.com/questions/27745/getting-parts-of-a-url-regex/45708666#45708666


local M = {}
M.__index = M



local function encodeValue(v)
	if type(v) == 'table' and v.__raw then
		return v.value
	end

	return M.encode(v)
end


-- Metatable for query parameters to add dynamic getters
local QUERY_MT = {
	__index = function( self, key )
		return self.__data[ key ]
	end,


	__newindex = function(self, key, value)
		local data = self.__data

		if value == nil then
			rawset(data, key, nil)

		elseif type(value) == 'table' and value.__raw then
			-- Preserve raw values without modification
			if value.value ~= nil then
				rawset(data, key, nil)
			else
				rawset(data, key, value)
			end

		elseif type(value) == 'string' then
			local trimmed = value:match('^%s*(.-)%s*$')

			if trimmed == '' then
					rawset(data, key, nil)
			else
					rawset(data, key, trimmed)
			end

		elseif type(value) == 'number' then
			rawset(data, key, tostring(value))

		else
			error('Query parameter must be string, number or raw, got '.. type(value), 2)
		end
	end,



	__pairs = function(t)
		return pairs( t.__data )
	end
}




-- Metatable for result to add dynamic getters
local RESULT_MT = {
	__index = function( t, k )
		if k == 'search' then
			local query_parts = {}

			-- Collect and sort keys (deterministic order)
			local keys = {}
			for key in pairs(t.query) do
				keys[#keys + 1] = key
			end
			table.sort(keys)

			-- Build query string
			for _, key in ipairs(keys) do
				local val = t.query[key]

				if type(val) == 'table' and not val.__raw then
					-- multi-value array
					for _, v in ipairs(val) do
						query_parts[#query_parts + 1] =
							M.encode(key) .. '=' .. encodeValue(v)
					end

				elseif val ~= nil then
					-- single value (string, number, or RAW)
					query_parts[#query_parts + 1] =
						M.encode(key) .. '=' .. encodeValue(val)
				end
			end

			if #query_parts > 0 then
				return '?' .. table.concat(query_parts, '&')
			else
				return ''
			end



		elseif k == 'fullpath' then
			-- Build fullpath dynamically
			local path = t.pathname or ''
			local search = t.search or ''
			local fragment_part = ''

			-- Include fragment or hashparams in fullpath
			if t.fragment then
				fragment_part = '#' .. M.encode(t.fragment)
			elseif t.hashparams then
				-- Build hashparams string
				local hash_parts = {}
				for key, val in pairs(t.hashparams) do
					if type(val) == 'table' and not val.__raw then
						for _, v in ipairs(val) do
							hash_parts[#hash_parts + 1] = M.encode(key) .. '=' .. encodeValue(v)
						end
					elseif val ~= nil then
						hash_parts[#hash_parts + 1] = M.encode(key) .. '=' .. encodeValue(val)
					end
				end
				if #hash_parts > 0 then
					fragment_part = '#' .. table.concat(hash_parts, '&')
				end
			end

			return path .. search .. fragment_part
		elseif k == 'href' then
			-- Build complete href dynamically
			local href = ''
			if t.scheme then
				href = href .. t.scheme .. ':'
			end
			if t.authority then
				href = href .. '//'
				if t.username then
					href = href .. M.encode(t.username)
					if t.password then
						href = href .. ':' .. M.encode(t.password)
					end
					href = href .. '@'
				end
				href = href .. (t.host or '')
				if t.port then
					href = href .. ':' .. t.port
				end
			end
			href = href .. (t.fullpath or '')
			return href
		end
	end
}



--- Decodes percent-encoded strings (RFC 3986).
--
-- This function converts percent-encoded characters (%XX) back to their original
-- form and also converts '+' characters to spaces (application/x-www-form-urlencoded).
--
-- @function unescape
-- @param str (string) The percent-encoded string to decode
-- @return (string) The decoded string
-- @usage
-- local decoded = url.unescape("Hello%20World%2B%21")  -- Returns "Hello World+!"
--
M.unescape = function( str )
	return string.gsub( str:gsub( '+', ' ' ), '%%(%x%x)', function( hex )
		return string.char( tonumber( hex, 16 ))
	end )
end



--- Creates a raw value wrapper to bypass encoding/decoding.
-- When a value is wrapped with `raw`, it will be used as-is without percent-encoding
-- when building URLs or query strings.
-- @function raw
-- @param value (string|number) The value to wrap
-- @return (table) A raw value object with `__raw` flag
-- @usage
-- local r = url.raw("already%20encoded")
--
M.raw = function( value )
	return {
		__raw = true,
		value = tostring( value )
	}
end


---Percent-encodes a string following RFC 3986.
--
-- Encodes all characters except unreserved characters (ALPHA, DIGIT, '-', '.', '_', '~')
-- and any additional characters specified in the `raw` parameter.
--
-- @function encode
-- @param uri (string) The string to encode
-- @param[opt] raw (string) Additional characters to preserve without encoding
-- @return (string) The percent-encoded string
-- @raise Error if uri is nil
-- @usage
-- local encoded = url.encode("hello world!")  -- Returns "hello%20world%21"
-- local encoded2 = url.encode("a+b", "+")     -- Returns "a+b" (preserves '+')
--
M.encode = function( uri, raw )
	if uri == nil then return '' end
	uri = tostring(uri)

	-- RFC 3986 unreserved characters
	local unreserved = "%-%.%_%~%w"

	-- Add raw characters if provided (properly escaped for pattern)
	if raw then
		-- Escape special regex characters
		raw = raw:gsub("([%%%]%^%-$()%.%[%]*+?])", "%%%1")
		unreserved = unreserved .. raw
	end

	-- Percent-encode everything else
	return uri:gsub( '([^'.. unreserved ..'])', function( c )
		return string.format( '%%%02X', string.byte( c ))
	end)
end



--- Creates a smart query table with controlled assignment.
--
-- The smart query table accepts only string and number values, automatically:
--   Removes nil values
--   Trims and removes empty strings
--   Converts numbers to strings
--   Rejects other data types with an error
--
-- @local
-- @function create_query_table
-- @param[opt] query_data (table) Initial query data
-- @return (table) Smart query table proxy
--
local create_query_table = function(query_data)
	query_data = query_data or {}

	local proxy = {
		__data = query_data
	}

	return setmetatable(proxy, QUERY_MT)
end




--- Breaks a path into its segments, unescaping each segment.
--
-- @local
-- @function parsepath
-- @param path (string) The path string to parse
-- @return (table) Array of path segments
-- @usage
-- local segments = parsepath("/api/v1/users")  -- Returns {"api", "v1", "users"}
--
M.parsepath = function( path )
	local parsed = {}
	path = path or ''

	-- Remove leading slash if present for cleaner segments
	local clean_path = path:gsub( '^/', '' )

	string.gsub( clean_path, '([^/]+)', function( str )
		parsed[ #parsed + 1 ] = M.unescape( str )
	end)

	return parsed
end



--- Parses a fragment string, detecting whether it contains hash parameters.
--
-- Some APIs (like OAuth2) use the fragment for parameters: #access_token=abc&expires=3600
-- This function detects if the fragment contains key-value pairs and parses them accordingly.
--
-- @local
-- @function parse_fragment_and_hashparams
-- @param fragment_str (string) The fragment string (without leading '#')
-- @return (table) Table containing either fragment or hashparams
--   fragment (string): Simple fragment identifier if no parameters detected
--   hashparams (table): Smart table of parsed parameters if fragment contains key-value pairs
--
local parse_fragment_and_hashparams = function(fragment_str)
	local result = {
		fragment = fragment_str, -- The raw fragment
		hashparams = {}          -- Parsed hash parameters (if any)
	}

	if not fragment_str or fragment_str == '' then
		return result
	end

	-- Check if fragment contains parameters (contains '=' or '&')
	if fragment_str:find('=') or fragment_str:find('&') then
		-- Try to parse as hash parameters
		local params = {}
		string.gsub(fragment_str, '([^&=]+)=([^&=]*)&?', function(key, val)
			key = M.unescape(key)
			val = M.unescape(val)

			if not params[key] then
				params[key] = val
			else
				local t = type(params[key])
				if t == 'string' then
					params[key] = {params[key], val}
				elseif t == 'table' then
					params[key][#params[key] + 1] = val
				end
			end
		end)

		-- If we successfully parsed parameters, use them
		if next(params) ~= nil then
			result.hashparams = create_query_table(params)
			result.fragment = nil -- Not a simple fragment
		end
	end

	return result
end



--- Parses a query string into a smart query table.
--
-- Supports multiple values for the same parameter (converts to array).
-- Empty parameters and values are filtered out.
--
-- @local
-- @function parsequery
-- @param query_string (string) The query string to parse (without leading '?')
-- @return (table) Smart query table
-- @usage
-- local query = parsequery("name=John&age=30&tags=lua&tags=url")
-- -- Returns: {name="John", age="30", tags={"lua", "url"}}
--
M.parsequery = function(query_string)
	if type(query_string) ~= 'string' then
		return create_query_table()
	end

	local raw_data = {}

	for pair in query_string:gmatch('[^&]+') do
		local key, val = pair:match('([^=]*)=?(.*)')

		-- Decode
		key = M.unescape(key or '')
		val = M.unescape(val or '')

		-- Trim whitespace
		key = key:match('^%s*(.-)%s*$') or key
		val = val:match('^%s*(.-)%s*$') or val

		-- Ignore empty keys
		if key ~= '' then
			if raw_data[key] == nil then
				raw_data[key] = val
			else
				local t = type(raw_data[key])

				if t == 'string' then
					raw_data[key] = { raw_data[key], val }

				elseif t == 'table' then
					raw_data[key][#raw_data[key] + 1] = val
				end
			end
		end
	end

	return create_query_table(raw_data)
end



--- Parses a complete URL into its components.
--
-- Parses all URL components: scheme, authority, host, port, path, query, and fragment/hashparams.
-- Returns a table with the following structure:
--   scheme (string): Protocol (http, https, etc.)
--   authority (boolean): True if URL has '//'
--   host (string): Hostname or IP address
--   port (number): Port number if specified
--   username (string): Username from userinfo
--   password (string): Password from userinfo
--   pathname (string): Full path as string
--   path (table): Array of path segments
--   query (table): Smart query parameter table
--   fragment (string): Fragment identifier (if simple fragment)
--   hashparams (table): Smart table of hash parameters (if fragment contains key-value pairs)
--   hash (string): Alias for fragment
--   origin (string): scheme://host[:port]
--   search (string): Query string with leading '?'
--   fullpath (string): pathname + search + fragment/hashparams
--   href (string): Complete URL
--
-- @function parse
-- @param url (string) The URL to parse
-- @return (table) Parsed URL object
-- @raise Error if url is not a string
-- @usage
-- -- Standard fragment
-- local url1 = url.parse("https://example.com/page#section-2")
-- -- Hash parameters (OAuth2 style)
-- local url2 = url.parse("https://app.com/callback#access_token=abc&expires=3600")
--
M.parse = function( str )
	if type( str ) ~= 'string' then
		error( 'Invalid url: ' .. type( str ))
	end

	-- Work with a copy to avoid modifying original
	local remaining = str
	local result = {
		__type = 'fetch.url'
	}

	-- 1. Parse scheme (protocol)
	local scheme = remaining:match( '^([a-zA-Z][a-zA-Z0-9+.-]*):' )
	if scheme then
		result.scheme = scheme
		remaining = remaining:sub( #scheme + 2 ) -- Remove scheme and ':'
	end

	-- 2. Check for authority (//)
	if remaining:sub( 1, 2 ) == '//' then
		result.authority = true
		remaining = remaining:sub( 3 ) -- Remove '//'

		-- 3. Parse userinfo (optional) - username:password@
		local userinfo, rest = remaining:match( '^([^/@]+)@([^/]*)' )
		if userinfo then
			result.userinfo = userinfo
			remaining = rest
			-- Split into username and password if present
			local username, password = result.userinfo:match('^([^:]+):?(.*)$')
			result.username = M.unescape(username)
			if password and password ~= '' then
				result.password = M.unescape(password)
			end
		end

		-- 4. Parse host and port
		-- Host can be: domain, IPv4, IPv6 in brackets, or hostname

		-- Check for IPv6 address
		if remaining:sub(1, 1) == '[' then
			-- IPv6 address
			local ipv6, after_bracket = remaining:match('%[([%x:.]+)%](.*)')
			if ipv6 then
				result.host = '[' .. ipv6 .. ']'
				remaining = after_bracket or ''
			end
		else
			-- Regular host (stop at : / ? #)
			local host_match = remaining:match('^([^:/?#]+)')
			if host_match then
				result.host = host_match
				remaining = remaining:sub(#host_match + 1)
			end
		end

		-- 5. Parse port
		if remaining:sub(1, 1) == ':' then
			local port = remaining:match('^:([0-9]+)')
			if port then
				result.port = tonumber(port)
				remaining = remaining:sub(#port + 2) -- Remove ':' and port
			end
		end
	end

	-- 6. Parse pathname (string) and path (array)
	local path_match = remaining:match('^([^?#]*)')
	if path_match and path_match ~= '' then
		-- pathname: full path as string
		result.pathname = path_match

		-- path: array of path segments (already decoded)
		result.path = M.parsepath(path_match)

		remaining = remaining:sub(#path_match + 1)
	else
		-- For empty paths
		result.pathname = ''
		result.path = {}
	end

	-- 7. Parse query string (smart table)
	if remaining:sub(1, 1) == '?' then
		local query_string = remaining:match('^%?([^#]*)')
		if query_string and query_string ~= '' then
			-- query: smart table with parsed parameters
			result.query = M.parsequery(query_string)
			remaining = remaining:sub(#query_string + 2) -- Remove '?' and query
		else
			-- Empty query (? without parameters) - still create smart table
			result.query = M.parsequery()
		end
	else
		-- No query string - create empty smart table
		result.query = M.parsequery()
	end

	-- 8. Parse fragment/hash parameters
	if remaining:sub(1, 1) == '#' then
		local fragment_str = remaining:sub(2)
		if fragment_str and fragment_str ~= '' then
			-- Parse as both fragment and possible hashparams
			local fragment_data = parse_fragment_and_hashparams(fragment_str)

			if fragment_data.fragment then
				result.fragment = fragment_data.fragment
				result.hash = fragment_data.fragment -- alias
			end

			if fragment_data.hashparams and next(fragment_data.hashparams) ~= nil then
				result.hashparams = fragment_data.hashparams
			end
		end
	end

	-- 9. Build derived fields
	if result.scheme and result.host then
		result.origin = result.scheme .. '://' .. result.host
		if result.port then
			result.origin = result.origin .. ':' .. result.port
		end
	end


	setmetatable( result, RESULT_MT )
	return result
end



--- Builds a URL from its component parts.
--
-- Accepts a table with URL components and returns the complete URL string.
-- Supports all components parsed by `url.parse()`, including hashparams.
--
-- @function build
-- @param parts (table) Table containing URL components
-- @return (string) Complete URL string
-- @usage
-- -- Standard URL with fragment
-- local url1 = url.build{
-- 	scheme = "https",
-- 	host = "example.com",
-- 	path = {"api", "v1", "users"},
-- 	query = {page = "2", sort = "name"},
-- 	fragment = "results"
-- }
-- -- URL with hash parameters (OAuth2 style)
-- local url2 = url.build{
-- 	scheme = "https",
-- 	host = "app.com",
-- 	pathname = "/callback",
-- 	hashparams = {
-- 		access_token = "abc123",
-- 		token_type = "Bearer",
-- 		expires_in = "3600"
-- 	}
-- }
--
M.build = function(parts)
	local url = ''

	if parts.scheme then
		url = url .. parts.scheme .. ':'
	end

	if parts.host then
		url = url .. '//'

		if parts.username then
			url = url .. M.encode(parts.username)
			if parts.password then
				url = url .. ':' .. M.encode(parts.password)
			end
			url = url .. '@'
		end

		url = url .. parts.host

		if parts.port then
			url = url .. ':' .. parts.port
		end
	end

	-- Build pathname from path array or use pathname string
	local pathname = ''
	if parts.path and type(parts.path) == 'table' then
		-- Reconstruct from array
		if #parts.path > 0 then
			-- Make path absolute if host exists
			pathname = table.concat(parts.path, '/')
			if parts.host then
				pathname = '/' .. pathname
			end
		end
	elseif parts.pathname then
		-- Use direct pathname string
		pathname = parts.pathname
	end

	-- Ensure proper formatting
	if pathname ~= '' then
		-- Add leading slash if needed and not present
		if parts.host and not pathname:match('^/') then
			pathname = '/' .. pathname
		end
		url = url .. pathname
	end

	-- Build query string from query table
	if parts.query and type(parts.query) == 'table' then
		local query_parts = {}
		for k, v in pairs(parts.query) do
			if type(v) == 'table' and not v.__raw then
				for _, val in ipairs(v) do
					query_parts[#query_parts + 1] = M.encode(k) .. '=' .. encodeValue(val)
				end
			elseif v ~= nil then
				query_parts[#query_parts + 1] = M.encode(k) .. '=' .. encodeValue(v)
			end
		end
		if #query_parts > 0 then
			url = url .. '?' .. table.concat(query_parts, '&')
		end
	end

	-- Add fragment or hashparams
	if parts.fragment then
		url = url .. '#' .. M.encode(parts.fragment)
	elseif parts.hashparams then
		-- Build hash parameters string
		local hash_parts = {}
		for k, v in pairs(parts.hashparams) do
			if type(v) == 'table' then
				for _, val in ipairs(v) do
					hash_parts[#hash_parts + 1] = M.encode(k) .. '=' .. encodeValue(val)
				end
			elseif v ~= nil then
				hash_parts[#hash_parts + 1] = M.encode(k) .. '=' .. encodeValue(v)
			end
		end
		if #hash_parts > 0 then
			url = url .. '#' .. table.concat(hash_parts, '&')
		end
	end

	return url
end




local ok, fetch = pcall( require, 'fetch' )

if ok then
	fetch.url = setmetatable( M, {
		__call = function( self, url )
			return M.parse( url )
		end
	})
end


return M