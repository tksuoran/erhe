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

#define FATAL(format, ...) do { printf("%s:%d " format, __FILE__, __LINE__, ##__VA_ARGS__); DebugBreak(); abort(); } while (1)
#define VERIFY(expression) do { if (!(expression)) { FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#else

#define FATAL(format, ...) do { printf("%s:%d " format, __FILE__, __LINE__, ##__VA_ARGS__); __builtin_trap(); __builtin_unreachable(); abort(); } while (1)
#define VERIFY(expression) do { if (!(expression)) { FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#endif

