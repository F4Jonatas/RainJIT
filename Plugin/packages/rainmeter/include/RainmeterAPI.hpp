/* Copyright (C) 2011 Rainmeter Project Developers
 *
 * This Source Code Form is subject to the terms of the GNU General Public
 * License; either version 2 of the License, or (at your option) any later
 * version. If a copy of the GPL was not distributed with this file, You can
 * obtain one at <https://www.gnu.org/licenses/gpl-2.0.html>.
 */

#ifndef __RAINMETERAPI_H__
#define __RAINMETERAPI_H__

#ifdef LIBRARY_EXPORTS
	#define LIBRARY_EXPORT EXTERN_C
#else
	#define LIBRARY_EXPORT EXTERN_C __declspec( dllimport )
#endif // LIBRARY_EXPORTS

#define PLUGIN_EXPORT EXTERN_C __declspec( dllexport )



enum RmGetType {
	RMG_MEASURENAME = 0,
	RMG_SKIN = 1,
	RMG_SETTINGSFILE = 2,
	RMG_SKINNAME = 3,
	RMG_SKINWINDOWHANDLE = 4
};



enum LOGLEVEL {
	LOG_ERROR = 1,
	LOG_WARNING = 2,
	LOG_NOTICE = 3,
	LOG_DEBUG = 4
};



/// Exported functions

/**
 * @brief Retrieves an option of the plugin script measure.
 *
 * @param rm Pointer to the plugin measure.
 * @param option Option name.
 * @param defValue Default value for the option if it is not found or invalid.
 * @param replaceMeasures If true, replaces section variables in the returned string.
 * @return Returns the option value as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     LPCWSTR value = RmReadString(rm, L"Value", L"DefaultValue");
 * }
 * @endcode
 */
LIBRARY_EXPORT LPCWSTR __stdcall RmReadString( void *rm, LPCWSTR option, LPCWSTR defValue, BOOL replaceMeasures = TRUE );



/**
 * @brief Retrieves an option of a meter/measure.
 *
 * @remarks In older Rainmeter versions without support for this API, always returns the default value.
 *
 * @param rm Pointer to the plugin measure.
 * @param section Meter/measure section name.
 * @param option Option name.
 * @param defValue Default value for the option if it is not found or invalid.
 * @param replaceMeasures If true, replaces section variables in the returned string.
 * @return Returns the option value as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     LPCWSTR value = RmReadStringFromSection(rm, L"MySection", L"Value", L"DefaultValue");
 * }
 * @endcode
 */
#ifdef LIBRARY_EXPORTS

LIBRARY_EXPORT LPCWSTR __stdcall RmReadStringFromSection( void *rm, LPCWSTR section, LPCWSTR option, LPCWSTR defValue, BOOL replaceMeasures = TRUE );


#else

inline LPCWSTR RmReadStringFromSection( void *rm, LPCWSTR section, LPCWSTR option, LPCWSTR defValue, BOOL replaceMeasures = TRUE ) {
	typedef LPCWSTR( __stdcall * RmReadStringFromSectionFunc )( void *, LPCWSTR, LPCWSTR, LPCWSTR, BOOL );
	static auto delayedFunc = (RmReadStringFromSectionFunc)GetProcAddress( GetModuleHandle( L"Rainmeter.dll" ), "RmReadStringFromSection" );

	if ( delayedFunc )
		return delayedFunc( rm, section, option, defValue, replaceMeasures );

	return defValue;
}

#endif



/**
 * @brief Retrieves an option of the plugin script measure as a number after parsing a possible formula.
 *
 * @param rm Pointer to the plugin measure.
 * @param option Option name.
 * @param defValue Default value for the option if it is not found, invalid, or a formula could not be parsed.
 * @return Returns the option value as a double.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     double value = RmReadFormula(rm, L"Value", 20);
 * }
 * @endcode
 */
LIBRARY_EXPORT double __stdcall RmReadFormula( void *rm, LPCWSTR option, double defValue );



