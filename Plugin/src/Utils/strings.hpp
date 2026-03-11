/**
 * @file strings.hpp
 * @brief Encoding and string manipulation utilities.
 * @license GPL v2.0 License
 *
 * Provides functions for conversion between different encodings:
 * - UTF-8 ↔ UTF-16 (for Lua/Windows interoperability)
 * - Automatic encoding detection
 * - String normalization to avoid BOM issues
 *
 * @details
 * All functions are thread-safe and designed not to throw exceptions.
 * Used throughout the plugin to bridge Lua's UTF-8 strings with
 * Windows' UTF-16 API expectations.
 *
 * @note The encoding detection logic is specifically tuned for
 *       Rainmeter's behavior with different file encodings.
 */

#pragma once

#include <string>
#include <codecvt>
#include <locale>
#include <Windows.h>



/**
 * @brief Convert UTF-8 string to UTF-16 (std::wstring).
 *
 * Used mainly to convert messages returned by Lua (errors, script strings)
 * to the format expected by Rainmeter API.
 *
 * @param str String in UTF-8.
 * @return String converted to UTF-16, or empty string on error.
 *
 * @throws Never throws exceptions.
 *
 * @example
 * @code{.cpp}
 * std::wstring wide = utf8_to_wstring("Café");  // L"Café"
 * @endcode
 */
inline std::wstring utf8_to_wstring(const std::string& str) {
	if (str.empty())
		return L"";

	int wide_len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (wide_len == 0)
		return L"";

	std::wstring wide_str(wide_len, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wide_str[0], wide_len);

	// Remove NULL terminator added by Windows API
	wide_str.pop_back();
	return wide_str;
}



/**
 * @brief Convert UTF-16 string to UTF-8 (std::string).
 *
 * Used to convert Windows API strings to Lua-compatible UTF-8.
 *
 * @param wide_str String encoded in UTF-16.
 * @return String converted to UTF-8, or empty string on error.
 *
 * @throws Never throws exceptions.
 *
 * @example
 * @code{.cpp}
 * std::string utf8 = wstring_to_utf8(L"Café");  // "Café"
 * @endcode
 */
inline std::string wstring_to_utf8(const std::wstring& wstr) {
	if (wstr.empty())
		return "";

	int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);

	if (utf8_len == 0)
		return "";

	std::string utf8_str(utf8_len, 0);

	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_str[0], utf8_len, nullptr, nullptr);

	// Remove NULL character added by Windows API
	utf8_str.pop_back();
	return utf8_str;
}



/**
 * @brief Detect and convert text content to valid UTF-8 robustly.
 *
 * Attempts to normalize input string (received as UTF-16) to valid UTF-8,
 * handling common scenarios in Rainmeter + Windows ecosystem.
 *
 * Cases handled explicitly:
 *
 * 1. **Pure ASCII**
 *    - When all characters are below 128
 *    - Direct conversion, no additional cost
 *
 * 2. **UTF-8 incorrectly stored in wchar_t**
 *    - Common situation when skin is saved as UTF-8,
 *      but Rainmeter interprets bytes as UTF-16
 *    - Function reconstructs byte sequence and validates
 *      if result is valid UTF-8
 *
 * 3. **Legitimate UTF-16**
 *    - Standard fallback using UTF-16 → UTF-8 conversion
 *
 * 4. **BOM (Byte Order Mark)**: Automatic removal
 *
 * This function is used mainly for:
 * - Normalizing Lua scripts before execution via LuaJIT
 * - Avoiding parsing errors caused by incorrect encoding
 * - Ensuring compatibility with files saved in ANSI,
 *   UTF-8 (with or without BOM) or UTF-16
 *
 * @param wstr Input string in UTF-16.
 * @return Content converted to valid UTF-8.
 *
 * @note This function doesn't throw exceptions and always returns
 *       a valid string (possibly empty).
 *
 * @warning This function assumes input comes from Rainmeter and may
 *          contain misinterpreted data. Should not be used as
 *          generic converter outside this context.
 *
 * @example
 * @code{.cpp}
 * // Skin saved as UTF-8, read as UTF-16
 * std::wstring bad = L"\x00E9\x00E7\x0061";  // "éça" misinterpreted
 * std::string good = DetectAndConvertToUTF8(bad);  // "éça" correct
 * @endcode
 */
