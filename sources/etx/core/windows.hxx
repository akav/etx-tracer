#pragma once

#include <etx/core/platform.hxx>

#if defined(ETX_PLATFORM_WINDOWS)

#if !defined(NOMINMAX)
#error NOMINMAX should be defined
#endif

#if !defined(_CRT_SECURE_NO_WARNINGS)
#error _CRT_SECURE_NO_WARNINGS should be defined
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#error WIN32_LEAN_AND_MEAN should be defined
#endif

#include <Windows.h>
#include <DbgHelp.h>
#include <commdlg.h>

#endif
