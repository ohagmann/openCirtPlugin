// AUTO-GENERATED: Qt 6.8+ Windows SDK Fix
// This header is FORCE-INCLUDED (/FI) before EVERY compilation
#ifndef WINDOWS_FIX_H_INCLUDED
#define WINDOWS_FIX_H_INCLUDED

#ifdef _WIN32

// Windows Version (Windows 7+)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WINVER
#define WINVER 0x0601
#endif

// CRITICAL: Prevent Qt from stripping Windows headers
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

// Critical defines
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32 1
#define _WINDOWS 1

// Load FULL Windows headers - NO WIN32_LEAN_AND_MEAN
#ifndef _WINDOWS_
#include <windows.h>
#endif

// BLOCK any future attempts to strip Windows types
#define WIN32_LEAN_AND_MEAN __DO_NOT_DEFINE_THIS__

// Load intrinsics for SIMD
#ifndef _INTRIN_H_
#include <intrin.h>
#endif

// Load AVX/SSE
#ifndef _IMMINTRIN_H_INCLUDED
#include <immintrin.h>
#endif

// NOTE: Do NOT include brx_platform_windows.h here!
// It will be included later in source files after include paths are set.

#endif // _WIN32

#endif // WINDOWS_FIX_H_INCLUDED
