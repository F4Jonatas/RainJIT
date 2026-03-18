--- Window effects module for Windows.
-- Applies visual effects (Mica, Acrylic, Blur, Dark mode, Rounded corners, Shadow) to a window handle (HWND).
-- Uses FFI to call Windows API functions from user32, dwmapi, and ntdll.
-- Compatible with Windows 10/11, with version checks for specific features.
-- @module glass

local ffi = require( 'ffi' )

ffi.cdef[[
	typedef void* HWND;
	typedef int BOOL;
	typedef unsigned long DWORD;
	typedef long HRESULT;
	typedef long LONG;

	typedef struct {
		DWORD dwOSVersionInfoSize;
		DWORD dwMajorVersion;
		DWORD dwMinorVersion;
		DWORD dwBuildNumber;
		DWORD dwPlatformId;
		char  szCSDVersion[128];
	} RTL_OSVERSIONINFOW;

	typedef struct {
		int AccentState;
		int AccentFlags;
		DWORD GradientColor;
		int AnimationId;
	} ACCENT_POLICY;

	typedef struct {
		int Attribute;
		void* Data;
		int SizeOfData;
	} WINDOWCOMPOSITIONATTRIBDATA;

	BOOL SetWindowCompositionAttribute(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

	HRESULT DwmSetWindowAttribute(
		HWND hwnd,
		DWORD dwAttribute,
		const void* pvAttribute,
		DWORD cbAttribute
	);

	LONG RtlGetVersion(RTL_OSVERSIONINFOW*);
]]




local user32 = ffi.load( 'user32' )
local dwmapi = ffi.load( 'dwmapi' )
local ntdll  = ffi.load( 'ntdll' )

--- Attribute IDs for SetWindowCompositionAttribute.
-- @section WCA
local WCA_ACCENT_POLICY = 19

--- Accent state values.
-- @section AccentState
local ACCENT_DISABLED                   = 0
local ACCENT_ENABLE_GRADIENT            = 1
local ACCENT_ENABLE_TRANSPARENTGRADIENT = 2
local ACCENT_ENABLE_BLURBEHIND          = 3
local ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4

--- Accent flags: enables borders on all sides.
-- @section AccentFlags
local FLAG_ALLBORDERS = 0x1E0

--- DWM window attribute IDs.
-- @section DWMWA
local DWMWA_NCRENDERING_POLICY       = 2
local DWMWA_USE_IMMERSIVE_DARK_MODE  = 20
local DWMWA_WINDOW_CORNER_PREFERENCE = 33
local DWMWA_SYSTEMBACKDROP_TYPE      = 38

--- Corner preference values.
-- @section DWMWCP
local DWMWCP_DEFAULT    = 0
local DWMWCP_DONOTROUND = 1
local DWMWCP_ROUND      = 2
local DWMWCP_ROUNDSMALL = 3

--- Non-client rendering policy values.
-- @section DWMNCRP
local DWMNCRP_ENABLED  = 2
local DWMNCRP_DISABLED = 1

--- System backdrop type values.
-- @section DWMSBT
local DWMSBT_NONE     = 1
local DWMSBT_MICA     = 2
local DWMSBT_MICA_ALT = 4


--- Converts separate alpha and RGB values to a combined ARGB integer.
-- @tparam number alpha Alpha value (0-255).
-- @tparam number rgb RGB value (0xRRGGBB).
-- @treturn number ARGB integer suitable for GradientColor field.
local function argb( alpha, rgb )
	return bit.bor( bit.lshift( alpha, 24 ), rgb )
end




--- Retrieves the Windows version information using RtlGetVersion.
-- @treturn table Table with fields `major`, `minor`, `build` (all numbers).
-- @raise Asserts if RtlGetVersion fails.
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




--- Resets all effects on a window to their default/disabled state.
-- Disables accent, system backdrop, restores default corners, disables dark mode,
-- and re-enables non-client rendering.
-- @tparam HWND hwnd Handle to the target window.
local function resetAll( hwnd )
	-- Accent off
	local accent = ffi.new( 'ACCENT_POLICY' )
	accent.AccentState = ACCENT_DISABLED

	local data = ffi.new( 'WINDOWCOMPOSITIONATTRIBDATA' )
	data.Attribute = WCA_ACCENT_POLICY
	data.Data = accent
	data.SizeOfData = ffi.sizeof( accent )

	user32.SetWindowCompositionAttribute( hwnd, data )

	-- Backdrop off
	local backdrop = ffi.new( 'int[1]', DWMSBT_NONE )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, backdrop, ffi.sizeof( backdrop ))

	-- Corners default
	local corners = ffi.new( 'int[1]', DWMWCP_DEFAULT )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, corners, ffi.sizeof( corners ))

	-- Dark mode off (both known attributes)
	local dark = ffi.new( 'int[1]', 0 )
	dwmapi.DwmSetWindowAttribute( hwnd, 20, dark, ffi.sizeof( dark ))
	dwmapi.DwmSetWindowAttribute( hwnd, 19, dark, ffi.sizeof( dark ))

	-- NC rendering enabled
	local nc = ffi.new( 'int[1]', DWMNCRP_ENABLED )
	dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_NCRENDERING_POLICY, nc, ffi.sizeof( nc ))
