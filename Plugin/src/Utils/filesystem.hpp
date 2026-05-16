#pragma once

#include <Windows.h>
#include <string>

namespace fs {

	/**
	 * @brief Check if file exists on filesystem.
	 *
	 * Helper function based on Win32 API. Returns false if path
	 * doesn't exist or represents a directory.
	 *
	 * @param path Absolute file path in UTF-16.
	 * @return true if file exists and is not a directory.
	 * @return false otherwise.
	 *
	 * @example
	 * @code{.cpp}
	 * if (fs::fileExists(L"C:\\file.txt")) {
	 *     // File exists
	 * }
	 * @endcode
	 */
	inline bool fileExists( const std::wstring &path ) {
		DWORD attrib = GetFileAttributesW( path.c_str() );
		return ( attrib != INVALID_FILE_ATTRIBUTES && !( attrib & FILE_ATTRIBUTE_DIRECTORY ) );
	}



	/**
	 * @brief Save binary data to file
	 * @param filePath Target file path (UTF-16)
	 * @param data Binary data to save
	 * @param size Size of data in bytes
	 * @return true on success, false on error
	 */
	inline bool SaveToFile( const std::wstring &filePath, const BYTE *data, size_t size ) {
		if ( filePath.empty() || !data || size == 0 )
			return false;

		HANDLE hFile = CreateFileW( filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

		if ( hFile == INVALID_HANDLE_VALUE )
			return false;

		DWORD bytesWritten = 0;
		BOOL success = WriteFile( hFile, data, static_cast<DWORD>( size ), &bytesWritten, NULL );

		CloseHandle( hFile );

		return ( success && bytesWritten == size );
	}



	/**
	 * @brief Create directories recursively
	 * @param path Directory path to create (UTF-16)
	 * @return true on success, false on error
	 */
	inline bool CreateDirectoriesRecursive( const std::wstring &path ) {
		if ( path.empty() )
			return false;

		// Extract directory from full path
		size_t lastSlash = path.find_last_of( L"\\/" );
		if ( lastSlash == std::wstring::npos )
			return true; // No directory to create

		std::wstring dirPath = path.substr( 0, lastSlash );
		if ( dirPath.empty() )
			return true;

		// Check if directory already exists
		DWORD attrs = GetFileAttributesW( dirPath.c_str() );
		if ( attrs != INVALID_FILE_ATTRIBUTES && ( attrs & FILE_ATTRIBUTE_DIRECTORY ) )
			return true;

		// Create parent directory first
		size_t parentSlash = dirPath.find_last_of( L"\\/" );
		if ( parentSlash != std::wstring::npos ) {
			std::wstring parentPath = dirPath.substr( 0, parentSlash );
			if ( !CreateDirectoriesRecursive( parentPath ) )
				return false;
		}

		// Create this directory
		return CreateDirectoryW( dirPath.c_str(), NULL ) || GetLastError() == ERROR_ALREADY_EXISTS;
	}



/**
 * @brief Reads a text file into a UTF-8 string.
 *
 * Handles UTF-8 BOM stripping automatically.
 * Rejects files larger than 10 MB as a safety guard.
 *
 * @param path  Absolute file path (UTF-16).
 * @param out   Output string in UTF-8.
 * @return true on success, false on error.
 */
inline bool ReadTextFile( const std::wstring &path, std::string &out ) {
    HANDLE hFile = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( hFile == INVALID_HANDLE_VALUE )
        return false;

    LARGE_INTEGER size;
    if ( !GetFileSizeEx( hFile, &size ) || size.QuadPart > 10 * 1024 * 1024 ) {
        CloseHandle( hFile );
        return false;
    }

    out.resize( static_cast<size_t>( size.QuadPart ) );
    DWORD bytesRead = 0;
    BOOL  ok        = ::ReadFile( hFile, out.data(),
                                  static_cast<DWORD>( size.QuadPart ),
                                  &bytesRead, nullptr );
    CloseHandle( hFile );

    if ( !ok || bytesRead != static_cast<DWORD>( size.QuadPart ) ) {
        out.clear();
        return false;
    }

    // Strip UTF-8 BOM if present
    if ( out.size() >= 3 &&
         (unsigned char)out[0] == 0xEF &&
         (unsigned char)out[1] == 0xBB &&
         (unsigned char)out[2] == 0xBF )
        out.erase( 0, 3 );

    return true;
}
} // namespace fs
