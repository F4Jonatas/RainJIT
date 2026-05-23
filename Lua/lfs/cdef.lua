-- Windows basic types
local ffi = require( 'ffi' )

ffi.cdef[[
typedef void* HWND;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef unsigned int UINT;
typedef long long LPARAM;
typedef int BOOL;

typedef struct {
	HWND hwndOwner;
	void* pidlRoot;
	LPWSTR pszDisplayName;
	LPCWSTR lpszTitle;
	UINT ulFlags;
	void* lpfn;
	LPARAM lParam;
	int iImage;
} BROWSEINFOW;
]]

-- Windows API functions
ffi.cdef[[
void* SHBrowseForFolderW(BROWSEINFOW* lpbi);
BOOL SHGetPathFromIDListW(void* pidl, LPWSTR pszPath);
void CoTaskMemFree(void* pv);

int MultiByteToWideChar( unsigned int CodePage, unsigned long dwFlags, const char* lpMultiByteStr, int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar);
int WideCharToMultiByte( unsigned int CodePage, unsigned long dwFlags, const wchar_t* lpWideCharStr, int cchWideChar, char* lpMultiByteStr, int cbMultiByte, const char* lpDefaultChar, int* lpUsedDefaultChar);
]]
