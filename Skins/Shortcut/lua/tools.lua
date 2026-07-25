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
	typedef unsigned short WCHAR;

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

	typedef struct {
		DWORD dwFileAttributes;
		DWORD ftCreationTime[2];
		DWORD ftLastAccessTime[2];
		DWORD ftLastWriteTime[2];
		DWORD nFileSizeHigh;
		DWORD nFileSizeLow;
		DWORD dwReserved0;
		DWORD dwReserved1;
		WCHAR cFileName[260];
		WCHAR cAlternateFileName[14];
	} WIN32_FIND_DATAW;

	DWORD_PTR SHGetFileInfoA( LPCSTR, DWORD, SHFILEINFOA*, UINT, UINT );

	BOOL      DestroyIcon(HICON);

	HDC       CreateCompatibleDC(HDC);
	BOOL      DeleteDC(HDC);
	HANDLE    SelectObject(HDC, HANDLE);
	BOOL      DeleteObject(HANDLE);
	HBITMAP   CreateDIBSection( HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD );
	BOOL      DrawIconEx( HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT );
	HANDLE    CreateFileA( LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE );
	BOOL      WriteFile( HANDLE, const void*, DWORD, DWORD*, void* );
	BOOL      CloseHandle( HANDLE );

	HANDLE    FindFirstFileW( const WCHAR*, WIN32_FIND_DATAW* );
	BOOL      FindNextFileW( HANDLE, WIN32_FIND_DATAW* );
	BOOL      FindClose( HANDLE );

	int       MultiByteToWideChar( UINT, DWORD, const char*, int, WCHAR*, int );
	int       WideCharToMultiByte( UINT, DWORD, const WCHAR*, int, char*, int, const char*, BOOL* );

	DWORD     GetFileAttributesW( const WCHAR* lpFileName );
	DWORD     GetLastError(void);

	static const DWORD INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF;
	static const DWORD FILE_ATTRIBUTE_HIDDEN    = 0x2;
	static const DWORD FILE_ATTRIBUTE_SYSTEM    = 0x4;
	static const DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10;
]]



-- Constantes
local SHGFI_ICON       = 0x00000100
local SHGFI_LARGEICON  = 0x00000000
local DI_NORMAL        = 0x0003
local BI_RGB           = 0
local DIB_RGB_COLORS   = 0

local GENERIC_WRITE         = 0x40000000
local CREATE_ALWAYS         = 2
local FILE_ATTRIBUTE_NORMAL = 0x80
local INVALID_HANDLE_VALUE  = ffi.cast( 'HANDLE', -1 )

local FILE_ATTR_DIR     = 0x10
local FILE_ATTR_HIDDEN  = 0x02
local FILE_ATTR_SYSTEM  = 0x04
-- Reparse point (junction / symlink) — evita recursão infinita
local FILE_ATTR_REPARSE = 0x400



-- Converte nFileSizeHigh + nFileSizeLow para número Lua sem perda de sinal.
-- DWORD é uint32, mas cdata em aritmética Lua pode ser tratado como signed.
-- tonumber() + mascaramento garante que valores > 2^31 sejam positivos.
local DWORD_MAX = 4294967296  -- 2^32
local function fileSize( high, low )
	local h = tonumber( high ) % DWORD_MAX   -- garante unsigned
	local l = tonumber( low  ) % DWORD_MAX   -- garante unsigned
	return h * DWORD_MAX + l
end



-- Unicode helpers

--- Converte string UTF-8 do Lua para wchar_t* (UTF-16).
local function utf8ToWide( str )
	local len = kernel32.MultiByteToWideChar( 65001, 0, str, -1, nil, 0 )
	if len <= 0 then return nil end
	local buf = ffi.new( 'wchar_t[?]', len )
	kernel32.MultiByteToWideChar( 65001, 0, str, -1, buf, len )
	return buf
end

--- Converte wchar_t* (UTF-16) para string UTF-8 do Lua.
local function wideToUtf8( wstr )
	local len = kernel32.WideCharToMultiByte( 65001, 0, wstr, -1, nil, 0, nil, nil )
	if len <= 0 then return nil end
	local buf = ffi.new( 'char[?]', len )
	kernel32.WideCharToMultiByte( 65001, 0, wstr, -1, buf, len, nil, nil )
	return ffi.string( buf, len - 1 )
