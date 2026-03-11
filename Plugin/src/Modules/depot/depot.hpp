/**
 * @file depot.hpp
 * @brief INI file accessor bound to a fixed section.
 * @license GPL v2.0 License
 *
 * Provides a safe and modern C++ wrapper over WinAPI profile functions,
 * scoped to a single section and file.
 *
 * @details
 * - All strings exposed to Lua are UTF-8
 * - Internally, WinAPI calls use UTF-16
 * - Section and file path are fixed at construction
 * - Null values delete keys, nullptr values remove keys
 *
 * @example
 * @code
 * Depot depot("MySection", "config.ini");
 * depot.set("Key", "Value");
 * @endcode
 */

#pragma once

#include <Windows.h>
#include <filesystem>
#include <optional>
#include <string>

#include <utils/strings.hpp>

struct lua_State;
struct Rain;





/**
 * @struct Depot
 * @brief INI file accessor bound to a fixed section.
 *
 * This struct provides a RAII-style wrapper around Windows INI file APIs,
 * with automatic UTF-8 to UTF-16 conversion for Lua compatibility.
 */
struct Depot {

	/// @brief Section name (UTF-8, logical).
	std::string Section;

	/// @brief Section name cached as UTF-16 for WinAPI calls.
	std::wstring SectionW;

	/// @brief INI file path as UTF-16 for WinAPI calls.
	std::wstring FilePathW;

	/// @brief INI file path as filesystem path.
	std::filesystem::path FilePath;



	/**
	 * @brief Constructs a Depot bound to a section and file.
	 *
	 * @param section Section name (UTF-8).
	 * @param filePath INI file path (UTF-8).
	 *
	 * @post Section and SectionW are initialized with UTF-16 equivalents.
	 */
	explicit Depot( std::string section, std::string filePath ) :
		Section( section ),
		SectionW( utf8_to_wstring( section ) ),
		FilePathW( utf8_to_wstring( filePath ) ),
		FilePath( std::filesystem::path( FilePathW ) ) {
	}



	/**
	 * @brief Writes or deletes a string value in the INI file.
	 *
	 * Behavior:
	 * - value.has_value() == false -> deletes the key
	 * - value == ""               -> writes empty string
	 * - otherwise                 -> writes value
	 *
	 * @param key Key name (UTF-8).
	 * @param value Optional value (UTF-8). nullopt deletes the key.
	 */
	void set( const std::string &key, const std::optional<std::string> &value ) const noexcept {
		const std::wstring wkey = utf8_to_wstring( key );

		const wchar_t *value_ptr = nullptr;
		std::wstring storage;

		if ( value.has_value() ) {

			if ( value->empty() )
				value_ptr = L"";

			else {
				storage = utf8_to_wstring( *value );
				value_ptr = storage.c_str();
			}
		}

		WritePrivateProfileStringW( SectionW.c_str(), key.empty() ? nullptr : wkey.c_str(), value_ptr, FilePath.c_str() );
	}



	/**
	 * @brief Reads a string value from the INI file.
	 *
	 * @param key Key name (UTF-8).
	 * @param defaultValue Optional default value (UTF-8) if key is not found.
	 * @return Value as UTF-8 string, or empty string if not found.
	 */
	std::string get( std::string key, std::optional<std::string> defaultValue = std::nullopt ) const noexcept {
		wchar_t buffer[4096]{};

		std::wstring wkey = utf8_to_wstring( key );
		std::wstring wdef;

		if ( defaultValue )
			wdef = utf8_to_wstring( *defaultValue );

		// clang-format off
		GetPrivateProfileStringW(
			SectionW.c_str(),
			wkey.c_str(),
			defaultValue ? wdef.c_str() : L"",
			buffer,
			_countof( buffer ),
			FilePath.c_str()
		);
		// clang-format on

		return wstring_to_utf8( buffer );
	}



	/**
	 * @brief Reads a string value from the INI file with optional result.
	 *
	 * @param key Key name (UTF-8).
	 * @return Optional value as UTF-8 string.
	 */
	std::optional<std::string> getOptional( std::string key ) const noexcept {
		wchar_t buffer[4096]{};

		std::wstring wkey = utf8_to_wstring( key );

		// clang-format off
		DWORD read = GetPrivateProfileStringW(
			SectionW.c_str(),
			wkey.c_str(),
			L"",
			buffer,
			_countof( buffer ),
			FilePath.c_str()
		);
		// clang-format on

		if ( read == 0 )
			return std::nullopt;

		return wstring_to_utf8( buffer );
	}



	/**
	 * @brief Deletes a key from the section.
	 *
	 * @param key Key name (UTF-8) to delete.
	 */
	void delKey( std::string key ) const noexcept {
		set( key, std::nullopt );
	}



	/**
	 * @brief Deletes the entire section.
	 *
	 * Removes all keys under the bound section from the INI file.
	 */
	void delSection() const noexcept {
		// clang-format off
		WritePrivateProfileStringW(
			SectionW.c_str(),
			nullptr,
			nullptr,
			FilePath.c_str()
		);
		// clang-format on
	}



	/**
	 * @brief Checks if a key exists in the section.
	 *
	 * @param key Key name (UTF-8).
	 * @return true if key exists, false otherwise.
	 */
	bool hasKey( std::string key ) const noexcept {
		wchar_t buffer[2]{};

		std::wstring wkey = utf8_to_wstring( key );

		// clang-format off
		DWORD read = GetPrivateProfileStringW(
			SectionW.c_str(),
			wkey.c_str(),
			L"",
			buffer,
			_countof( buffer ),
			FilePath.c_str()
		);
		// clang-format on

		return read > 0;
	}



	/**
	 * @brief Gets the file path as UTF-8 string.
	 *
	 * @return File path in UTF-8.
	 */
	std::string getFilePath() const noexcept {
		return wstring_to_utf8( FilePathW );
	}



	/**
	 * @brief Gets the file path as UTF-16 string.
	 *
	 * @return File path in UTF-16.
	 */
	const std::wstring &getFilePathW() const noexcept {
		return FilePathW;
	}
};



/**
 * @brief Lua module entry point for Depot.
 *
 * Called when Lua executes `require("depot")`.
 *
 * @param L Lua state.
 * @return 1 (module table with __call metamethod).
 *
 * @note Requires a Measure pointer as upvalue 1.
 */
extern "C" int luaopen_depot( lua_State *L );



/**
 * @brief Register depot Lua module in package.preload.
 *
 * Allows embedded depot module to be loaded via `require()` in Lua,
 * without depending on external files.
 *
 * @param L Lua state.
 * @param rain Pointer to the Measure instance (passed as upvalue).
 */
void RegisterDepotModule( lua_State *L, Rain *rain );
