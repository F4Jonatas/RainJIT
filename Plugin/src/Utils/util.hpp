/**
 * @file util.hpp
 * @brief General utilities for RainJIT plugin.
 * @license GPL v2.0 License
 *
 * @namespace util
 * @brief Helper functions for Lua, files, and common operations.
 *
 * This namespace contains utility functions used throughout the RainJIT plugin
 * for common tasks like Lua script execution, file operations, logging,
 * and Rainmeter API interactions.
 */

#pragma once

#include <string>
#include <algorithm>
#include <Windows.h>


#include "strings.hpp"
#include <RainmeterAPI.hpp>




namespace util {

	/**
	* @brief Detects whether a string likely represents a Rainmeter formula.
	*
	* This function performs a lightweight heuristic check to determine if
	* a given string should be interpreted as a Rainmeter mathematical formula.
	* It is primarily used before invoking the Rainmeter formula parser
	* (e.g., via a temporary option and `RmReadFormulaFromSection`).
	*
	* @details
	* The detection algorithm:
	*
	* 1. Trims leading and trailing whitespace characters.
	* 2. Returns **true** if the trimmed string is enclosed in parentheses:
	*    `( ... )`.
	* 3. Otherwise checks if the string contains any Rainmeter math operators:
	*    `+ - * / % ^`.
	*
	* This heuristic covers the majority of Rainmeter formula patterns such as:
	*
	* - `(2+3)`
	* - `(#Var# * 5)`
	* - `10 + 20`
	*
	* The function does **not guarantee** that the expression is a valid
	* Rainmeter formula; it only detects patterns commonly used for formulas.
	*
	* @param[in] v Input string to test.
	*
	* @retval true  The string likely represents a Rainmeter formula.
	* @retval false The string likely represents a literal value.
	*
	* @note
	* Rainmeter variables are stored as strings and may contain literal
	* values, expressions, or references to other variables. This function
	* helps decide whether the expression should be evaluated by the
	* Rainmeter formula engine.
	*
	* @warning
	* This function uses heuristics and may produce false positives in rare
	* cases where a literal string contains math operators.
	*
	* @example
	* @code{.cpp}
	* std::wstring value = L"(#Radius# * 2)";
	*
	* if (util::isRainmeterFormula(value))
	* {
	*     // Evaluate using Rainmeter formula engine
	* }
	* @endcode
	*/
	inline bool isRainmeterFormula( const std::wstring& v ) {
		const wchar_t* ws = L" \t\n\r\f\v";

		size_t start = v.find_first_not_of(ws);
		if (start == std::wstring::npos)
			return false;

		size_t end = v.find_last_not_of(ws);

		if (end - start + 1 >= 3 && v[start] == L'(' && v[end] == L')')
			return true;

		return v.find_first_of(L"+-*/%^", start) != std::wstring::npos;
	}



	/**
	* @brief Get Rainmeter variable as integer.
	*
	* Retrieves Rainmeter variable and converts to Lua integer.
	* Uses RmReplaceVariables internally.
	*
	* @param rain Measure instance.
	* @param var Variable name (UTF-16).
	* @param[out] out Result integer.
	* @return true if variable exists and is numeric.
	* @return false otherwise.
	*
	* @example
	* @code{.cpp}
	* lua_Integer value;
	* if (util::RmVarInt(rain, L"#MyVar#", value)) {
	*     // Use value
	* }
	* @endcode
	*/
	template<typename TRain>
	inline bool RmVarInt(TRain* rain, LPCWSTR var, lua_Integer& out) {
		LPCWSTR value = RmReplaceVariables(rain->rm, var);
		if (!value || wcscmp(value, var) == 0) {
			return false;
		}

		try {
			out = (lua_Integer)std::stoll(value);
			return true;
		}
		catch (...) {
			return false;
		}
	}



	/**
	* @brief Canonicalize Windows path.
	*
	* Converts relative path to absolute and normalizes (removes .., .).
	* Uses GetFullPathNameW internally.
	*
	* @param path Input path (UTF-16).
	* @return Canonicalized absolute path or empty string on error.
	*
	* @note Uses GetFullPathNameW internally.
	*
	* @example
	* @code{.cpp}
	* std::wstring abs = util::CanonicalizePath(L"..\\file.txt");
	* // Returns "C:\\full\\path\\file.txt"
	* @endcode
	*/
	inline std::wstring CanonicalizePath(const std::wstring& path) {
		if (path.empty())
			return {};

		DWORD required = GetFullPathNameW(
			path.c_str(),
			0,
			nullptr,
			nullptr
		);

		if (required == 0)
			return {};

		std::wstring buffer(required, L'\0');

		DWORD len = GetFullPathNameW(
			path.c_str(),
			required,
			buffer.data(),
			nullptr
		);

		if (len == 0)
			return {};

		buffer.resize(len);
		return buffer;
	}



	/**
	 * @brief Checks whether a string is a valid Rainmeter variable identifier.
	 *
	 * Valid identifiers contain only ASCII letters, digits, or underscore.
	 */
	constexpr inline bool IsValidRainmeterVar(const std::wstring& name) {
		if (name.empty())
			return false;

		return std::all_of( name.begin(), name.end(), []( wchar_t ch ) {
			return (ch == L'@')
				|| (ch >= L'a' && ch <= L'z')
				|| (ch >= L'A' && ch <= L'Z')
				|| (ch >= L'0' && ch <= L'9');
		});
	}
} // namespace util