/**
 * @brief Retrieves an option of a meter/measure as a number after parsing a possible formula.
 *
 * @remarks In older Rainmeter versions without support for this API, always returns the default value.
 *
 * @param rm Pointer to the plugin measure.
 * @param section Meter/measure section name.
 * @param option Option name.
 * @param defValue Default value for the option if it is not found, invalid, or a formula could not be parsed.
 * @return Returns the option value as a double.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     double value = RmReadFormulaFromSection(rm, L"MySection", L"Value", 20);
 * }
 * @endcode
 */
#ifdef LIBRARY_EXPORTS

LIBRARY_EXPORT double __stdcall RmReadFormulaFromSection( void *rm, LPCWSTR section, LPCWSTR option, double defValue );


#else

inline double RmReadFormulaFromSection( void *rm, LPCWSTR section, LPCWSTR option, double defValue ) {
	typedef double( __stdcall * RmReadFormulaFromSectionFunc )( void *, LPCWSTR, LPCWSTR, double );
	static auto delayedFunc = (RmReadFormulaFromSectionFunc)GetProcAddress( GetModuleHandle( L"Rainmeter.dll" ), "RmReadFormulaFromSection" );

	if ( delayedFunc )
		return delayedFunc( rm, section, option, defValue );

	return defValue;
}


#endif



/**
 * @brief Retrieves the option defined in a section and converts it to an integer.
 *
 * @remarks If the option is a formula, the returned value will be the result of the parsed formula.
 *
 * @param rm Pointer to the plugin measure.
 * @param section Meter/measure section name.
 * @param option Option name.
 * @param defValue Default value if the option is not found or invalid.
 * @return Returns the option value as an integer.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     int value = RmReadIntFromSection(rm, L"Section", L"Option", 20);
 * }
 * @endcode
 */
__inline int RmReadIntFromSection( void *rm, LPCWSTR section, LPCWSTR option, int defValue ) {
	return (int)RmReadFormulaFromSection( rm, section, option, defValue );
}



/**
 * @brief Retrieves the option defined in a section and converts it to a double.
 *
 * @remarks If the option is a formula, the returned value will be the result of the parsed formula.
 *
 * @param rm Pointer to the plugin measure.
 * @param section Meter/measure section name.
 * @param option Option name.
 * @param defValue Default value if the option is not found or invalid.
 * @return Returns the option value as a double.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     double value = RmReadDoubleFromSection(rm, L"Section", L"Option", 20.0);
 * }
 * @endcode
 */
__inline double RmReadDoubleFromSection( void *rm, LPCWSTR section, LPCWSTR option, double defValue ) {
	return RmReadFormulaFromSection( rm, section, option, defValue );
}



/**
 * @brief Returns a string, replacing any variables (or section variables) within the inputted string.
 *
 * @param rm Pointer to the plugin measure.
 * @param str String with unresolved variables.
 * @return Returns a string replacing any variables in the 'str'.
 *
 * @code
 * PLUGIN_EXPORT double Update(void* data)
 * {
 *     Measure* measure = (Measure*)data;
 *     LPCWSTR myVar = RmReplaceVariables(measure->rm, L"#MyVar#");  // 'measure->rm' stored previously in the Initialize function
 *     if (_wcsicmp(myVar, L"SOMETHING") == 0) { return 1.0; }
 *     return 0.0;
 * }
 * @endcode
 */
LIBRARY_EXPORT LPCWSTR __stdcall RmReplaceVariables( void *rm, LPCWSTR str );



/**
 * @brief Converts a relative path to an absolute path (use RmReadPath where appropriate).
 *
 * @param rm Pointer to the plugin measure.
 * @param relativePath String of path to be converted.
 * @return Returns the absolute path of the relativePath value as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     std::wstring somePath = L"..\\SomeFolder";
 *     LPCWSTR path = RmPathToAbsolute(rm, somePath.c_str());
 * }
 * @endcode
 */
LIBRARY_EXPORT LPCWSTR __stdcall RmPathToAbsolute( void *rm, LPCWSTR relativePath );