end



-- Iterador seguro sobre entradas de uma pasta via FindFirstFileW / FindNextFileW.
-- Uso:
--   for data in iterDir( 'C:\\foo' ) do ... end
--
-- Garante:
--   • FindClose sempre chamado (mesmo em erro)
--   • condição de parada via valor de retorno Lua (boolean), nunca cdata BOOL
--   • não itera '.' nem '..'
local function iterDir( dir )
	local wpattern = utf8ToWide( dir .. '\\*' )
	if not wpattern then
		return function() return nil end
	end

	local data   = ffi.new( 'WIN32_FIND_DATAW' )
	local handle = kernel32.FindFirstFileW( wpattern, data )

	-- FindFirstFileW já preenche 'data' com a primeira entrada
	-- Usamos 'first' para entregá-la antes de chamar FindNextFileW
	if handle == INVALID_HANDLE_VALUE then
		return function() return nil end
	end

	local first = true
	local done  = false

	return function()
		if done then return nil end

		if first then
			-- Primeira entrada já está em 'data' — não chama FindNextFileW ainda
			first = false
		else
			-- Avança para a próxima entrada; retorno é BOOL (int cdata)
			-- Convertemos para boolean Lua explicitamente para evitar
			-- comportamento indefinido com 'not cdata_value'
			local ok = kernel32.FindNextFileW( handle, data )
			if ok == 0 then
				-- Sem mais entradas (ou erro) — fecha o handle e para
				kernel32.FindClose( handle )
				done = true
				return nil
			end
		end

		return data
	end
end



-- Obtém HICON real
local function GetAssociatedIcon( path )
	local shfi  = ffi.new( 'SHFILEINFOA' )
	local flags = bit.bor( SHGFI_ICON, SHGFI_LARGEICON )

	if shell32.SHGetFileInfoA( path, 0, shfi, ffi.sizeof( shfi ), flags ) ~= 0 then
		return shfi.hIcon
	end

	return nil
end



-- Extrai pixels do ícone (32-bit ARGB)
local function ExtractIconPixels( hIcon, size )
	local hdc = gdi32.CreateCompatibleDC( nil )
	if not hdc then return nil end

	local bmi = ffi.new( 'BITMAPINFO' )
	bmi.bmiHeader.biSize        = ffi.sizeof( 'BITMAPINFOHEADER' )
	bmi.bmiHeader.biWidth       = size
	bmi.bmiHeader.biHeight      = -size
	bmi.bmiHeader.biPlanes      = 1
	bmi.bmiHeader.biBitCount    = 32
	bmi.bmiHeader.biCompression = BI_RGB

	local ppBits  = ffi.new( 'void*[1]' )
	local hBitmap = gdi32.CreateDIBSection( hdc, bmi, DIB_RGB_COLORS, ppBits, nil, 0 )
	if not hBitmap then
		gdi32.DeleteDC( hdc )
		return nil
	end

	local oldBmp = gdi32.SelectObject( hdc, hBitmap )

	-- Limpar fundo
	ffi.fill( ppBits[0], size * size * 4, 0x00 )

	-- Desenhar ícone
	user32.DrawIconEx(
		hdc, 0, 0, hIcon,
		size, size, 0, nil, DI_NORMAL
	)

	local src = ffi.cast( 'uint8_t*', ppBits[0] )

	-- Despremultiplicar alfa
	for i = 0, size * size - 1 do
		local p = src + i * 4
		local a = p[3]
		if a ~= 0 then
			p[0] = math.min( 255, p[0] * 255 / a )
			p[1] = math.min( 255, p[1] * 255 / a )
			p[2] = math.min( 255, p[2] * 255 / a )
		end
	end

	-- Copiar pixels
	local imageSize = size * size * 4
	local outPixels = ffi.new( 'uint8_t[?]', imageSize )
	ffi.copy( outPixels, src, imageSize )

	-- Limpeza GDI
	gdi32.SelectObject( hdc, oldBmp )
	gdi32.DeleteObject( hBitmap )
	gdi32.DeleteDC( hdc )

	return outPixels
end



