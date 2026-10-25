--- Window effects for Windows (LuaJIT FFI).
--
-- Applies visual effects to a native window handle (HWND): Mica, Mica Alt,
-- Acrylic, Blur, transparent/solid accent, dark title bar, rounded corners,
-- shadow and border color.
--
-- Win32 functions are called through the LuaJIT FFI (`user32`, `dwmapi`,
-- `ntdll` and `advapi32`). Features are gated by the Windows build number:
--
--   - Mica (`DWMWA_SYSTEMBACKDROP_TYPE`): build 22621+ (Windows 11 22H2).
--   - Mica (legacy `DWMWA_MICA_EFFECT`):   build 22000+ (Windows 11 21H2).
--   - Acrylic:                             build 17134+ (Windows 10 1803).
--   - Corners and border color:            build 22000+ (Windows 11).
--
-- When Mica is requested on a build that does not support it, the module
-- falls back to Acrylic (build 17134+) or Blur.
--
-- @module glass
-- @author F4Jonatas
-- @release 1.2.1
-- @license GPL v2.0 License


local ffi = require( 'ffi' )

ffi.cdef[[
	typedef void*         HWND;
	typedef void*         HKEY;
	typedef int           BOOL;
	typedef unsigned long DWORD;
	typedef long          HRESULT;
	typedef long          LONG;

	typedef struct {
		DWORD          dwOSVersionInfoSize;
		DWORD          dwMajorVersion;
		DWORD          dwMinorVersion;
		DWORD          dwBuildNumber;
		DWORD          dwPlatformId;
		unsigned short szCSDVersion[128]; // WCHAR[128]
	} RTL_OSVERSIONINFOW;

	typedef struct {
		int   AccentState;
		int   AccentFlags;
		DWORD GradientColor;
		int   AnimationId;
	} ACCENT_POLICY;

	typedef struct {
		int    Attribute;
		void*  Data;
		size_t SizeOfData; // SIZE_T
	} WINDOWCOMPOSITIONATTRIBDATA;

	typedef struct {
		int cxLeftWidth;
		int cxRightWidth;
		int cyTopHeight;
		int cyBottomHeight;
	} MARGINS;

	BOOL SetWindowCompositionAttribute( HWND, WINDOWCOMPOSITIONATTRIBDATA* );

	HRESULT DwmSetWindowAttribute(
		HWND        hwnd,
		DWORD       dwAttribute,
		const void* pvAttribute,
		DWORD       cbAttribute
	);

	HRESULT DwmExtendFrameIntoClientArea( HWND, const MARGINS* );

	LONG RtlGetVersion( RTL_OSVERSIONINFOW* );

	LONG RegOpenKeyExA( HKEY, const char*, DWORD, DWORD, HKEY* );
	LONG RegQueryValueExA( HKEY, const char*, DWORD*, DWORD*, void*, DWORD* );
	LONG RegCloseKey( HKEY );
]]


local user32   = ffi.load( 'user32' )
local dwmapi   = ffi.load( 'dwmapi' )
local ntdll    = ffi.load( 'ntdll' )
local advapi32 = ffi.load( 'advapi32' )


--- Window composition attribute IDs (`SetWindowCompositionAttribute`).
-- @section WCA

--- Attribute ID for the accent policy.
local WCA_ACCENT_POLICY = 19


--- Accent state values.
-- @section AccentState

local ACCENT_DISABLED                   = 0
local ACCENT_ENABLE_GRADIENT            = 1
local ACCENT_ENABLE_TRANSPARENTGRADIENT = 2
local ACCENT_ENABLE_BLURBEHIND          = 3
local ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4


--- Accent flags.
-- @section AccentFlags

--- Draw borders on all four sides of the window.
local FLAG_ALLBORDERS = 0x1E0


--- DWM window attribute IDs.
-- @section DWMWA

local DWMWA_NCRENDERING_POLICY       = 2
local DWMWA_USE_IMMERSIVE_DARK_MODE  = 20
local DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19 -- Pre-20H1 builds.
local DWMWA_WINDOW_CORNER_PREFERENCE = 33
local DWMWA_BORDER_COLOR             = 34
local DWMWA_SYSTEMBACKDROP_TYPE      = 38    -- Build 22621+.
local DWMWA_MICA_EFFECT              = 1029  -- Build 22000-22620 (undocumented).