/**
 * @brief Executes a command.
 *
 * @param skin Pointer to the current skin (See RmGetSkin).
 * @param command Bang to execute.
 * @return No return type.
 *
 * @code
 * PLUGIN_EXPORT double Update(void* data)
 * {
 *     Measure* measure = (Measure*)data;
 *     RmExecute(measure->skin, L"!SetVariable SomeVar 10");  // 'measure->skin' stored previously in the Initialize function
 *     return 0.0;
 * }
 * @endcode
 */
LIBRARY_EXPORT void __stdcall RmExecute( void *skin, LPCWSTR command );



/**
 * @brief Retrieves data from the measure or skin (use the helper functions instead).
 *
 * @remarks Call RmGet() in the Initialize function and store the results for later use.
 *
 * @param rm Pointer to the plugin measure.
 * @param type Type of information to retrieve (see RmGetType).
 * @return Returns a pointer to an object which is determined by the 'type'.
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     measure->hwnd = RmGet(rm, RMG_SKINWINDOWHANDLE);  // 'measure->hwnd' defined as HWND in class scope
 * }
 * @endcode
 */
LIBRARY_EXPORT void *__stdcall RmGet( void *rm, int type );



/**
 * @brief Sends a message to the Rainmeter log with source.
 *
 * @remarks LOG_DEBUG messages are logged only when Rainmeter is in debug mode.
 *
 * @param rm Pointer to the plugin measure.
 * @param type Log type (LOG_ERROR, LOG_WARNING, LOG_NOTICE, or LOG_DEBUG).
 * @param message Message to be logged.
 * @return No return type.
 *
 * @code
 * RmLog(rm, LOG_NOTICE, L"I am a 'notice' log message with a source");
 * @endcode
 */
LIBRARY_EXPORT void __stdcall RmLog( void *rm, int level, LPCWSTR message );



/**
 * @brief Sends a formatted message to the Rainmeter log.
 *
 * @remarks LOG_DEBUG messages are logged only when Rainmeter is in debug mode.
 *
 * @param rm Pointer to the plugin measure.
 * @param level Log level (LOG_ERROR, LOG_WARNING, LOG_NOTICE, or LOG_DEBUG).
 * @param format Formatted message to be logged, follows printf syntax.
 * @param ... Comma-separated list of args referenced in the formatted message.
 * @return No return type.
 *
 * @code
 * std::wstring notice = L"notice";
 * RmLogF(rm, LOG_NOTICE, L"I am a '%s' log message with a source", notice.c_str());
 * @endcode
 */
LIBRARY_EXPORT void __cdecl RmLogF( void *rm, int level, LPCWSTR format, ... );


/// @brief DEPRECATED: Use RmLog. Sends a message to the Rainmeter log.
LIBRARY_EXPORT BOOL __cdecl LSLog( int level, LPCWSTR unused, LPCWSTR message );





#ifndef LIBRARY_EXPORTS
/**
 * @brief Retrieves the option defined in the skin file and converts a relative path to an absolute path.
 *
 * @param rm Pointer to the plugin measure.
 * @param option Option name to be read from skin.
 * @param defValue Default value for the option if it is not found or invalid.
 * @return Returns the absolute path of the option value as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     LPCWSTR path = RmReadPath(rm, L"MyPath", L"C:\\");
 * }
 * @endcode
 */
__inline LPCWSTR RmReadPath( void *rm, LPCWSTR option, LPCWSTR defValue ) {
	LPCWSTR relativePath = RmReadString( rm, option, defValue, TRUE );
	return RmPathToAbsolute( rm, relativePath );
}



/**
 * @brief Retrieves the option defined in the skin file and converts it to an integer.
 *
 * @remarks If the option is a formula, the returned value will be the result of the parsed formula.
 *
 * @param rm Pointer to the plugin measure.
 * @param option Option name to be read from skin.
 * @param defValue Default value for the option if it is not found, invalid, or a formula could not be parsed.
 * @return Returns the option value as an integer.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     int value = RmReadInt(rm, L"Value", 20);
 * }
 * @endcode
 */
