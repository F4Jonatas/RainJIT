--- Windows Explorer Context Menu integration for Rainmeter using LuaJIT FFI.
-- This script invokes the native Windows Explorer context menu for a given
-- filesystem path. It uses low-level COM interfaces (IShellFolder, IContextMenu)
-- and is designed to operate safely within Rainmeter constraints.
--
-- IMPORTANT:
--   This code intentionally initializes and uninitializes COM per invocation.
--   This is required to avoid Shell extension instability in non-Explorer hosts.
--   Rainmeter is NOT a full Shell host; certain behaviors are expected.

-- @module menu.contextmenu
-- @release 0.2.1
-- @author F4Jonatas
-- @license GPL v2.0 License

require( 'string.utf8' )

local ffi      = require( 'ffi' )
local ole32    = ffi.load( 'ole32' )
local shell32  = ffi.load( 'shell32' )
local user32   = ffi.load( 'user32' )


ffi.cdef[[
	typedef long HRESULT;
	typedef unsigned long ULONG;
	typedef void* HWND;
	typedef void* HMENU;
	typedef void* HINSTANCE;
	typedef void* LPCITEMIDLIST;
	typedef void* LPITEMIDLIST;
	typedef void* PCIDLIST_ABSOLUTE;
	typedef void* PCUITEMID_CHILD;
	typedef unsigned long DWORD;
	typedef void* PIDLIST_ABSOLUTE;

	typedef struct {
		long x;
		long y;
	} POINT;

	typedef struct {
		unsigned long Data1;
		unsigned short Data2;
		unsigned short Data3;
		unsigned char Data4[8];
	} GUID;

	typedef struct IUnknown IUnknown;
	typedef struct IContextMenu IContextMenu;
	typedef struct IShellFolder IShellFolder;

	typedef struct IUnknownVtbl {
		HRESULT (__stdcall *QueryInterface)(IUnknown*, const GUID*, void**);
		ULONG   (__stdcall *AddRef)(IUnknown*);
		ULONG   (__stdcall *Release)(IUnknown*);
	} IUnknownVtbl;

	typedef struct IContextMenuVtbl {
		HRESULT (__stdcall *QueryInterface)(IContextMenu*, const GUID*, void**);
		ULONG   (__stdcall *AddRef)(IContextMenu*);
		ULONG   (__stdcall *Release)(IContextMenu*);
		HRESULT (__stdcall *QueryContextMenu)(IContextMenu*, HMENU, UINT, UINT, UINT, UINT);
		HRESULT (__stdcall *InvokeCommand)(IContextMenu*, void*);
		HRESULT (__stdcall *GetCommandString)(IContextMenu*, UINT, UINT, void*, char*, UINT);
	} IContextMenuVtbl;

	struct IContextMenu {
		IContextMenuVtbl* lpVtbl;
	};

	typedef struct IShellFolderVtbl {
		HRESULT (__stdcall *QueryInterface)(IShellFolder*, const GUID*, void**);
		ULONG   (__stdcall *AddRef)(IShellFolder*);
		ULONG   (__stdcall *Release)(IShellFolder*);
		HRESULT (__stdcall *ParseDisplayName)(IShellFolder*, HWND, void*, const wchar_t*, UINT*, LPITEMIDLIST*, UINT*);
		HRESULT (__stdcall *EnumObjects)(IShellFolder*, HWND, UINT, void**);
		HRESULT (__stdcall *BindToObject)(IShellFolder*, LPCITEMIDLIST, void*, const GUID*, void**);
		HRESULT (__stdcall *BindToStorage)(IShellFolder*, LPCITEMIDLIST, void*, const GUID*, void**);
		HRESULT (__stdcall *CompareIDs)(IShellFolder*, long, LPCITEMIDLIST, LPCITEMIDLIST);
		HRESULT (__stdcall *CreateViewObject)(IShellFolder*, HWND, const GUID*, void**);
		HRESULT (__stdcall *GetAttributesOf)(IShellFolder*, UINT, LPCITEMIDLIST*, UINT*);
		HRESULT (__stdcall *GetUIObjectOf)(IShellFolder*, HWND, UINT, LPCITEMIDLIST*, const GUID*, UINT*, void**);
	} IShellFolderVtbl;

	struct IShellFolder {
		IShellFolderVtbl* lpVtbl;
	};

	typedef struct {
		UINT cbSize;
		UINT fMask;
		HWND hwnd;
		const char* lpVerb;
		const char* lpParameters;
		const char* lpDirectory;
		int nShow;
		DWORD dwHotKey;
		void* hIcon;
	} CMINVOKECOMMANDINFO;

	HRESULT CoInitialize(void*);
	void CoUninitialize(void);

	HRESULT SHParseDisplayName(const wchar_t*,void*,PIDLIST_ABSOLUTE*,UINT,UINT*);
	HRESULT SHBindToParent(PCIDLIST_ABSOLUTE,const GUID*,void**,PCUITEMID_CHILD*);

	HMENU CreatePopupMenu(void);
	UINT TrackPopupMenu(HMENU, UINT, int, int, int, HWND, void*);
	HWND GetForegroundWindow(void);
	void GetCursorPos(POINT*);
	BOOL DestroyMenu(HMENU);
	void CoTaskMemFree(void*);
]]


