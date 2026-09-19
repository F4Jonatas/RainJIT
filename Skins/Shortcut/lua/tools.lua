

require( 'string.utf8' )

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



-- Iterador seguro sobre entradas de uma pasta via FindFirstFileW / FindNextFileW.
-- Uso:
--   for data in iterDir( 'C:\\foo' ) do ... end
--
-- Garante:
--   • FindClose sempre chamado (mesmo em erro)
--   • condição de parada via valor de retorno Lua (boolean), nunca cdata BOOL
--   • não itera '.' nem '..'
local function iterDir( dir )
	local wpattern = string.wcs( dir .. '\\*' )
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



--- Retrieves filesystem attributes for a physical Windows path.
--
-- @param (string) fullpath Absolute filesystem path.
-- @return (table|nil) Attributes table.
-- @return (string|nil) Error message when the Win32 call fails.
local function win_attributes( fullpath )
	local wpath = string.wcs( fullpath )
	local attrs = kernel32.GetFileAttributesW(wpath)

	if attrs == ffi.C.INVALID_FILE_ATTRIBUTES then
		return nil, 'GetFileAttributesW failed (' .. ffi.C.GetLastError() .. ')'
	end

	return {
		hidden = bit.band( attrs, ffi.C.FILE_ATTRIBUTE_HIDDEN ) ~= 0,
		system = bit.band( attrs, ffi.C.FILE_ATTRIBUTE_SYSTEM ) ~= 0
	}
end


-- @submodule listdir

ffi.cdef[[
	typedef int32_t HRESULT;
	typedef uint32_t ULONG;
	typedef uint32_t UINT;
	typedef uint32_t DWORD;
	typedef uint16_t WCHAR;

	typedef struct {
		uint32_t Data1;
		uint16_t Data2;
		uint16_t Data3;
		uint8_t Data4[8];
	} GUID;

	typedef struct {
		UINT uType;

		union {
			WCHAR *pOleStr;
			UINT uOffset;
			char cStr[260];
		};
	} STRRET;

	HRESULT CoInitializeEx(
		void *pvReserved,
		DWORD dwCoInit
	);

	void CoUninitialize(void);

	void CoTaskMemFree(
		void *pv
	);

	HRESULT SHGetDesktopFolder(
		void **ppshf
	);

	HRESULT SHParseDisplayName(
		const WCHAR *pszName,
		void *pbc,
		void **ppidl,
		DWORD sfgaoIn,
		DWORD *psfgaoOut
	);

	HRESULT SHBindToObject(
		void *psf,
		void *pidl,
		void *pbc,
		const GUID *riid,
		void **ppv
	);

	HRESULT StrRetToBufW(
		STRRET *pstr,
		void *pidl,
		WCHAR *pszBuf,
		UINT cchBuf
	);

	/*
	 * IShellFolder::ParseDisplayName
	 */
	typedef HRESULT (__stdcall *IShellFolder_ParseDisplayName)(
		void *This,
		void *hwnd,
		void *pbc,
		WCHAR *pszDisplayName,
		ULONG *pchEaten,
		void **ppidl,
		ULONG *pdwAttributes
	);

	/*
	 * IShellFolder::EnumObjects
	 */
	typedef HRESULT (__stdcall *IShellFolder_EnumObjects)(
		void *This,
		void *hwnd,
		DWORD grfFlags,
		void **ppEnumIDList
	);

	/*
	 * IShellFolder::GetDisplayNameOf
	 */
	typedef HRESULT (__stdcall *IShellFolder_GetDisplayNameOf)(
		void *This,
		void *pidl,
		DWORD uFlags,
		STRRET *pName
	);

	/*
	 * IUnknown::Release
	 */
	typedef ULONG (__stdcall *IUnknown_Release)(
		void *This
	);

	/*
	 * IEnumIDList::Next
	 */
	typedef HRESULT (__stdcall *IEnumIDList_Next)(
		void *This,
		ULONG celt,
		void **rgelt,
		ULONG *pceltFetched
	);
]]

local shell32 = ffi.load( 'shell32' )
local shlwapi = ffi.load( 'shlwapi' )
local ole32   = ffi.load( 'ole32'   )