-- Salva ICO 32-bit válido
local function SaveICO( filename, pixels, size )
	local maskStride = math.ceil( size / 32 ) * 4
	local maskSize   = maskStride * size
	local imageSize  = size * size * 4

	local hFile = kernel32.CreateFileA(
		filename,
		GENERIC_WRITE,
		0,
		nil,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		nil
	)

	if hFile == INVALID_HANDLE_VALUE then return false end

	local written = ffi.new( 'DWORD[1]' )

	local dir   = ffi.new( 'ICONDIR', { 0, 1, 1 } )
	local entry = ffi.new( 'ICONDIRENTRY' )
	entry.bWidth        = size
	entry.bHeight       = size
	entry.wPlanes       = 1
	entry.wBitCount     = 32
	entry.dwImageOffset = ffi.sizeof( 'ICONDIR' ) + ffi.sizeof( 'ICONDIRENTRY' )
	entry.dwBytesInRes  = ffi.sizeof( 'BITMAPINFOHEADER' ) + imageSize + maskSize

	local bih = ffi.new( 'BITMAPINFOHEADER' )
	bih.biSize        = ffi.sizeof( 'BITMAPINFOHEADER' )
	bih.biWidth       = size
	bih.biHeight      = size * 2
	bih.biPlanes      = 1
	bih.biBitCount    = 32
	bih.biCompression = BI_RGB

	kernel32.WriteFile( hFile, dir,    ffi.sizeof( dir ),   written, nil )
	kernel32.WriteFile( hFile, entry,  ffi.sizeof( entry ), written, nil )
	kernel32.WriteFile( hFile, bih,    ffi.sizeof( bih ),   written, nil )
	kernel32.WriteFile( hFile, pixels, imageSize,           written, nil )

	local mask = ffi.new( 'uint8_t[?]', maskSize )
	ffi.fill( mask, maskSize, 0x00 )
	kernel32.WriteFile( hFile, mask, maskSize, written, nil )

	kernel32.CloseHandle( hFile )
	return true
end



local function create_recursive_dir( path )
	local normalized_path = path:gsub( '\\|', '/' ):gsub( '\\([^\\]+)(%.%w+)$', '' )

	if lfs.attributes( normalized_path ) then
		return true
	end

	local success, err = lfs.rmkdir( normalized_path )
	if success then
		return true
	else
		return false
	end
end



local function ExtractAndSaveAssociatedIcon( path, output, size )
	size = size or 32

	if not create_recursive_dir( output ) then
		return error( string.format( 'Failed to create directory: "%s"', output ) )
	end

	local hIcon = GetAssociatedIcon( path )
	if not hIcon then return false end

	local pixels = ExtractIconPixels( hIcon, size )
	user32.DestroyIcon( hIcon )

	if not pixels then return false end
	return SaveICO( output, pixels, size )
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

local ole32 = ffi.load( 'ole32' )



-- COM / Windows / Shell definitions
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



