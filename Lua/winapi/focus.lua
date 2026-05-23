local ffi = require("ffi")

ffi.cdef[[
	typedef void* HWINEVENTHOOK;
	typedef void* HWND;

	typedef void (__stdcall *WINEVENTPROC)(
		HWINEVENTHOOK hWinEventHook,
		unsigned long event,
		HWND hwnd,
		long idObject,
		long idChild,
		unsigned long idEventThread,
		unsigned long dwmsEventTime
	);

	HWINEVENTHOOK SetWinEventHook(
		unsigned long eventMin,
		unsigned long eventMax,
		void* hmodWinEventProc,
		WINEVENTPROC pfnWinEventProc,
		unsigned long idProcess,
		unsigned long idThread,
		unsigned long dwFlags
	);
]]



local EVENT_SYSTEM_FOREGROUND = 0x0003
local WINEVENT_OUTOFCONTEXT = 0x0000

-- 🔒 estado global do hook
local hook_handle
local hook_proc

-- 📦 registry multi-window
local watched = {}   -- [HWND] = callback
local focus_state = {} -- [HWND] = bool

-- converte HWND → chave estável
local function hwnd_key(hwnd)
	return tonumber(ffi.cast("intptr_t", hwnd))
end

-- callback global único
hook_proc = ffi.cast( 'WINEVENTPROC', function( hook, event, event_hwnd )
	if event ~= EVENT_SYSTEM_FOREGROUND then return end

	local fg_key = hwnd_key( event_hwnd )
	-- verifica cada janela registrada
	for key, cb in pairs( watched ) do
		local prev = focus_state[key]
		local has_focus = (key == fg_key)

		if prev ~= has_focus then
			focus_state[key] = has_focus
			cb(has_focus, event_hwnd)
		end
	end
end)


-- instala hook UMA vez
hook_handle = ffi.C.SetWinEventHook(
	EVENT_SYSTEM_FOREGROUND,
	EVENT_SYSTEM_FOREGROUND,
	nil,
	hook_proc,
	0,
	0,
	WINEVENT_OUTOFCONTEXT
)


return function(hwnd, callback)
	local key = hwnd_key(hwnd)
	watched[key] = callback
	focus_state[key] = false
end

-- Crash ao reiniciar a skin, porque o hook não está sendo removido corretamente
-- local focus = require( 'winapi.focus' )
-- focus( rain.hwnd,function(focus)
--     print("janela 1:", focus)
-- end)