local IID_ISHELL_FOLDER = ffi.new('GUID', {
	Data1 = 0x000214E6,
	Data2 = 0x0000,
	Data3 = 0x0000,
	Data4 = { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
})

local COINIT_APARTMENTTHREADED = 0x2

local SHCONTF_FOLDERS    = 0x20
local SHCONTF_NONFOLDERS = 0x40

local SHGDN_NORMAL     = 0x0000
local SHGDN_FORPARSING = 0x8000

local RPC_E_CHANGED_MODE = -2147417850

--- Checks whether a path represents a Windows Shell namespace.
--
-- Shell namespaces are virtual locations and cannot be enumerated using the
-- regular filesystem APIs used by iterDir().
--
-- @usage
--   isShellNamespace( 'shell:AppsFolder' )
--   isShellNamespace( 'shell:Desktop' )
--   isShellNamespace( 'shell:ControlPanelFolder' )
--   isShellNamespace( '::{20D04FE0-3AEA-1069-A2D8-08002B30309D}' )
--
-- @param (string) path - Path to inspect.
-- @return (boolean) True when path is a Shell namespace.
local function isShellNamespace( path )
	if type( path ) ~= 'string' then
		return false
	end

	local lower = path:lower()
	return lower:sub(1, 6) == 'shell:' or path:sub(1, 2) == '::'
end



--- Releases a COM interface.
--
-- @param (cdata) object - COM interface pointer.
-- @return (nil)
local function releaseCom( object )
	if object == nil then
		return
	end

	local vtable  = ffi.cast( 'void***', object )[0]
	local release = ffi.cast( 'IUnknown_Release', vtable[2] )

	release( object )
end



--- Retrieves a display name from a Shell item.
--
-- @param (cdata) folder - IShellFolder interface.
-- @param (cdata) pidl - Child PIDL relative to folder.
-- @param (number) flags - SHGDN flags.
-- @return (string|nil) Display name.
local function shellDisplayName( folder, pidl, flags )
	local vtable = ffi.cast( 'void***', folder )[0]

	local getDisplayNameOf = ffi.cast( 'IShellFolder_GetDisplayNameOf', vtable[11] )
	local strret           = ffi.new( 'STRRET' )
	local buffer           = ffi.new( 'WCHAR[32768]' )

	local hr = getDisplayNameOf( folder, pidl, flags, strret )
	if tonumber( hr ) < 0 then
		return nil
	end

	hr = shlwapi.StrRetToBufW( strret, pidl, buffer, 32768 )

	if tonumber( hr ) < 0 then
		return nil
	end

	return string.mbs( buffer )
end



--- Enumerates the contents of a Windows Shell namespace.
-- This function uses the native IShellFolder/IEnumIDList interfaces instead
-- of LuaCOM. LuaCOM cannot access the indexed FolderItems.Item property used
-- by this particular Shell collection.
--
-- @param (string) dir - Shell namespace path.
-- @param (boolean) [subfolder=false] - Recursively enumerate child folders.
-- @return (table) Array containing Shell entries.
local function listShellDirectory( dir, subfolder )
	local result = {}

	-- COM is usually already initialized by the host application.
	-- We initialize it here only when necessary.
	local comHr          = ole32.CoInitializeEx( nil, COINIT_APARTMENTTHREADED )
	local comInitialized = ( comHr == 0 or comHr == 1 )

	if comHr ~= 0 and comHr ~= 1 and comHr ~= RPC_E_CHANGED_MODE then
		error(('CoInitializeEx failed (0x%08X)')
			:format( tonumber( ffi.cast( 'uint32_t', comHr )))
		)
	end

	local function cleanup()
		if comInitialized then
			ole32.CoUninitialize()
		end
	end

	--- Obtain the Shell desktop folder.
	-- The desktop folder is the root of the Shell namespace. From there,
	-- ParseDisplayName can resolve shell: paths into PIDLs.
	local desktop = ffi.new( 'void*[1]' )
	local hr      = shell32.SHGetDesktopFolder( desktop )

	if tonumber( hr ) < 0 then
		cleanup()

		error(('SHGetDesktopFolder failed (0x%08X)'):format( tonumber( ffi.cast( 'uint32_t', hr ))))
	end

	local desktopVtable    = ffi.cast( 'void***', desktop[0] )[0]
	local parseDisplayName = ffi.cast( 'IShellFolder_ParseDisplayName', desktopVtable[3] )
	local namespacePidl    = ffi.new( 'void*[1]' )
	local eaten            = ffi.new( 'ULONG[1]' )

	-- Parse the namespace path relative to the Shell desktop.
	hr = parseDisplayName(
		desktop[0],
		nil,
		nil,
		string.wcs( dir ),
		eaten,
		namespacePidl,
		nil
	)

	if tonumber( hr ) < 0 or namespacePidl[0] == nil then
		releaseCom( desktop[0] )
		cleanup()

		error(('IShellFolder::ParseDisplayName failed for "%s" (0x%08X)')
			:format( dir, tonumber( ffi.cast( 'uint32_t', hr )))
		)
	end

	-- Bind the parsed PIDL to an IShellFolder object.
	local folder = ffi.new( 'void*[1]' )
	hr = shell32.SHBindToObject( desktop[0], namespacePidl[0], nil, IID_ISHELL_FOLDER, ffi.cast( 'void**', folder ))

	ole32.CoTaskMemFree( namespacePidl[0] )
	releaseCom( desktop[0] )

	if tonumber( hr ) < 0 or folder[0] == nil then
		cleanup()

		error(('SHBindToObject failed for "%s" (0x%08X)')
			:format( dir, tonumber( ffi.cast( 'uint32_t', hr )))
		)
	end

	local folderVtable = ffi.cast( 'void***', folder[0] )[0]

	-- IShellFolder::EnumObjects is vtable slot 4.
	local enumObjects = ffi.cast( 'IShellFolder_EnumObjects', folderVtable[4] )
	local enum = ffi.new('void*[1]')

	hr = enumObjects( folder[0], nil, bit.bor( SHCONTF_FOLDERS, SHCONTF_NONFOLDERS ), enum )

	if tonumber( hr ) < 0 then
		releaseCom( folder[0] )
		cleanup()

		error(('IShellFolder::EnumObjects failed for "%s" (0x%08X)')
			:format( dir, tonumber( ffi.cast( 'uint32_t', hr )))
		)
	end

	-- S_FALSE is also a valid result indicating that no matching children
	-- exist. In that case the enumerator pointer may be NULL.
	if enum[0] == nil then
		releaseCom( folder[0] )
		cleanup()

		return result
	end

	local enumObject = enum[0]
	local enumVtable = ffi.cast( 'void***', enumObject )[0]

	-- IEnumIDList::Next is vtable slot 3.
	local nextItem = ffi.cast( 'IEnumIDList_Next', enumVtable[3] )

	while true do
		local pidl    = ffi.new( 'void*[1]' )
		local fetched = ffi.new( 'ULONG[1]' )

		hr = nextItem( enumObject, 1, pidl, fetched )

		if tonumber( hr ) < 0 or fetched[0] == 0 then
			break
		end

		local itemPidl = pidl[0]

		-- Use the Shell folder itself to obtain the item name.
		local nameUtf = shellDisplayName( folder[0], itemPidl, SHGDN_NORMAL )

		-- Obtain the parsing name separately.
		-- For AppsFolder this is commonly the application identifier,
		-- which is what we need to reconstruct:
		--
		-- shell:AppsFolder\<identifier>
		local parsingName = shellDisplayName( folder[0], itemPidl, SHGDN_FORPARSING )

		-- Do not silently discard the item when the parsing name is unavailable.
		-- The display name is sufficient for the returned item in that case.
		if nameUtf or parsingName then
			local displayName = nameUtf or parsingName
			local itemPath    = dir

			if parsingName and parsingName ~= '' then
				itemPath = dir .. '\\' .. parsingName
			end

			local file = parsingName or displayName
			local item = {
				filePath         = itemPath,
				path             = dir,
				file             = file,
				size             = 0,
				name             = displayName,
				dateCreated      = nil,
				dateLastAccessed = nil,
				hidden           = false,
				system           = false,
				directory        = false,
				type             = 'application',
				ext              = file:match( '^.+%.(.+)$' ) or nil
			}

			table.insert( result, item )
		end

		-- IEnumIDList::Next allocates each PIDL. Release it after use.
		ole32.CoTaskMemFree( itemPidl )
	end

	releaseCom( enumObject )
	releaseCom( folder[0] )
	cleanup()

	return result
end


local iconPath = rain:var( '#CURRENTPATH#icons\\\\' )



--- Lists the contents of a filesystem directory or Windows Shell namespace.
--
-- Normal filesystem paths are enumerated through iterDir(). Virtual Windows
-- Shell namespaces such as shell:AppsFolder are enumerated through the native
-- Shell API.
--
-- @param (string) dir - Directory or Shell namespace to enumerate.
-- @param (boolean) [subfolder=false] - Recursively enumerate subdirectories.
-- @return (table) Array containing directory entries.
local function listdir( dir, subfolder )
	-- shell:... and ::{GUID} locations are not filesystem paths.
	if isShellNamespace( dir ) then
		return listShellDirectory( dir, subfolder )
	end

	local result = {}

	for data in iterDir( dir ) do
		local nameUtf = string.mbs( data.cFileName )

		if nameUtf and nameUtf ~= '.' and nameUtf ~= '..' then
			local attrs = data.dwFileAttributes
			local hidden = bit.band( attrs, FILE_ATTR_HIDDEN ) ~= 0

			if not hidden then
				local isDir = bit.band( attrs, FILE_ATTR_DIR ) ~= 0
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

					if subfolder then
						local children = listdir( filePath, true )

						for _, child in ipairs( children ) do
							table.insert( result, child )
						end
					end

				else
					local ext = nameUtf:match( '^.+%.(.+)$' ) or nil

					item.type = 'file'
					item.ext  = ext

					if ext and not lfs.exists( iconPath .. 'cache\\\\' .. ext .. '.png' ) then
						ExtractAndSaveAssociatedIcon( filePath, iconPath .. 'cache\\\\' .. ext .. '.png' )
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
		local nameUtf = string.mbs( data.cFileName )
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
	contextMenu = require( 'menu.contextmenu' ),
	listdir     = listdir,
	folderInfo  = folderInfo
}