--- Special DWM color values.
-- @section DWMColor

local DWMWA_COLOR_DEFAULT = 0xFFFFFFFF -- Restore the system default border color.
local DWMWA_COLOR_NONE    = 0xFFFFFFFE -- Remove the border entirely.


--- Corner preference values.
-- @section DWMWCP

local DWMWCP_DEFAULT    = 0
local DWMWCP_DONOTROUND = 1
local DWMWCP_ROUND      = 2
local DWMWCP_ROUNDSMALL = 3


--- Non-client rendering policy values.
-- @section DWMNCRP

local DWMNCRP_DISABLED = 1
local DWMNCRP_ENABLED  = 2


--- System backdrop type values.
-- @section DWMSBT

local DWMSBT_NONE     = 1
local DWMSBT_MICA     = 2
local DWMSBT_MICA_ALT = 4


--- Minimum Windows build numbers for each feature.
-- @section Builds

local BUILD_ACRYLIC  = 17134
local BUILD_WIN11    = 22000
local BUILD_BACKDROP = 22621


--- Converts a `0xRRGGBB` color to the `0x00BBGGRR` layout used by DWM and GDI.
-- @param number rgb Color as `0xRRGGBB`.
-- @return number Color as `0x00BBGGRR`.
local function rgbToBgr( rgb )
	local r = bit.band( bit.rshift( rgb, 16 ), 0xFF )
	local g = bit.band( bit.rshift( rgb, 8 ),  0xFF )
	local b = bit.band( rgb, 0xFF )
	return bit.bor( bit.lshift( b, 16 ), bit.lshift( g, 8 ), r )
end


--- Builds the `GradientColor` value of an accent policy.
-- The accent API expects `0xAABBGGRR`, so the RGB input is swapped to BGR.
-- @param number alpha Alpha value (0-255).
-- @param number rgb Color as `0xRRGGBB`.
-- @return number Color as `0xAABBGGRR`.
local function abgr( alpha, rgb )
	return bit.bor( bit.lshift( alpha, 24 ), rgbToBgr( rgb ))
end


--- Retrieves the Windows version using `RtlGetVersion`.
-- Unlike `GetVersionEx`, it is not affected by application manifests.
-- @return table Table with the numeric fields `major`, `minor` and `build`.
-- @raise Asserts if `RtlGetVersion` fails.
local function getWindowsVersion()
	local info = ffi.new( 'RTL_OSVERSIONINFOW' )
	info.dwOSVersionInfoSize = ffi.sizeof( info )

	assert( ntdll.RtlGetVersion( info ) == 0, 'RtlGetVersion failed' )

	return {
		major = tonumber( info.dwMajorVersion ),
		minor = tonumber( info.dwMinorVersion ),
		build = tonumber( info.dwBuildNumber )
	}
end


--- Sets an accent policy on a window.
-- @param HWND hwnd Handle to the target window.
-- @param cdata accent A one-element `ACCENT_POLICY[1]` array.
local function setAccent( hwnd, accent )
	local data = ffi.new( 'WINDOWCOMPOSITIONATTRIBDATA' )
	data.Attribute  = WCA_ACCENT_POLICY
	data.Data       = accent
	data.SizeOfData = ffi.sizeof( 'ACCENT_POLICY' )

	user32.SetWindowCompositionAttribute( hwnd, data )
end


