#pragma once

#if _MSC_VER && !defined(__clang__)

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
#include <string>

#define ERHE_FATAL(format, ...) do { erhe_report_fatal(std::source_location::current().file_name(), static_cast<int>(std::source_location::current().line()), format, ##__VA_ARGS__); DebugBreak(); abort(); } while (1)
#define ERHE_VERIFY(expression) do { if (!(expression)) { ERHE_FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#elif defined(__ANDROID__)

#include <android/log.h>
#include <cstdlib>
#include <string>

// erhe_report_fatal() writes to logcat (tag "erhe") on Android, where app
// processes have stdout/stderr connected to /dev/null.
#define ERHE_FATAL(format, ...) do { erhe_report_fatal(__FILE__, __LINE__, format, ##__VA_ARGS__); __builtin_trap(); __builtin_unreachable(); abort(); } while (1)
#define ERHE_VERIFY(expression) do { if (!(expression)) { ERHE_FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#else

#include <cstdio>
#include <cstdlib>
#include <string>

#define ERHE_FATAL(format, ...) do { erhe_report_fatal(__FILE__, __LINE__, format, ##__VA_ARGS__); __builtin_trap(); __builtin_unreachable(); abort(); } while (1)
#define ERHE_VERIFY(expression) do { if (!(expression)) { ERHE_FATAL("assert %s failed in %s", #expression, __func__); } } while (0)

#endif

#if defined(__GNUC__) || defined(__clang__)
#   define ERHE_PRINTF_FORMAT(format_index, first_argument_index) __attribute__((format(printf, format_index, first_argument_index)))
#else
#   define ERHE_PRINTF_FORMAT(format_index, first_argument_index)
#endif

// Reports a failing ERHE_FATAL / ERHE_VERIFY before the caller aborts: the
// "file:line message" line goes to stdout (logcat on Android), the
// callstack to stderr (logcat on Android), and both to the fatal handler.
void erhe_report_fatal(const char* file, int line, const char* format, ...) ERHE_PRINTF_FORMAT(3, 4);

// Receives the message and the callstack of a failing ERHE_FATAL /
// ERHE_VERIFY. erhe::log installs one in initialize_log_sinks() so the crash
// reason reaches logs/log.txt. A fatal failure inside the handler skips it.
using Erhe_fatal_handler = void (*)(const char* message, const char* callstack);
void erhe_set_fatal_handler(Erhe_fatal_handler handler);

void erhe_dump_callstack();
auto erhe_get_callstack() -> std::string;
