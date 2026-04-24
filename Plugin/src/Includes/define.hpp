

#pragma once

#include <Windows.h>
#include <string>


#ifndef DOCHOSTUIFLAG_SILENT
	#define DOCHOSTUIFLAG_SILENT 0x00040000
#endif



// Níveis de log compatíveis com Rainmeter
#ifndef LOG_DEBUG
	#define LOG_DEBUG 0
	#define LOG_INFO 1
	#define LOG_WARNING 2
	#define LOG_ERROR 3
#endif


// Converte nível de log para string (usado no fallback)
inline const char *LogLevelToString( int level ) {
	switch ( level ) {
	case LOG_DEBUG:
		return "DEBUG";
	case LOG_INFO:
		return "INFO";
	case LOG_WARNING:
		return "WARNING";
	case LOG_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

#ifdef __RAINMETERAPI_H__
	#define RN_LOG( rainPtr, level, msg ) \
		if ( ( rainPtr ) && ( rainPtr )->rm ) \
		RmLog( ( rainPtr )->rm, level, msg )

#else
	#define RN_LOG( rainPtr, level, msg ) \
		do { \
			std::string _prefix = "[RainJIT][" + std::string( LogLevelToString( level ) ) + "] "; \
			std::string _full = _prefix + ( msg ); \
			OutputDebugStringA( _full.c_str() ); \
		} while ( 0 )

#endif
