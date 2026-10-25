#pragma once

#ifndef GUMBO_STRINGS_H
	#define GUMBO_STRINGS_H

/*
 * Windows compatibility shim for POSIX strings.h
 */

	#include <string.h>

	#ifdef _MSC_VER

		#define strcasecmp _stricmp
		#define strncasecmp _strnicmp

	#endif

#endif