-- Constants
local TPM_RETURNCMD = 0x0100
local SW_SHOWNORMAL = 1



-- GUID helpers

--- Creates a GUID structure.
local function GUID( d1, d2, d3, d4 )
	local g = ffi.new( 'GUID' )
	g.Data1 = d1
	g.Data2 = d2
	g.Data3 = d3
	ffi.copy( g.Data4, d4, 8 )
	return g
end

local IID_IShellFolder = GUID(
	0x000214E6, 0x0000, 0x0000,
	ffi.new( 'unsigned char[8]', { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } )
)

local IID_IContextMenu = GUID(
	0x000214E4, 0x0000, 0x0000,
	ffi.new( 'unsigned char[8]', { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } )
)



--- Opens the native Windows Explorer context menu for a file or folder.
local function openContextMenu( windowHWND, filepath )
	if not filepath or not windowHWND then
		return
	end

	ole32.CoInitialize( nil )

	local pidl = ffi.new( 'PIDLIST_ABSOLUTE[1]' )
	local wide = string.wcs( filepath )

	local hr = shell32.SHParseDisplayName( wide, nil, pidl, 0, nil )
	if hr ~= 0 then
		ole32.CoUninitialize()
		return
	end

	local psf   = ffi.new( 'IShellFolder*[1]' )
	local child = ffi.new( 'PCUITEMID_CHILD[1]' )

	hr = shell32.SHBindToParent(
		pidl[0],
		IID_IShellFolder,
		ffi.cast( 'void**', psf ),
		child
	)

	if hr ~= 0 then
		ole32.CoUninitialize()
		return
	end

	local pcm = ffi.new( 'IContextMenu*[1]' )

	hr = psf[0].lpVtbl.GetUIObjectOf(
		psf[0],
		nil,
		1,
		child,
		IID_IContextMenu,
		nil,
		ffi.cast( 'void**', pcm )
	)

	if hr ~= 0 then
		psf[0].lpVtbl.Release( psf[0] )
		ole32.CoUninitialize()
		return
	end

	local hMenu = user32.CreatePopupMenu()

	pcm[0].lpVtbl.QueryContextMenu( pcm[0], hMenu, 0, 1, 0x7FFF, 0 )

	local pt = ffi.new( 'POINT' )
	user32.GetCursorPos( pt )

	local cmd = user32.TrackPopupMenu( hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, windowHWND, nil )

	if cmd ~= 0 then
		local ici        = ffi.new( 'CMINVOKECOMMANDINFO' )
		ici.cbSize       = ffi.sizeof( ici )
		ici.fMask        = 0
		ici.hwnd         = windowHWND
		ici.lpVerb       = ffi.cast( 'const char*', cmd - 1 )
		ici.lpParameters = nil
		ici.lpDirectory  = nil
		ici.nShow        = SW_SHOWNORMAL

		pcm[0].lpVtbl.InvokeCommand( pcm[0], ici )
	end

	pcm[0].lpVtbl.Release( pcm[0] )
	psf[0].lpVtbl.Release( psf[0] )

	ole32.CoTaskMemFree( pidl[0] )
	user32.DestroyMenu( hMenu )

	ole32.CoUninitialize()
end


return openContextMenu