__inline int RmReadInt( void *rm, LPCWSTR option, int defValue ) {
	return (int)RmReadFormula( rm, option, defValue );
}



/**
 * @brief Retrieves the option defined in the skin file and converts it to a double.
 *
 * @remarks If the option is a formula, the returned value will be the result of the parsed formula.
 *
 * @param rm Pointer to the plugin measure.
 * @param option Option name to read from the skin.
 * @param defValue Default value for the option if it is not found, invalid, or a formula could not be parsed.
 * @return Returns the option value as a double.
 *
 * @code
 * PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
 * {
 *     double value = RmReadDouble(rm, L"Value", 20.0);
 * }
 * @endcode
 */
__inline double RmReadDouble( void *rm, LPCWSTR option, double defValue ) {
	return RmReadFormula( rm, option, defValue );
}



/**
 * @brief Retrieves the name of the measure.
 *
 * @remarks Call RmGetMeasureName() in the Initialize function and store the results for later use.
 *
 * @param rm Pointer to the plugin measure.
 * @return Returns the current measure name as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     measure->myName = RmGetMeasureName(rm);  // 'measure->myName' defined as a string (LPCWSTR) in class scope
 * }
 * @endcode
 */
__inline LPCWSTR RmGetMeasureName( void *rm ) {
	return (LPCWSTR)RmGet( rm, RMG_MEASURENAME );
}



/**
 * @brief Retrieves a path to the Rainmeter data file (Rainmeter.data).
 *
 * @remarks Call GetSettingsFile() in the Initialize function and store the results for later use.
 *
 * @return Returns the path and filename of the Rainmeter data file as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     if (rmDataFile == nullptr) { rmDataFile = RmGetSettingsFile(); }  // 'rmDataFile' defined as a string (LPCWSTR) in global scope
 * }
 * @endcode
 */
__inline LPCWSTR RmGetSettingsFile() {
	return (LPCWSTR)RmGet( NULL, RMG_SETTINGSFILE );
}



/**
 * @brief Retrieves an internal pointer to the current skin.
 *
 * @remarks Call GetSkin() in the Initialize function and store the results for later use.
 *
 * @param rm Pointer to the plugin measure.
 * @return Returns a pointer to the current skin.
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     measure->mySkin = RmGetSkin(rm);  // 'measure->mySkin' defined as a 'void*' in class scope
 * }
 * @endcode
 */
__inline void *RmGetSkin( void *rm ) {
	return (void *)RmGet( rm, RMG_SKIN );
}



/**
 * @brief Retrieves the full path and name of the skin.
 *
 * @remarks Call GetSkinName() in the Initialize function and store the results for later use.
 *
 * @param rm Pointer to the plugin measure.
 * @return Returns the path and filename of the skin as a string (LPCWSTR).
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     measure->skinName = RmGetSkinName(rm);  // 'measure->skinName' defined as a string (LPCWSTR) in class scope
 * }
 * @endcode
 */
__inline LPCWSTR RmGetSkinName( void *rm ) {
	return (LPCWSTR)RmGet( rm, RMG_SKINNAME );
}



/**
 * @brief Returns a pointer to the handle of the skin window (HWND).
 *
 * @remarks Call GetSkinWindow() in the Initialize function and store the results for later use.
 *
 * @param rm Pointer to the plugin measure.
 * @return Returns a handle to the skin window as a HWND.
 *
 * @code
 * PLUGIN_EXPORT void Initialize(void** data, void* rm)
 * {
 *     Measure* measure = new Measure;
 *     *data = measure;
 *     measure->skinWindow = RmGetSkinWindow(rm);  // 'measure->skinWindow' defined as HWND in class scope
 * }
 * @endcode
 */
__inline HWND RmGetSkinWindow( void *rm ) {
	return (HWND)RmGet( rm, RMG_SKINWINDOWHANDLE );
}



/// @brief DEPRECATED: Use RmLog(rm, type, message). Sends a message to the Rainmeter log.
__inline void RmLog( int level, LPCWSTR message ) {
	LSLog( level, NULL, message );
}


#endif // LIBRARY_EXPORTS
#endif
