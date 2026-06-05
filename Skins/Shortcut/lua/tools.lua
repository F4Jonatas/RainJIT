

local ffi = require( 'ffi' )
local lfs = require( 'lfs.utils' )


local shell32  = ffi.load( 'shell32' )
local user32   = ffi.load( 'user32' )
local gdi32    = ffi.load( 'gdi32' )
local kernel32 = ffi.load( 'kernel32' )

ffi.cdef[[
typedef void* HANDLE;
typedef void* HICON;
typedef void* HDC;
typedef void* HBITMAP;
typedef void* HBRUSH;
typedef unsigned int UINT;
typedef int BOOL;
typedef long LONG;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef const char* LPCSTR;
typedef size_t DWORD_PTR;

typedef struct {
	HICON hIcon;
	int   iIcon;
	DWORD dwAttributes;
	char  szDisplayName[260];
	char  szTypeName[80];
} SHFILEINFOA;

typedef struct {
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct {
	BITMAPINFOHEADER bmiHeader;
	DWORD bmiColors[1];
} BITMAPINFO;

typedef struct {
	WORD idReserved;
	WORD idType;
	WORD idCount;
} ICONDIR;

typedef struct {
	BYTE  bWidth;
	BYTE  bHeight;
	BYTE  bColorCount;
	BYTE  bReserved;
	WORD  wPlanes;
	WORD  wBitCount;
	DWORD dwBytesInRes;
	DWORD dwImageOffset;
} ICONDIRENTRY;

DWORD_PTR SHGetFileInfoA( LPCSTR, DWORD, SHFILEINFOA*, UINT, UINT );

BOOL DestroyIcon(HICON);

HDC CreateCompatibleDC(HDC);
BOOL DeleteDC(HDC);
HANDLE SelectObject(HDC, HANDLE);
BOOL DeleteObject(HANDLE);
HBITMAP CreateDIBSection( HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD );
BOOL DrawIconEx( HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT );
HANDLE CreateFileA( LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE );
BOOL WriteFile( HANDLE, const void*, DWORD, DWORD*, void* );
BOOL CloseHandle( HANDLE );
]]

-- Constantes
local SHGFI_ICON       = 0x00000100
local SHGFI_LARGEICON  = 0x00000000
local DI_NORMAL        = 0x0003
local BI_RGB           = 0
local DIB_RGB_COLORS   = 0

local GENERIC_WRITE    = 0x40000000
local CREATE_ALWAYS    = 2
local FILE_ATTRIBUTE_NORMAL = 0x80
local INVALID_HANDLE_VALUE = ffi.cast( 'HANDLE', -1)

----------------------------------------------------------------
-- Obtém HICON real
----------------------------------------------------------------
local function GetAssociatedIcon( path )
	local shfi = ffi.new( 'SHFILEINFOA' )
	local flags = bit.bor( SHGFI_ICON, SHGFI_LARGEICON )

	if shell32.SHGetFileInfoA( path, 0, shfi, ffi.sizeof( shfi ), flags ) ~= 0 then
		return shfi.hIcon
	end

	return nil
end

----------------------------------------------------------------
-- Extrai pixels do ícone (32-bit ARGB)
----------------------------------------------------------------
local function ExtractIconPixels( hIcon, size )
	local hdc = gdi32.CreateCompatibleDC( nil )
	if not hdc then return nil end

	local bmi = ffi.new('BITMAPINFO' )
	bmi.bmiHeader.biSize = ffi.sizeof('BITMAPINFOHEADER')
	bmi.bmiHeader.biWidth = size
	bmi.bmiHeader.biHeight = -size
	bmi.bmiHeader.biPlanes = 1
	bmi.bmiHeader.biBitCount = 32
	bmi.bmiHeader.biCompression = BI_RGB

	local ppBits = ffi.new('void*[1]')
	local hBitmap = gdi32.CreateDIBSection(
		hdc, bmi, DIB_RGB_COLORS, ppBits, nil, 0
	)
	if not hBitmap then
		gdi32.DeleteDC(hdc)
		return nil
	end

	local oldBmp = gdi32.SelectObject(hdc, hBitmap)

	-- Limpar fundo
	ffi.fill(ppBits[0], size * size * 4, 0x00)

	-- Desenhar ícone
	user32.DrawIconEx(
		hdc, 0, 0, hIcon,
		size, size, 0, nil, DI_NORMAL
	)

	local src = ffi.cast('uint8_t*', ppBits[0])

	-- Despremultiplicar alfa
	for i = 0, size * size - 1 do
		local p = src + i * 4
		local a = p[3]
		if a ~= 0 then
			p[0] = math.min(255, p[0] * 255 / a)
			p[1] = math.min(255, p[1] * 255 / a)
			p[2] = math.min(255, p[2] * 255 / a)
		end
	end

	-- COPIAR pixels (PASSO CRÍTICO)
	local imageSize = size * size * 4
	local outPixels = ffi.new('uint8_t[?]', imageSize)
	ffi.copy(outPixels, src, imageSize)

	-- Limpeza GDI
	gdi32.SelectObject(hdc, oldBmp)
	gdi32.DeleteObject(hBitmap)
	gdi32.DeleteDC(hdc)

	return outPixels
end


----------------------------------------------------------------
-- Salva ICO 32-bit válido
----------------------------------------------------------------
local function SaveICO( filename, pixels, size )
	local maskStride = math.ceil( size / 32 ) * 4
	local maskSize = maskStride * size
	local imageSize = size * size * 4

	local hFile = kernel32.CreateFileA(
		filename,
		GENERIC_WRITE,
		0,
		nil,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		nil
	)

	if hFile == INVALID_HANDLE_VALUE then return false end

	local written = ffi.new('DWORD[1]')

	local dir = ffi.new('ICONDIR', {0, 1, 1})
	local entry = ffi.new('ICONDIRENTRY')
	entry.bWidth = size
	entry.bHeight = size
	entry.wPlanes = 1
	entry.wBitCount = 32
	entry.dwImageOffset = ffi.sizeof('ICONDIR') + ffi.sizeof('ICONDIRENTRY')
	entry.dwBytesInRes =
		ffi.sizeof('BITMAPINFOHEADER') + imageSize + maskSize

	local bih = ffi.new('BITMAPINFOHEADER')
	bih.biSize = ffi.sizeof('BITMAPINFOHEADER')
	bih.biWidth = size
	bih.biHeight = size * 2
	bih.biPlanes = 1
	bih.biBitCount = 32
	bih.biCompression = BI_RGB

	kernel32.WriteFile(hFile, dir, ffi.sizeof(dir), written, nil)
	kernel32.WriteFile(hFile, entry, ffi.sizeof(entry), written, nil)
	kernel32.WriteFile(hFile, bih, ffi.sizeof(bih), written, nil)
	kernel32.WriteFile(hFile, pixels, imageSize, written, nil)

	local mask = ffi.new('uint8_t[?]', maskSize)
	ffi.fill(mask, maskSize, 0x00)
	kernel32.WriteFile(hFile, mask, maskSize, written, nil)

	kernel32.CloseHandle(hFile)
	return true
end



local function create_recursive_dir(path)
	-- Normalize the path to handle platform-specific differences (e.g., Windows vs Unix separators)
	-- Although lfs.rmkdir assumes '/' as the separator internally, it handles normalization
	local normalized_path = path:gsub( '\\|', '/' ):gsub( '\\([^\\]+)(%.%w+)$', '' )

		if lfs.attributes(normalized_path) then
		return true
	end

	-- lfs.rmkdir() creates missing intermediate directories
	local success, err = lfs.rmkdir( normalized_path )
	if success then
		return true
	else
		return false
	end
end



local function ExtractAndSaveAssociatedIcon( path, output, size )
	size = size or 32

	-- Create the output directory if it doesn't exist
	if not create_recursive_dir(output) then
		return error( string.format( 'Failed to create directory: "%s"', output ))
	end

	local hIcon = GetAssociatedIcon(path)
	if not hIcon then return false end

	local pixels = ExtractIconPixels(hIcon, size)
	user32.DestroyIcon(hIcon)

	if not pixels then return false end
	return SaveICO(output, pixels, size)
end














-- @file contextMenu.lua
-- @brief Windows Explorer Context Menu integration for Rainmeter using LuaJIT FFI.
--
-- This script invokes the native Windows Explorer context menu for a given
-- filesystem path. It uses low-level COM interfaces (IShellFolder, IContextMenu)
-- and is designed to operate safely within Rainmeter constraints.
--
-- IMPORTANT:
-- - This code intentionally initializes and uninitializes COM per invocation.
-- - This is required to avoid Shell extension instability in non-Explorer hosts.
-- - Rainmeter is NOT a full Shell host; certain behaviors are expected.
--
-- @author F4Jonatas
-- @license MIT (implicit, no warranty)

-- local ffi = require('ffi')

-- local user32 = ffi.load('user32')
-- local shell32 = ffi.load('shell32')
local ole32 = ffi.load('ole32')



-- COM / Windows / Shell definitions
ffi.cdef[[
typedef long HRESULT;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef void* HWND;
typedef void* HMENU;
typedef void* HINSTANCE;
typedef void* LPCITEMIDLIST;
typedef void* LPITEMIDLIST;
typedef void* PCIDLIST_ABSOLUTE;
typedef void* PCUITEMID_CHILD;
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
-- @param d1 number
-- @param d2 number
-- @param d3 number
-- @param d4 cdata[8]
-- @return GUID
local function GUID(d1, d2, d3, d4)
	local g = ffi.new('GUID')
	g.Data1 = d1
	g.Data2 = d2
	g.Data3 = d3
	ffi.copy(g.Data4, d4, 8)
	return g
end

local IID_IShellFolder = GUID(
	0x000214E6, 0x0000, 0x0000,
	ffi.new('unsigned char[8]', { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 })
)

local IID_IContextMenu = GUID(
	0x000214E4, 0x0000, 0x0000,
	ffi.new('unsigned char[8]', { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 })
)



-- UTF-8 → UTF-16 (ASCII-safe, sufficient for standard paths)

--- Converts a Lua string to a UTF-16 wide string.
-- @param str string
-- @return wchar_t*
local function toWide(str)
	local w = ffi.new('wchar_t[?]', #str + 1)
	for i = 1, #str do
		w[i - 1] = str:byte(i)
	end
	w[#str] = 0
	return w
end



-- Explorer context menu entry point

--- Opens the native Windows Explorer context menu for a file or folder.
--
-- This function:
-- - Initializes COM in STA mode
-- - Resolves the filesystem PIDL
-- - Binds to the parent IShellFolder
-- - Retrieves IContextMenu
-- - Displays the popup menu at the cursor position
-- - Invokes the selected command
-- - Releases all COM interfaces
--
-- @param filepath string Absolute filesystem path
function openContextMenu( windowHWND, filepath )
	if not filepath or not windowHWND then
		return
	end

	ole32.CoInitialize(nil)

	local pidl = ffi.new('PIDLIST_ABSOLUTE[1]')
	local wide = toWide(filepath)

	local hr = shell32.SHParseDisplayName(wide, nil, pidl, 0, nil)
	if hr ~= 0 then
		ole32.CoUninitialize()
		return
	end

	local psf = ffi.new('IShellFolder*[1]')
	local child = ffi.new('PCUITEMID_CHILD[1]')

	hr = shell32.SHBindToParent(
		pidl[0],
		IID_IShellFolder,
		ffi.cast('void**', psf),
		child
	)

	if hr ~= 0 then
		ole32.CoUninitialize()
		return
	end

	local pcm = ffi.new('IContextMenu*[1]')

	hr = psf[0].lpVtbl.GetUIObjectOf(
		psf[0],
		nil,
		1,
		child,
		IID_IContextMenu,
		nil,
		ffi.cast('void**', pcm)
	)

	if hr ~= 0 then
		psf[0].lpVtbl.Release(psf[0])
		ole32.CoUninitialize()
		return
	end

	local hMenu = user32.CreatePopupMenu()

	pcm[0].lpVtbl.QueryContextMenu(pcm[0], hMenu, 0, 1, 0x7FFF, 0)

	local pt = ffi.new('POINT')
	user32.GetCursorPos(pt)

	local cmd = user32.TrackPopupMenu( hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, windowHWND, nil )

	if cmd ~= 0 then
		local ici = ffi.new('CMINVOKECOMMANDINFO')
		ici.cbSize = ffi.sizeof(ici)
		ici.fMask = 0
		ici.hwnd = windowHWND
		ici.lpVerb = ffi.cast("const char*", cmd - 1)
		ici.lpParameters = nil
		ici.lpDirectory = nil
		ici.nShow = SW_SHOWNORMAL

		pcm[0].lpVtbl.InvokeCommand(pcm[0], ici)
	end

	pcm[0].lpVtbl.Release(pcm[0])
	psf[0].lpVtbl.Release(psf[0])

	ole32.CoTaskMemFree(pidl[0])
	user32.DestroyMenu(hMenu)

	ole32.CoUninitialize()
end









-- @file listdir.lua
ffi.cdef[[
	typedef unsigned long DWORD;
	typedef const wchar_t* LPCWSTR;
	DWORD GetFileAttributesW(LPCWSTR lpFileName);
	DWORD GetLastError(void);
	static const DWORD INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF;
	static const DWORD FILE_ATTRIBUTE_HIDDEN = 0x2;
	static const DWORD FILE_ATTRIBUTE_SYSTEM = 0x4;
	static const DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10;
]]

local function win_attributes( fullpath )
	local wpath = toWide( fullpath )
	local attrs = kernel32.GetFileAttributesW( wpath )

	if attrs == ffi.C.INVALID_FILE_ATTRIBUTES then
		return nil, 'GetFileAttributesW failed ('.. ffi.C.GetLastError() ..')'
	end

	return {
		hidden = bit.band(attrs, ffi.C.FILE_ATTRIBUTE_HIDDEN)    ~= 0,
		system = bit.band(attrs, ffi.C.FILE_ATTRIBUTE_SYSTEM)    ~= 0
	}

end


local iconPath = rain:var( '#CURRENTPATH#icons\\' )
local function listdir( dir, subfolder )
	local result = {}

	for entry in lfs.dir( dir ) do
		if entry ~= "." and entry ~= '..' then
			local filePath = dir .. '\\' .. entry
			local attrs = lfs.attributes( filePath )
			local winattr = win_attributes( filePath )

			local item = {
					filePath = filePath,
					path = dir,
					file = entry,
					size = attrs.size,
					name = entry:gsub( '(%.%w+)$', '' ),
					dateCreated = attrs.modification,
					dateLastAccessed = attrs.access,
					hidden = winattr and winattr.hidden or false,
					system = winattr and winattr.system or false,
					directory = winattr and winattr.directory or false
				}

			-- Não mostrar arquivos protegidos pelo sistema
			if winattr.hidden == true then goto continue return end

			if attrs.mode == 'directory' then
				item.type = 'folder'
				item.ext = nil

				if subfolder then listdir( filePath ) end

			else
				local ext = entry:match( '^.+%.(.+)$' ) or nil
				item.type = 'file'
				item.ext = ext

				if not lfs.exists( iconPath .. 'cache\\' .. ext .. '.png' ) then
					ExtractAndSaveAssociatedIcon( filePath, iconPath .. 'cache\\' .. ext .. '.png' )
				end
			end

			table.insert( result, item )

			::continue::
		end
	end

	return result
end



return {
	saveICO = ExtractAndSaveAssociatedIcon,
	contextMenu = openContextMenu,
	listdir = listdir
}