--- Resets all effects of a window to their default state.
-- Disables the accent policy, the system backdrop and the legacy Mica flag,
-- restores the default corners and border color, disables dark mode, removes
-- the extended frame and re-enables non-client rendering.
-- @param HWND hwnd Handle to the target window.
local function resetAll( hwnd )
	-- Accent off
	local accent = ffi.new( 'ACCENT_POLICY[1]' )
	accent[0].AccentState = ACCENT_DISABLED
	setAccent( hwnd, accent )

	-- Backdrop off (both the current and the legacy attribute)
	local backdrop = ffi.new( 'int[1]', DWMSBT_NONE )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, backdrop, ffi.sizeof( backdrop ))

	local mica = ffi.new( 'int[1]', 0 )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_MICA_EFFECT, mica, ffi.sizeof( mica ))

	-- Frame extension off
	local margins = ffi.new( 'MARGINS' )
	dwmapi.DwmExtendFrameIntoClientArea( hwnd, margins )

	-- Corners default
	local corners = ffi.new( 'int[1]', DWMWCP_DEFAULT )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, corners, ffi.sizeof( corners ))

	-- Dark mode off (both known attribute IDs for compatibility)
	local dark = ffi.new( 'int[1]', 0 )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, dark, ffi.sizeof( dark ))
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, dark, ffi.sizeof( dark ))

	-- Border color default
	local borderColor = ffi.new( 'DWORD[1]', DWMWA_COLOR_DEFAULT )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_BORDER_COLOR, borderColor, ffi.sizeof( borderColor ))

	-- Non-client rendering enabled
	local nc = ffi.new( 'int[1]', DWMNCRP_ENABLED )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_NCRENDERING_POLICY, nc, ffi.sizeof( nc ))
end


--- Applies the requested visual effects to a window.
--
-- If `mica` or `mica_alt` is requested on a build older than 22000, the effect
-- falls back to `acrylic` (build 17134+) or `blur`. On builds 22000-22620 only
-- the legacy Mica effect is available, so `mica_alt` behaves like `mica`.
--
-- @param HWND hwnd Handle to the target window.
-- @param table winver Version table returned by `getWindowsVersion`.
-- @param[opt] table opt Options table.
-- @param[opt] string opt.effect One of `"mica"`, `"mica_alt"`, `"blur"`,
--   `"acrylic"`, `"transparent"` or `"solid"`.
-- @param[opt] table opt.effect_opts Extra options for `acrylic` and `solid`
--   (ignored by the other effects).
-- @param[opt] number opt.effect_opts.opacity Opacity (0-255).
-- @param[opt] number opt.effect_opts.color Color as `0xRRGGBB`.
-- @param[opt] string opt.corners One of `"round"`, `"small"`, `"none"` or
--   `"default"`. Requires Windows 11.
-- @param[opt] boolean opt.dark Enables or disables the dark title bar.
-- @param[opt] boolean opt.shadow Enables or disables non-client rendering
--   (which includes the window shadow).
-- @param[opt] boolean|number opt.border `false` removes the border, `true`
--   restores the default, a `0xRRGGBB` number sets a custom color. Requires
--   Windows 11 (build 22000+).
-- @raise Asserts if `acrylic` is requested explicitly on builds older than 17134.
local function applyEffects( hwnd, winver, opt )
	opt = opt or {}

	local build  = winver.build
	local effect = opt.effect

	-- Fall back when Mica is not available
	if ( effect == 'mica' or effect == 'mica_alt' ) and build < BUILD_WIN11 then
		effect = build >= BUILD_ACRYLIC and 'acrylic' or 'blur'
	end

	-- Mica
	if effect == 'mica' or effect == 'mica_alt' then
		if build >= BUILD_BACKDROP then
			local v = ffi.new( 'int[1]', effect == 'mica' and DWMSBT_MICA or DWMSBT_MICA_ALT )
			dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, v, ffi.sizeof( v ))
		else
			local v = ffi.new( 'int[1]', 1 )
			dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_MICA_EFFECT, v, ffi.sizeof( v ))
		end

		-- The backdrop is only visible when the frame covers the client area
		local margins = ffi.new( 'MARGINS', -1, -1, -1, -1 )
		dwmapi.DwmExtendFrameIntoClientArea( hwnd, margins )

	-- Accent
	elseif effect then
		local accent = ffi.new( 'ACCENT_POLICY[1]' )
		accent[0].AccentFlags = FLAG_ALLBORDERS

		local eo = opt.effect_opts or {}

		if effect == 'blur' then
			accent[0].AccentState = ACCENT_ENABLE_BLURBEHIND

		elseif effect == 'acrylic' then
			assert( build >= BUILD_ACRYLIC, 'Acrylic requires Windows 10 1803+' )
			accent[0].AccentState   = ACCENT_ENABLE_ACRYLICBLURBEHIND
			accent[0].GradientColor = abgr( eo.opacity or 0x0, eo.color or 0x0 )

		elseif effect == 'transparent' then
			accent[0].AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT

		elseif effect == 'solid' then
			accent[0].AccentState   = ACCENT_ENABLE_GRADIENT
			accent[0].GradientColor = abgr( eo.opacity or 0xFF, eo.color or 0x000000 )
		end

		setAccent( hwnd, accent )
	end

	-- Corners
	if opt.corners then
		local v = ffi.new( 'int[1]',
			opt.corners == 'round' and DWMWCP_ROUND      or
			opt.corners == 'small' and DWMWCP_ROUNDSMALL or
			opt.corners == 'none'  and DWMWCP_DONOTROUND or
			DWMWCP_DEFAULT
		)

		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, v, ffi.sizeof( v ))
	end

	-- Dark mode
	if opt.dark ~= nil then
		local v = ffi.new( 'int[1]', opt.dark and 1 or 0 )
		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, v, ffi.sizeof( v ))
		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, v, ffi.sizeof( v ))
	end

	-- Shadow
	if opt.shadow ~= nil then
		local v = ffi.new( 'int[1]', opt.shadow and DWMNCRP_ENABLED or DWMNCRP_DISABLED )
		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_NCRENDERING_POLICY, v, ffi.sizeof( v ))
	end

	-- Border (requires Windows 11, build 22000+)
	if opt.border ~= nil and build >= BUILD_WIN11 then
		local color

		if opt.border == false then
			color = DWMWA_COLOR_NONE
		elseif opt.border == true then
			color = DWMWA_COLOR_DEFAULT
		else
			-- Custom 0xRRGGBB color; DWM expects BGR
			color = rgbToBgr( opt.border )
		end

		local v = ffi.new( 'DWORD[1]', color )
		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_BORDER_COLOR, v, ffi.sizeof( v ))
	end
