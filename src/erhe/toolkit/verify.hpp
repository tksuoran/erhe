#pragma once


#if _MSC_VER

#if defined(_WIN32)
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   define VC_EXTRALEAN
#   ifndef STRICT
#       define STRICT
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX       // Macros min(a,b) and max(a,b)
#   endif
#   include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <source_location>

#define ERHE_FATAL(format, ...) do { printf("%s:%d " format "\n", std::source_location::current().file_name(), std::source_location::current().line(), ##__VA_ARGS__); DebugBreak(); abort(); } while (1)
#define ERHE_VERIFY(expression) do { if (!(expression)) { ERHE_FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#else

#include <cstdio>
#include <cstdlib>

#define ERHE_FATAL(format, ...) do { printf("%s:%d " format "\n", __FILE__, __LINE__, ##__VA_ARGS__); __builtin_trap(); __builtin_unreachable(); abort(); } while (1)
#define ERHE_VERIFY(expression) do { if (!(expression)) { ERHE_FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#endif

