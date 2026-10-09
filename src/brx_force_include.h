#ifndef BRX_FORCE_INCLUDE_H
#define BRX_FORCE_INCLUDE_H

// Wrapper header for forced inclusion of BricsCAD platform headers
// This must be the FIRST header included in all translation units

// CRITICAL: Include windows.h FIRST to ensure all Windows types are defined
#ifndef _WINDOWS_
#include <windows.h>
#endif

// Now include BricsCAD platform header
#include "brx_platform_windows.h"

#endif // BRX_FORCE_INCLUDE_H
