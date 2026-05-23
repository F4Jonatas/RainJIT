--- monitor.lua
-- Windows monitor utilities using LuaJIT FFI.
--
-- Features:
-- - Enumerate monitors with stable index ordering
-- - Query monitor from HWND
-- - Retrieve window rectangle
-- - Center window on its monitor
--
-- Uses Win32 API via LuaJIT FFI.
--
-- @module monitor

local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
typedef void* HWND;
typedef void* HMONITOR;
typedef void* HDC;
typedef int BOOL;
typedef unsigned long DWORD;
typedef long LPARAM;

typedef struct {
	long left;
	long top;
	long right;
	long bottom;
} RECT;

typedef struct {
	DWORD cbSize;
	RECT rcMonitor;
	RECT rcWork;
	DWORD dwFlags;
} MONITORINFO;

typedef BOOL (__stdcall *MONITORENUMPROC)(
	HMONITOR,
	HDC,
	RECT*,
	LPARAM
);

HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags);
BOOL GetMonitorInfoA(HMONITOR hMonitor, MONITORINFO* lpmi);

BOOL EnumDisplayMonitors(
	HDC hdc,
	RECT* lprcClip,
	MONITORENUMPROC lpfnEnum,
	LPARAM dwData
);

BOOL GetWindowRect(HWND hWnd, RECT* lpRect);
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter,
	int X, int Y, int cx, int cy, unsigned int uFlags);
]]

local user32 = ffi.load("user32")

local MONITOR_DEFAULTTONEAREST = 2
local MONITORINFOF_PRIMARY = 1

local SWP_NOZORDER = 0x0004
local SWP_NOACTIVATE = 0x0010

local monitorIndexMap = {}
local monitorList = {}

local M = {}

---------------------------------------------------------------------
-- Helpers
---------------------------------------------------------------------

local function rectToTable(r)
	return {
		left = r.left,
		top = r.top,
		right = r.right,
		bottom = r.bottom,
		width = r.right - r.left,
		height = r.bottom - r.top
	}
end

local function getMonitorInfoRaw(hMonitor)
	local mi = ffi.new("MONITORINFO")
	mi.cbSize = ffi.sizeof(mi)

	if user32.GetMonitorInfoA(hMonitor, mi) == 0 then
		return nil
	end

	return {
		handle = hMonitor,
		monitor = rectToTable(mi.rcMonitor),
		work = rectToTable(mi.rcWork),
		primary = bit.band(mi.dwFlags, MONITORINFOF_PRIMARY) ~= 0
	}
end

local function rebuildIndex()
	monitorIndexMap = {}

	-- sort: left -> top
	table.sort(monitorList, function(a, b)
		if a.monitor.left == b.monitor.left then
			return a.monitor.top < b.monitor.top
		end
		return a.monitor.left < b.monitor.left
	end)

	for i, m in ipairs(monitorList) do
		m.index = i
		local key = tonumber(ffi.cast("intptr_t", m.handle))
		monitorIndexMap[key] = i
	end
end

---------------------------------------------------------------------
--- Enumerate all monitors.
--
-- Returns monitors ordered from left to right, then top to bottom.
--
-- @return table List of monitors
-- @return[1].index Stable monitor index
-- @return[1].monitor Bounds (x, y, width, height)
-- @return[1].work Work area
-- @return[1].primary boolean
--
function M.getAll()
	monitorList = {}

	local callback
	callback = ffi.cast("MONITORENUMPROC", function(hMonitor, hdc, rect, lparam)
		local info = getMonitorInfoRaw(hMonitor)
		if info then
			table.insert(monitorList, info)
		end
		return 1
	end)

	user32.EnumDisplayMonitors(nil, nil, callback, 0)
	callback:free()

	rebuildIndex()

	return monitorList
end

---------------------------------------------------------------------
--- Get monitor from window.
--
-- @param hwnd HWND
-- @return table Monitor info including window rect
--
function M.getWindow(hwnd)
	hwnd = ffi.cast("HWND", hwnd)

	if not next(monitorIndexMap) then
		M.getAll()
	end

	local hMonitor = user32.MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
	if hMonitor == nil then
		return nil
	end

	local info = getMonitorInfoRaw(hMonitor)
	if not info then
		return nil
	end

	local key = tonumber(ffi.cast("intptr_t", hMonitor))
	info.index = monitorIndexMap[key]

	local rect = ffi.new("RECT")
	if user32.GetWindowRect(hwnd, rect) ~= 0 then
		info.window = rectToTable(rect)
	end

	return info
end

---------------------------------------------------------------------
--- Center a window on its current monitor.
--
-- @param hwnd HWND
-- @return boolean success
--
-- @usage
-- monitor.centerWindow(hwnd)
--
function M.centerWindow(hwnd)
	local info = M.getWindow(hwnd)
	if not info or not info.window then
		return false
	end

	local m = info.monitor
	local w = info.window

	local cx = m.left + (m.width - w.width) / 2
	local cy = m.top + (m.height - w.height) / 2

	user32.SetWindowPos(
		hwnd,
		nil,
		cx,
		cy,
		0,
		0,
		bit.bor(SWP_NOZORDER, SWP_NOACTIVATE)
	)

	return true
end

---------------------------------------------------------------------
--- Callable shortcut.
--
-- @usage
-- local monitors = monitor()
--
setmetatable(M, {
	__call = function()
		return M.getAll()
	end
})

return M