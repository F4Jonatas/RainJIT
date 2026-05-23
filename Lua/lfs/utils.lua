
-- https://github.com/ToxicFrog/luautil/blob/master/lfs.lua
local lfs = import( 'lfs' )
local ffi = require( 'ffi' )

local windows = package.config:sub( 1, 1 ) == '\\'

require( 'lfs.cdef' )

local shell32  = ffi.load( 'shell32' )
local ole32    = ffi.load( 'ole32' )
local kernel32 = ffi.load( 'kernel32' )


--- Converts a UTF-8 Lua string to a UTF-16 wide string.
--
-- This helper function wraps the Windows API `MultiByteToWideChar`
-- using the UTF-8 code page.
--
-- @function utf8_to_wide
-- @param string str UTF-8 encoded Lua string
-- @return cdata|nil wchar_t array or nil on failure
--
local function utf8_to_wide(str)
	local len = kernel32.MultiByteToWideChar( 65001, 0, str, -1, nil, 0 )
	if len == 0 then return nil end

	local buf = ffi.new("wchar_t[?]", len)
	kernel32.MultiByteToWideChar( 65001, 0, str, -1, buf, len)

	return buf
end


--- Converts a UTF-16 wide string to a UTF-8 Lua string.
--
-- This helper function wraps the Windows API `WideCharToMultiByte`
-- using the UTF-8 code page.
--
-- @function wide_to_utf8
-- @param cdata wstr Null-terminated UTF-16 string
-- @return string|nil UTF-8 Lua string or nil on failure
--
local function wide_to_utf8(wstr)
	local len = kernel32.WideCharToMultiByte( 65001, 0, wstr, -1, nil, 0, nil, nil )
	if len == 0 then return nil end

	local buf = ffi.new("char[?]", len)
	kernel32.WideCharToMultiByte( 65001, 0, wstr, -1, buf, len, nil, nil )

	return ffi.string(buf)
end








-- We make the simplifying assumption in these functions that path separators
-- are always forward slashes. This is true on *nix and *should* be true on
-- windows, but you can never tell what a user will put into a config file
-- somewhere. This function enforces this.
function lfs.normalize(path)
	if windows then
		return (path:gsub("\\", "/"))
	else
		return path
	end
end

local _attributes = lfs.attributes
function lfs.attributes(path, ...)
	path = lfs.normalize(path)
	if windows then
		-- Windows stat() is kind of awful. If the path has a trailing slash, it
		-- will always fail. Except on drive root directories, which *require* a
		-- trailing slash. Thankfully, appending a "." will always work if the
		-- target is a directory; and if it's not, failing on paths with trailing
		-- slashes is consistent with other OSes.
		path = path:gsub("/$", "/.")
	end

	return _attributes(path, ...)
end

function lfs.exists(path)
	return lfs.attributes(path, "mode") ~= nil
end

function lfs.dirname(oldpath)
	local path = lfs.normalize(oldpath):gsub("[^/]+/*$", "")
	if path == "" then
		return oldpath
	end
	return path
end

-- Recursive directory creation a la mkdir -p. Unlike lfs.mkdir, this will
-- create missing intermediate directories, and will not fail if the
-- destination directory already exists.
-- It assumes that the directory separator is '/' and that the path is valid
-- for the OS it's running on, e.g. no trailing slashes on windows -- it's up
-- to the caller to ensure this!
function lfs.rmkdir(path)
	path = lfs.normalize(path)
	if lfs.exists(path) then
		return true
	end
	if lfs.dirname(path) == path then
		-- We're being asked to create the root directory!
		return nil,"mkdir: unable to create root directory"
	end
	local r,err = lfs.rmkdir(lfs.dirname(path))
	if not r then
		return nil,err.." (creating "..path..")"
	end
	return lfs.mkdir(path)
end





--- Opens the native Windows folder selection dialog.
--
-- This function invokes the Win32 Shell API `SHBrowseForFolderW` to display
-- the classic Windows folder picker dialog. The dialog title is provided
-- as a UTF-8 Lua string and internally converted to UTF-16, as required
-- by the Windows Unicode API.
--
-- The function blocks execution until the user either selects a folder
-- or cancels the dialog.
--
-- ### Unicode handling
--
-- All strings passed to and returned from this function are UTF-8 encoded.
-- Internally, UTF-8 ⇄ UTF-16 conversion is performed using
-- `MultiByteToWideChar` and `WideCharToMultiByte`.
--
-- ### Memory management
--
-- - The PIDL returned by `SHBrowseForFolderW` is explicitly released
--   using `CoTaskMemFree`.
-- - All other allocated buffers are managed by LuaJIT's garbage collector.
--
-- ### Platform requirements
--
-- - Windows 7 or newer
-- - LuaJIT (Lua 5.1 ABI)
--
-- @function lfs.selectFolder
-- @param[opt] string title
--   Title displayed at the top of the folder selection dialog.
--   Must be a UTF-8 encoded Lua string.
--   Defaults to `"Select a folder"`.
--
-- @return string|nil
--   Returns the selected folder path as a UTF-8 string if the user
--   confirms the dialog.
--
--   Returns `nil` if the user cancels the dialog or if an internal error
--   occurs.
--
-- @usage
-- local folder = lfs.selectFolder("Choose an output directory")
-- if folder then
--     print("Selected:", folder)
-- else
--     print("No folder selected")
-- end
--
-- @see https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shbrowseforfolderw
-- @see https://learn.microsoft.com/pt-pt/windows/win32/api/shlobj_core/ns-shlobj_core-browseinfow
--
function lfs.selectFolder( title, flags )
	title = title or 'Select a folder'

	local title_w = utf8_to_wide(title)
	if not title_w then return nil end

	local path_buf = ffi.new('wchar_t[260]')

	local bi = ffi.new('BROWSEINFOW')
	bi.hwndOwner = rain.hwnd
	bi.pidlRoot = nil
	bi.pszDisplayName = path_buf
	bi.lpszTitle = title_w
	bi.ulFlags = flags or 1 + ( title:find( '\n' ) and 4 or 0 ) -- BIF_RETURNONLYFSDIRS + BIF_STATUSTEXT
	bi.lpfn = nil
	bi.lParam = 0
	bi.iImage = 0

	local pidl = shell32.SHBrowseForFolderW(bi)
	if pidl == nil then
		return nil
	end

	local ok = shell32.SHGetPathFromIDListW(pidl, path_buf)
	ole32.CoTaskMemFree(pidl)

	if ok == 0 then
		return nil
	end

	return wide_to_utf8(path_buf)
end


return lfs