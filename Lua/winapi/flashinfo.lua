

local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
/**
 * @typedef LONG
 * @brief Signed 32-bit integer.
 */
typedef long LONG;

/**
 * @typedef LONG_PTR
 * @brief Signed integer type large enough to hold a pointer.
 */
typedef long LONG_PTR;

/**
 * @typedef HWND
 * @brief Handle to a window.
 */
typedef void* HWND;

/**
 * @brief Retrieves information about the specified window.
 * @param hWnd Handle to the window.
 * @param nIndex Index of the value to retrieve.
 * @return The requested value.
 */
LONG_PTR GetWindowLongPtrA(HWND hWnd, int nIndex);

/**
 * @brief Changes an attribute of the specified window.
 * @param hWnd Handle to the window.
 * @param nIndex Index of the value to set.
 * @param dwNewLong New value.
 * @return The previous value.
 */
LONG_PTR SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong);

/**
 * @brief Changes the size, position, and Z order of a window.
 */
bool SetWindowPos(
	HWND hWnd,
	HWND hWndInsertAfter,
	int X,
	int Y,
	int cx,
	int cy,
	unsigned int uFlags
);

/**
 * @typedef UINT
 * @brief Unsigned integer.
 */
typedef unsigned int UINT;

/**
 * @typedef DWORD
 * @brief Unsigned 32-bit integer.
 */
typedef unsigned long DWORD;

/**
 * @typedef BOOL
 * @brief Boolean value.
 */
typedef int BOOL;

/**
 * @struct FLASHWINFO
 * @brief Contains the flash status for a window.
 */
typedef struct {
	UINT	cbSize;		/**< Size of the structure */
	HWND	hwnd;		/**< Handle to the window */
	DWORD	dwFlags;	/**< Flash status */
	UINT	uCount;		/**< Number of flashes */
	DWORD	dwTimeout;	/**< Flash rate */
} FLASHWINFO;

/**
 * @brief Flashes the specified window.
 * @param pfwi Pointer to FLASHWINFO structure.
 * @return Nonzero if successful.
 */
BOOL FlashWindowEx(FLASHWINFO* pfwi);
]]

local user32 = ffi.load("user32")

----------------------------------------------------------------
-- WinAPI constants
----------------------------------------------------------------

local GWL_EXSTYLE			= -20

local WS_EX_TOOLWINDOW		= 0x00000080
local WS_EX_APPWINDOW		= 0x00040000

local FLASHW_STOP			= 0x00000000
local FLASHW_TRAY			= 0x00000002

----------------------------------------------------------------
-- Shared FLASHWINFO instance
----------------------------------------------------------------

local fwi = ffi.new("FLASHWINFO")
fwi.cbSize	= ffi.sizeof(fwi)
fwi.dwFlags	= FLASHW_TRAY

----------------------------------------------------------------
-- Internal helpers
----------------------------------------------------------------


--- Adds WS_EX_APPWINDOW and removes WS_EX_TOOLWINDOW.
-- @param hwnd Target window handle.
--
local function applyAppWindowStyle(hwnd)
	local exStyle = user32.GetWindowLongPtrA(hwnd, GWL_EXSTYLE)
	exStyle = bit.band(exStyle, bit.bnot(WS_EX_TOOLWINDOW))
	exStyle = bit.bor(exStyle, WS_EX_APPWINDOW)
	user32.SetWindowLongPtrA(hwnd, GWL_EXSTYLE, exStyle)
end

--- Restores WS_EX_TOOLWINDOW and removes WS_EX_APPWINDOW.
-- @param hwnd Target window handle.
--
local function restoreToolWindowStyle(hwnd)
	local exStyle = user32.GetWindowLongPtrA(hwnd, GWL_EXSTYLE)
	exStyle = bit.band(exStyle, bit.bnot(WS_EX_APPWINDOW))
	exStyle = bit.bor(exStyle, WS_EX_TOOLWINDOW)
	user32.SetWindowLongPtrA(hwnd, GWL_EXSTYLE, exStyle)
end

----------------------------------------------------------------
-- Public API
----------------------------------------------------------------

--- Flashes the window taskbar icon.
--
-- This function:
--   Removes WS_EX_TOOLWINDOW
--   Adds WS_EX_APPWINDOW
--   Requests a taskbar flash
--
-- @param hwnd Window handle.
-- @param count Number of flashes (default: 999).
-- @param timeout Flash timeout in milliseconds (default: system).
--
local function flash(hwnd, count, timeout)
	applyAppWindowStyle(hwnd)

	fwi.hwnd		= hwnd
	fwi.uCount		= 999
	fwi.dwTimeout	=  0

	user32.FlashWindowEx(fwi)
end

-- @brief Stops flashing and restores TOOLWINDOW behavior.
--
-- This function:
-- - Stops any active flashing
-- - Restores WS_EX_TOOLWINDOW
-- - Removes WS_EX_APPWINDOW
-- 
-- @param hwnd Window handle.
-- 
local function remove(hwnd)
	fwi.hwnd	= hwnd
	fwi.dwFlags	= FLASHW_STOP
	fwi.uCount	= 0
	fwi.dwTimeout = 0

	user32.FlashWindowEx(fwi)
	restoreToolWindowStyle(hwnd)
end

----------------------------------------------------------------
-- Module exports
----------------------------------------------------------------

return {
	flash	= flash,
	remove	= remove
}





