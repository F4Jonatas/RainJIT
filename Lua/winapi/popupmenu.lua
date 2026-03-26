-----------------------------------------------------------------------
-- @module popupmenu.lua
--
-- Native Win32 popup menu helper using LuaJIT FFI.
-- This module provides a safe abstraction over CreatePopupMenu,
-- supporting icons via bitmap files, submenus, separators and callbacks,
-- while preserving the native Windows menu layout and theme behavior.
--
-- Owner-draw, character icons and font manipulation are intentionally
-- NOT supported to avoid layout breakage and WndProc dependencies.
-----------------------------------------------------------------------

local ffi = require("ffi")
local bit = bit or require("bit")

-----------------------------------------------------------------------
-- Win32 API declarations
-----------------------------------------------------------------------
ffi.cdef[[
typedef unsigned long	DWORD;
typedef unsigned int	UINT;
typedef int				BOOL;
typedef void*			HWND;
typedef void*			HMENU;
typedef void*			HBITMAP;
typedef void*			HANDLE;
typedef HANDLE			HBRUSH;
typedef unsigned long	ULONG_PTR;

typedef struct {
	long x;
	long y;
} POINT;

typedef struct {
	UINT		cbSize;
	UINT		fMask;
	UINT		fType;
	UINT		fState;
	UINT		wID;
	HMENU		hSubMenu;
	HBITMAP		hbmpChecked;
	HBITMAP		hbmpUnchecked;
	ULONG_PTR	dwItemData;
	const wchar_t*	dwTypeData;
	UINT		cch;
	HBITMAP		hbmpItem;
} MENUITEMINFOW;

typedef struct {
	DWORD	cbSize;
	DWORD	fMask;
	DWORD	dwStyle;
	UINT	cyMax;
	HBRUSH	hbrBack;
	DWORD	dwContextHelpID;
	ULONG_PTR	dwMenuData;
} MENUINFO;

BOOL	SetMenuInfo(HMENU, MENUINFO*);
HMENU	CreatePopupMenu(void);
BOOL	InsertMenuItemW(HMENU, UINT, BOOL, const MENUITEMINFOW*);
BOOL	TrackPopupMenuEx(HMENU, UINT, int, int, HWND, void*);
BOOL	DestroyMenu(HMENU);
BOOL	GetCursorPos(POINT*);
HWND	GetForegroundWindow(void);
BOOL	SetForegroundWindow(HWND);

HANDLE	LoadImageW(void*, const wchar_t*, UINT, int, int, UINT);
BOOL	DeleteObject(HANDLE);

int		MultiByteToWideChar(
			unsigned int,
			unsigned long,
			const char*,
			int,
			wchar_t*,
			int
		);

DWORD	GetLastError(void);
]]

local user32   = ffi.load("user32")
local kernel32 = ffi.load("kernel32")

-----------------------------------------------------------------------
-- Constants
-----------------------------------------------------------------------
local IMAGE_BITMAP			= 0
local LR_LOADFROMFILE		= 0x0010
local LR_CREATEDIBSECTION	= 0x2000

local MIIM_STRING	= 0x40
local MIIM_ID		= 0x02
local MIIM_BITMAP	= 0x80
local MIIM_FTYPE	= 0x100
local MIIM_SUBMENU	= 0x04

local MFT_SEPARATOR = 0x0800

local TPM_RIGHTBUTTON	= 0x0002
local TPM_RETURNCMD		= 0x0100

local CP_UTF8 = 65001

local MIM_STYLE			= 0x00000010
local MNS_CHECKORBMP	= 0x04000000
local MNS_AUTODISMISS	= 0x10000000

-----------------------------------------------------------------------
-- Internal caches
-----------------------------------------------------------------------
local WSTRING_CACHE	= setmetatable({}, { __mode = "v" })
local BITMAP_CACHE	= {}
local CALLBACKS		= {}
local NEXT_ID		= 1000

-----------------------------------------------------------------------
-- UTF-8 → UTF-16 conversion helper
-----------------------------------------------------------------------
--- Converts a UTF-8 Lua string to a cached UTF-16 wide string.
-- @param str UTF-8 string
-- @return wchar_t* or nil
local function wchar(str)
	if not str then return nil end
	if WSTRING_CACHE[str] then
		return WSTRING_CACHE[str]
	end

	local len = kernel32.MultiByteToWideChar(
		CP_UTF8, 0, str, -1, nil, 0
	)
	if len <= 0 then return nil end

	local buf = ffi.new("wchar_t[?]", len)
	kernel32.MultiByteToWideChar(
		CP_UTF8, 0, str, -1, buf, len
	)

	WSTRING_CACHE[str] = buf
	return buf
end

-----------------------------------------------------------------------
-- Bitmap loader with cache
-----------------------------------------------------------------------
--- Loads a bitmap from disk and caches it.
-- @param path File system path to bitmap
-- @return HBITMAP or nil
local function load_bitmap(path)
	if not path then return nil end
	if BITMAP_CACHE[path] then
		return BITMAP_CACHE[path]
	end

	local wpath = wchar(path)
	if not wpath then return nil end

	local bmp = user32.LoadImageW(
		nil,
		wpath,
		IMAGE_BITMAP,
		16,
		16,
		bit.bor(LR_LOADFROMFILE, LR_CREATEDIBSECTION)
	)

	if bmp ~= nil then
		BITMAP_CACHE[path] = bmp
	end

	return bmp
