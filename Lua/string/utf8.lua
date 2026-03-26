-- https://github.com/luapower/winapi/blob/84c32de365a6eb8281f5e3192c83744a04af11ad/winapi/wcs.lua#L74

local ffi = require( 'ffi' )


ffi.cdef[[
	int MultiByteToWideChar(
		unsigned int CodePage,
		unsigned long dwFlags,
		const char* lpMultiByteStr,
		int cbMultiByte,
		wchar_t* lpWideCharStr,
		int cchWideChar
	);

	int WideCharToMultiByte(
		unsigned int CodePage,
		unsigned long dwFlags,
		const wchar_t* lpWideCharStr,
		int cchWideChar,
		char* lpMultiByteStr,
		int cbMultiByte,
		const char* lpDefaultChar,
		int* lpUsedDefaultChar
	);
]]



local utf8 = {}
utf8.__index = utf8

--- Converts a UTF-8 Lua string to a UTF-16 wide string.
--
-- This helper function wraps the Windows API `MultiByteToWideChar`
-- using the UTF-8 code page.
--
-- @function wcs
-- @tparam string str UTF-8 encoded Lua string
-- @treturn cdata|nil wchar_t array or nil on failure
--
string.wcs = function(str)
	assert(type(str) == 'string', 'wcs expects Lua string (UTF-8)' )

	local len = ffi.C.MultiByteToWideChar(65001, 0, str, -1, nil, 0)
	if len == 0 then return nil end

	local buf = ffi.new( 'wchar_t[?]', len)
	ffi.C.MultiByteToWideChar(65001, 0, str, -1, buf, len)

	return buf
end


--- Converts a UTF-16 wide string to a UTF-8 Lua string.
--
-- This helper function wraps the Windows API `WideCharToMultiByte`
-- using the UTF-8 code page.
--
-- @function mbs
-- @tparam cdata wstr Null-terminated UTF-16 string
-- @treturn string|nil UTF-8 Lua string or nil on failure
--
string.mbs = function(wstr)
	assert(
		ffi.istype( 'wchar_t*', wstr ) or ffi.istype( 'wchar_t[?]', wstr ),
		'mbs expects wchar_t*'
	)

	local len = ffi.C.WideCharToMultiByte(65001, 0, wstr, -1, nil, 0, nil, nil)
	if len == 0 then return nil end

	local buf = ffi.new('char[?]', len)
	ffi.C.WideCharToMultiByte(65001, 0, wstr, -1, buf, len, nil, nil)

	return ffi.string(buf)
end



---
--
string.utf8_remove_last = function( str )
	local len = #str
	if len == 0 then
		return str
	end

	local i = len

	-- while byte is a continuation byte (10xxxxxx)
	while i > 0 do
		local byte = str:byte(i)

		if byte >= 128 and byte < 192 then
			i = i - 1
		else
			break
		end
	end

	return str:sub(1, i - 1)
end





utf8.char = function(cp)
	if cp <= 0x7F then
		return string.char(cp)

	elseif cp <= 0x7FF then
		return string.char(
			0xC0 + math.floor(cp / 0x40),
			0x80 + (cp % 0x40)
		)

	elseif cp <= 0xFFFF then
		return string.char(
			0xE0 + math.floor(cp / 0x1000),
			0x80 + (math.floor(cp / 0x40) % 0x40),
			0x80 + (cp % 0x40)
		)

	else
		return string.char(
			0xF0 + math.floor(cp / 0x40000),
			0x80 + (math.floor(cp / 0x1000) % 0x40),
			0x80 + (math.floor(cp / 0x40) % 0x40),
			0x80 + (cp % 0x40)
		)
	end
end


return utf8