inline std::string DetectAndConvertToUTF8(const std::wstring& wstr) {
	if ( wstr.empty() )
		return "";

	// Case 1: Pure ASCII
	bool isAscii = true;
	for (wchar_t ch : wstr) {
		if (ch >= 128) {
			isAscii = false;
			break;
		}
	}

	if (isAscii) {
		std::string result;
		result.reserve(wstr.length());
		for (wchar_t ch : wstr)
			result.push_back(static_cast<char>(ch));

		return result;
	}

	// Case 2: UTF-8 incorrectly stored in wchar_t
	bool mightBeUtf8InWide = true;
	for (size_t i = 0; i < wstr.length(); ++i) {
		if (wstr[i] == 0) {
			mightBeUtf8InWide = false;
			break;
		}
	}

	if (mightBeUtf8InWide) {
		std::string result;
		result.reserve(wstr.length());

		for (size_t i = 0; i < wstr.length(); ++i) {
			wchar_t ch = wstr[i];
			if ((ch & 0xFF00) != 0) {
				result.push_back(static_cast<char>((ch >> 8) & 0xFF));
				result.push_back(static_cast<char>(ch & 0xFF));
			}
			else
				result.push_back(static_cast<char>(ch & 0xFF));
		}

		// UTF-8 validation
		bool validUtf8 = true;
		for (size_t i = 0; i < result.length(); ++i) {
			unsigned char c = static_cast<unsigned char>(result[i]);
			if (c >= 128) {
				if ((c & 0xE0) == 0xC0) {
					if (i + 1 >= result.length() || (result[i + 1] & 0xC0) != 0x80) {
						validUtf8 = false;
						break;
					}
					i++;
				}
				else if ((c & 0xF0) == 0xE0) {
					if ( i + 2 >= result.length()
					|| ( result[i + 1] & 0xC0) != 0x80
					|| ( result[i + 2] & 0xC0) != 0x80 ) {
						validUtf8 = false;
						break;
					}
					i += 2;
				}

				else if ((c & 0xF8) == 0xF0) {
					if ( i + 3 >= result.length()
					|| ( result[i + 1] & 0xC0 ) != 0x80
					|| ( result[i + 2] & 0xC0 ) != 0x80
					|| ( result[i + 3] & 0xC0 ) != 0x80 ) {
						validUtf8 = false;
						break;
					}
					i += 3;
				}
				else {
					validUtf8 = false;
					break;
				}
			}
		}

		if (validUtf8)
			return result;
	}

	// Case 3: Standard UTF-16 → UTF-8 conversion
	return wstring_to_utf8(wstr);
}



namespace string {
	/**
	* @brief Convert string to uppercase (returns new string).
	*
	* Uses WinAPI LCMapString for proper locale-aware uppercase conversion.
	* Creates a copy of the input string and converts it to uppercase.
	* Original string remains unchanged.
	*
	* @param str Input string to convert.
	* @return New string converted to uppercase.
	*
	* @example
	* @code{.cpp}
	* std::wstring upper = ToUpperCase(L"Hello");  // L"HELLO"
	* @endcode
	*/
	inline std::wstring ToUpperCase(const std::wstring& str) {
		if (str.empty()) return L"";

		std::wstring result = str;
		WCHAR* srcAndDest = &result[0];
		int strAndDestLen = static_cast<int>(result.length());
		LCMapString(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, srcAndDest, strAndDestLen, srcAndDest, strAndDestLen);
		return result;
	}



	/**
	* @brief Convert string to lowercase (returns new string).
	*
	* Uses WinAPI LCMapString for proper locale-aware lowercase conversion.
	* Creates a copy of the input string and converts it to lowercase.
	* Original string remains unchanged.
	*
	* @param str Input string to convert.
	* @return New string converted to lowercase.
	*
	* @example
	* @code{.cpp}
	* std::wstring lower = ToLowerCase(L"HELLO");  // L"hello"
	* @endcode
	*/
	inline std::wstring ToLowerCase(const std::wstring& str) {
		if (str.empty())
			return L"";

		std::wstring result = str;
		WCHAR* srcAndDest = &result[0];
		int strAndDestLen = static_cast<int>(result.length());
		LCMapString(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, srcAndDest, strAndDestLen, srcAndDest, strAndDestLen);
		return result;
	}



	/**
	* @brief Convert UTF-8 string to uppercase.
	*
	* Converts UTF-8 string to uppercase via UTF-16 intermediate.
	*
	* @param str UTF-8 input string.
	* @return Uppercase UTF-8 string.
	*
	* @example
	* @code{.cpp}
	* std::string upper = ToUpperCase("café");  // "CAFÉ"
	* @endcode
	*/
	inline std::string ToUpperCase(const std::string& str) {
		if (str.empty())
			return "";

		std::wstring wide = utf8_to_wstring(str);
		if (wide.empty())
			return "";

		std::wstring result = wide;
		WCHAR* srcAndDest = &result[0];
		int strAndDestLen = static_cast<int>(result.length());
		LCMapString(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, srcAndDest, strAndDestLen, srcAndDest, strAndDestLen);
		return wstring_to_utf8(result);
	}



	/**
	* @brief Convert UTF-8 string to lowercase.
	*
	* Converts UTF-8 string to lowercase via UTF-16 intermediate.
	*
	* @param str UTF-8 input string.
	* @return Lowercase UTF-8 string.
	*
	* @example
	* @code{.cpp}
	* std::string lower = ToLowerCase("CAFÉ");  // "café"
	* @endcode
	*/
	inline std::string ToLowerCase(const std::string& str) {
		if (str.empty())
			return "";

		std::wstring wide = utf8_to_wstring(str);
		if (wide.empty())
			return "";

		std::wstring result = wide;
		WCHAR* srcAndDest = &result[0];
		int strAndDestLen = static_cast<int>(result.length());
		LCMapString(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, srcAndDest, strAndDestLen, srcAndDest, strAndDestLen);
		return wstring_to_utf8(result);
	}
}