end

-----------------------------------------------------------------------
-- Menu object
-----------------------------------------------------------------------
local Menu = {}
Menu.__index = Menu

-----------------------------------------------------------------------
-- Adds an item or separator to the menu.
--
-- options:
--   icon      = string (bitmap filepath)
--   separator = boolean
--
-- @param label string
-- @param callback function or nil
-- @param options table or nil
-----------------------------------------------------------------------
function Menu:add(label, callback, options)
	options = options or {}

	-- Separator
	if options.separator or label == "---" then
		local item = ffi.new("MENUITEMINFOW")
		item.cbSize = ffi.sizeof(item)
		item.fMask  = MIIM_FTYPE
		item.fType  = MFT_SEPARATOR

		user32.InsertMenuItemW(
			self.hmenu,
			#self.items,
			true,
			item
		)

		table.insert(self.items, { type = "separator" })
		return self
	end

	local id = NEXT_ID
	NEXT_ID = NEXT_ID + 1

	if callback then
		CALLBACKS[id] = callback
	end

	local item = ffi.new("MENUITEMINFOW")
	item.cbSize = ffi.sizeof(item)
	item.fMask  = bit.bor(MIIM_STRING, MIIM_ID)
	item.wID	= id
	item.dwTypeData = wchar(label)

	if options.icon then
		local bmp = load_bitmap(options.icon)
		if bmp then
			item.fMask = bit.bor(item.fMask, MIIM_BITMAP)
			item.hbmpItem = bmp
		end
	end

	local ok = user32.InsertMenuItemW(
		self.hmenu,
		#self.items,
		true,
		item
	)

	if ok ~= 0 then
		table.insert(self.items, {
			type = "item",
			id = id
		})
	end

	return self
end

-----------------------------------------------------------------------
-- Adds a submenu.
-- @param submenu Menu
-- @param label string
-----------------------------------------------------------------------
function Menu:sub(submenu, label)
	local item = ffi.new("MENUITEMINFOW")
	item.cbSize = ffi.sizeof(item)
	item.fMask  = bit.bor(MIIM_STRING, MIIM_SUBMENU)
	item.dwTypeData = wchar(label)
	item.hSubMenu = submenu.hmenu

	user32.InsertMenuItemW(
		self.hmenu,
		#self.items,
		true,
		item
	)

	table.insert(self.items, {
		type = "submenu",
		submenu = submenu
	})

	return self
end

-----------------------------------------------------------------------
-- Applies modern Windows menu style.
-----------------------------------------------------------------------
function Menu:set_style()
	local info = ffi.new("MENUINFO")
	info.cbSize = ffi.sizeof(info)
	info.fMask  = MIM_STYLE
	info.dwStyle = bit.bor(
		MNS_CHECKORBMP,
		MNS_AUTODISMISS
	)

	user32.SetMenuInfo(self.hmenu, info)
	return self
end

-----------------------------------------------------------------------
-- Displays the menu at the cursor position.
-----------------------------------------------------------------------
function Menu:show()
	self:set_style()

	local pt = ffi.new("POINT")
	user32.GetCursorPos(pt)

	local hwnd = self.hwnd or user32.GetForegroundWindow()
	user32.SetForegroundWindow(hwnd)

	local cmd = user32.TrackPopupMenuEx(
		self.hmenu,
		bit.bor(TPM_RIGHTBUTTON, TPM_RETURNCMD),
		pt.x,
		pt.y,
		hwnd,
		nil
	)

	if cmd ~= 0 and CALLBACKS[cmd] then
		CALLBACKS[cmd]()
	end

	self:cleanup()
end

-----------------------------------------------------------------------
-- Releases menu resources.
-----------------------------------------------------------------------
function Menu:cleanup()
	if self.cleaned then return end
	self.cleaned = true

	for _, item in ipairs(self.items) do
		if item.id then
			CALLBACKS[item.id] = nil
		end
	end

	user32.DestroyMenu(self.hmenu)
	self.items = {}
end

-----------------------------------------------------------------------
-- Menu factory
-----------------------------------------------------------------------
local function create_menu(hwnd)
	return setmetatable({
		hwnd	= hwnd,
		hmenu	= user32.CreatePopupMenu(),
		items	= {},
		cleaned	= false
	}, Menu)
end

-----------------------------------------------------------------------
-- Module table
-----------------------------------------------------------------------
local popup_menu = {}

setmetatable(popup_menu, {
	__call = function(_, hwnd)
		return create_menu(hwnd)
	end
})

-----------------------------------------------------------------------
-- Global cleanup
-----------------------------------------------------------------------
--- Releases all cached bitmaps and callbacks.
function popup_menu.global_cleanup()
	for _, bmp in pairs(BITMAP_CACHE) do
		user32.DeleteObject(bmp)
	end
	BITMAP_CACHE = {}
	CALLBACKS = {}
end

return popup_menu