end




--- Applies the requested visual effects to a window.
-- @tparam HWND hwnd Handle to the target window.
-- @tparam table winver Table with Windows version info (from getWindowsVersion()).
-- @tparam[opt] table opt Options table.
-- @tparam string opt.effect Effect type: "mica", "mica_alt", "blur", "acrylic", "transparent", "solid".
-- @tparam table opt.effect_opts Additional options for certain effects:
--   @tparam number opt.effect_opts.opacity Opacity (0-255) for acrylic or solid.
--   @tparam number opt.effect_opts.color RGB color (0xRRGGBB) for acrylic or solid.
-- @tparam string opt.corners Corner style: "round", "small", "none", "default".
-- @tparam boolean opt.dark Enable/disable dark mode.
-- @tparam boolean opt.shadow Enable/disable window shadow.
-- @raise Asserts if acrylic is used on Windows builds < 17134.
local function applyEffects( hwnd, winver, opt )
	opt = opt or {}

		-- Mica
	if ( opt.effect == 'mica' or opt.effect == 'mica_alt' ) and winver.build >= 22000 then
		local v = ffi.new( 'int[1]',
			opt.effect == 'mica' and DWMSBT_MICA or
			opt.effect == 'mica_alt' and DWMSBT_MICA_ALT or
			DWMSBT_NONE
		)

		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_SYSTEMBACKDROP_TYPE, v, ffi.sizeof( v ))


	-- Accent
	elseif opt.effect then
		local accent = ffi.new( 'ACCENT_POLICY' )
		accent.AccentFlags = FLAG_ALLBORDERS

		if opt.effect == 'blur' then
			accent.AccentState = ACCENT_ENABLE_BLURBEHIND

		elseif opt.effect == 'acrylic' then
			assert( winver.build >= 17134, 'Acrylic requires Windows 10 1803+' )
			accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND
			accent.GradientColor = argb(
				opt.effect_opts and opt.effect_opts.opacity or 0x0,
				opt.effect_opts and opt.effect_opts.color or 0x0
			)

		elseif opt.effect == 'transparent' then
			accent.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT

		elseif opt.effect == 'solid' then
			accent.AccentState = ACCENT_ENABLE_GRADIENT
			accent.GradientColor = argb(
				opt.effect_opts and opt.effect_opts.opacity or 0xFF,
				opt.effect_opts and opt.effect_opts.color or 0x000000
			)
		end

		local data = ffi.new( 'WINDOWCOMPOSITIONATTRIBDATA' )
		data.Attribute = WCA_ACCENT_POLICY
		data.Data = accent
		data.SizeOfData = ffi.sizeof( accent )

		user32.SetWindowCompositionAttribute( hwnd, data )
	end

	-- Corners
	if opt.corners then
		local v = ffi.new( 'int[1]',
			opt.corners == 'round' and DWMWCP_ROUND or
			opt.corners == 'small' and DWMWCP_ROUNDSMALL or
			opt.corners == 'none' and DWMWCP_DONOTROUND or
			DWMWCP_DEFAULT
		)

		dwmapi.DwmSetWindowAttribute( hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, v, ffi.sizeof( v ))
	end

	-- Dark mode
	if opt.dark ~= nil then
		local v = ffi.new( 'int[1]', opt.dark and 1 or 0)
		dwmapi.DwmSetWindowAttribute( hwnd, 20, v, ffi.sizeof( v ))
		dwmapi.DwmSetWindowAttribute( hwnd, 19, v, ffi.sizeof( v ))
	end

	-- Shadow
	if opt.shadow ~= nil then
		local v = ffi.new( 'int[1]', opt.shadow and DWMNCRP_ENABLED or DWMNCRP_DISABLED)
		dwmapi.DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, v, ffi.sizeof( v ))
	end
end



--- Applies visual effects to a window.
-- Resets any existing effects first, then applies the requested options.
--
-- @tparam HWND hwnd Handle to the target window (must be a valid HWND).
-- @tparam[opt] table options Configuration table. See @{applyEffects} for details.
-- @raise Asserts if hwnd is nil or NULL.
--
-- @usage
-- local glass = require( 'glass' )
-- glass( rain.hwnd, {
--   effect = 'mica',
--   corners = 'round',
--   dark = true,
--   shadow = true
-- })
return function( hwnd, options )
	assert( hwnd ~= nil and hwnd ~= ffi.NULL, 'Invalid HWND' )

	resetAll( hwnd )

	applyEffects(
		hwnd,
		getWindowsVersion(),
		options
	)
end