end




local M = {}



--- Checks whether Windows is using the dark app theme.
-- Reads the `AppsUseLightTheme` value from `HKCU` in the registry. The key
-- and value names are plain ASCII, so the ANSI registry functions are safe.
--
-- @return (boolean|nil) `true` for dark mode, `false` for light mode.
-- @return[2] nil If the registry value could not be read.
M.isDark = function()
	-- HKEY_CURRENT_USER is 0x80000001; it must be sign-extended on 64-bit
	local HKEY_CURRENT_USER = ffi.cast( 'HKEY', ffi.cast( 'intptr_t', -0x7FFFFFFF ))
	local KEY_READ = 0x20019

	local hkey = ffi.new( 'HKEY[1]' )
	local result = advapi32.RegOpenKeyExA(
		HKEY_CURRENT_USER,
		'Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize',
		0,
		KEY_READ,
		hkey
	)

	if result ~= 0 then return nil end

	local value = ffi.new( 'DWORD[1]' )
	local value_size = ffi.new( 'DWORD[1]', ffi.sizeof( 'DWORD' ))
	result = advapi32.RegQueryValueExA(
		hkey[0],
		'AppsUseLightTheme',
		nil,
		nil,
		value,
		value_size
	)

	advapi32.RegCloseKey( hkey[0] )

	if result ~= 0 then return nil end

	return value[0] == 0
end


--- Applies visual effects to a window.
-- Resets any existing effects first, then applies the requested options.
-- Each call replaces the previous state; options are not cumulative.
--
-- @function glass
-- @param HWND hwnd Handle to the target window (must be a valid HWND).
-- @param[opt] table options Configuration table. See `applyEffects` for the
--   available fields.
-- @raise Asserts if `hwnd` is nil or NULL.
--
-- @usage
-- local glass = require( 'glass' )
-- glass( rain.hwnd, {
--   effect  = 'mica',
--   corners = 'round',
--   dark    = true,
--   shadow  = true,
--   border  = false, -- remove border
-- })
return setmetatable( M, {
	__call = function( _, hwnd, options )
		assert( hwnd ~= nil and hwnd ~= ffi.NULL, 'Invalid HWND' )

		resetAll( hwnd )

		applyEffects( hwnd, getWindowsVersion(), options )
	end
})