-- UTF-8 → UTF-16 (mantida apenas para openContextMenu via SHParseDisplayName)
local function toWide( str )
	local w = ffi.new( 'wchar_t[?]', #str + 1 )
	for i = 1, #str do
		w[i - 1] = str:byte( i )
	end
	w[#str] = 0
	return w
end



--- Opens the native Windows Explorer context menu for a file or folder.
function openContextMenu( windowHWND, filepath )
	if not filepath or not windowHWND then
		return
	end

	ole32.CoInitialize( nil )

	local pidl = ffi.new( 'PIDLIST_ABSOLUTE[1]' )
	local wide = toWide( filepath )

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
		local ici = ffi.new( 'CMINVOKECOMMANDINFO' )
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



-- @submodule listdir

local function win_attributes( fullpath )
	local wpath = utf8ToWide( fullpath )
	local attrs = kernel32.GetFileAttributesW( wpath )

	if attrs == ffi.C.INVALID_FILE_ATTRIBUTES then
		return nil, 'GetFileAttributesW failed (' .. ffi.C.GetLastError() .. ')'
	end

	return {
		hidden = bit.band( attrs, ffi.C.FILE_ATTRIBUTE_HIDDEN ) ~= 0,
		system = bit.band( attrs, ffi.C.FILE_ATTRIBUTE_SYSTEM ) ~= 0
	}
end


local iconPath = rain:var( '#CURRENTPATH#icons\\' )

local function listdir( dir, subfolder )
	local result = {}

	for data in iterDir( dir ) do
		local nameUtf = wideToUtf8( data.cFileName )
		if nameUtf and nameUtf ~= '.' and nameUtf ~= '..' then
			local attrs  = data.dwFileAttributes
			local hidden = bit.band( attrs, FILE_ATTR_HIDDEN ) ~= 0

			if not hidden then
				local isDir    = bit.band( attrs, FILE_ATTR_DIR )    ~= 0
				local filePath = dir .. '\\' .. nameUtf

				local item = {
					filePath         = filePath,
					path             = dir,
					file             = nameUtf,
					size             = fileSize( data.nFileSizeHigh, data.nFileSizeLow ),
					name             = nameUtf:gsub( '(%.%w+)$', '' ),
					dateCreated      = data.ftCreationTime[0],
					dateLastAccessed = data.ftLastAccessTime[0],
					hidden           = false,
					system           = bit.band( attrs, FILE_ATTR_SYSTEM ) ~= 0,
					directory        = isDir
				}

				if isDir then
					item.type = 'folder'
					item.ext  = nil

					if subfolder then listdir( filePath ) end
				else
					local ext = nameUtf:match( '^.+%.(.+)$' ) or nil
					item.type = 'file'
					item.ext  = ext

					if ext and not lfs.exists( iconPath .. 'cache\\' .. ext .. '.png' ) then
						ExtractAndSaveAssociatedIcon( filePath, iconPath .. 'cache\\' .. ext .. '.png' )
					end
				end

				table.insert( result, item )
			end
		end
	end

	return result
end



-- fGroup carregado uma única vez fora da recursão
local fGroup = require( 'lua.fGroup' )

local function folderInfo( dir, _depth )
	-- Proteção contra recursão profunda demais (ex: junctions circulares)
	_depth = _depth or 0
	if _depth > 64 then return { size = 0, files = 0, folders = 0, groups = {} } end

	local result = {
		path    = dir,
		size    = 0,
		files   = 0,   -- total de arquivos (recursivo)
		folders = 0,   -- total de subpastas (recursivo)
		groups  = {}
	}

	for data in iterDir( dir ) do
		local nameUtf = wideToUtf8( data.cFileName )
		if nameUtf and nameUtf ~= '.' and nameUtf ~= '..' then
			local attrs   = data.dwFileAttributes
			local hidden  = bit.band( attrs, FILE_ATTR_HIDDEN )  ~= 0
			local isDir   = bit.band( attrs, FILE_ATTR_DIR )     ~= 0
			-- Reparse point: junction ou symlink — pula para não entrar em loop
			local reparse = bit.band( attrs, FILE_ATTR_REPARSE ) ~= 0

			if not hidden then
				local filePath = dir .. '\\' .. nameUtf

				if isDir and not reparse then
					local sub = folderInfo( filePath, _depth + 1 )
					result.size    = result.size    + sub.size
					result.files   = result.files   + sub.files
					result.folders = result.folders + sub.folders + 1

					for group, count in pairs( sub.groups ) do
						result.groups[group] = ( result.groups[group] or 0 ) + count
					end

					result.groups.folders = ( result.groups.folders or 0 ) + 1

				elseif not isDir then
					local size = fileSize( data.nFileSizeHigh, data.nFileSizeLow )
					result.size  = result.size  + size
					result.files = result.files + 1

					local ext = nameUtf:match( '.*(%..+)$' )

					if not ext then
						result.groups.unknown = ( result.groups.unknown or 0 ) + 1
					else
						local found = false
						for group, list in pairs( fGroup ) do
							if list[ext] then
								result.groups[group] = ( result.groups[group] or 0 ) + 1
								found = true
								break
							end
						end

						if not found then
							result.groups.unknown = ( result.groups.unknown or 0 ) + 1
						end
					end
				end
			end
		end
	end

	return result
end



return {
	saveICO     = ExtractAndSaveAssociatedIcon,
	contextMenu = openContextMenu,
	listdir     = listdir,
	folderInfo  = folderInfo
}
