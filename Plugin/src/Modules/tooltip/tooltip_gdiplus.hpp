/**
 * @file tooltip_gdiplus.hpp
 * @brief Safe umbrella include for GDI+.
 * @license GPL v2.0 License
 *
 * @details
 * GDI+ headers rely on the `min` / `max` names, which collide with the
 * Windows macros (or with NOMINMAX builds). This header isolates that
 * problem in a single place so the rest of the module can simply include it.
 *
 * Only translation units that really draw or load bitmaps should include
 * this header. Public headers forward-declare `Gdiplus::Bitmap` instead.
 */

#pragma once

#include <Windows.h>
#include <objidl.h>

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max

#include <algorithm>

namespace Gdiplus {
	using std::max;
	using std::min;
} // namespace Gdiplus

#include <gdiplus.h>

#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

#pragma comment( lib, "gdiplus.lib" )
