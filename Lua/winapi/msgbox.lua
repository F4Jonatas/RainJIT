
-- https://github.com/luapower/winapi/blob/master/winapi/messagebox.lua

local ffi = require( 'ffi' )
require( 'string.utf8' )


ffi.cdef[[
	int MessageBoxExW(
		void* hWnd,
		const wchar_t* lpText,
		const wchar_t* lpCaption,
		unsigned int uType,
		unsigned short wLanguageId
	);
]]



return function( content, title, flags )
	content = string.wcs( content )
	title = title or rain:var( 'CURRENTCONFIG' )
	flags = 0x00000020 + 0x00000004

	return ffi.C.MessageBoxExW(
		rain.hwnd,
		content,
		string.wcs( title ),
		flags or 0,
		lang or 0
	)
end