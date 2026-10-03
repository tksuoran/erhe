#include "erhe_verify/verify.hpp"

#include <cpptrace/cpptrace.hpp>
#include <cpptrace/formatting.hpp>
#include <cstdarg>
#include <cstdio>

#if defined(__ANDROID__)
#   include <android/log.h>
#   include <string>
#endif

namespace {

auto make_formatter() -> cpptrace::formatter
{
    cpptrace::formatter fmt;
    fmt.colors(cpptrace::formatter::color_mode::none);
    fmt.addresses(cpptrace::formatter::address_mode::none);
    return fmt;
}

Erhe_fatal_handler s_fatal_handler{nullptr};
thread_local bool  s_in_fatal_handler{false};

#if defined(__ANDROID__)
// Emit line-by-line so each line stays under the logcat message size limit.
void android_log_lines(const std::string& text)
{
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        if (end > start) {
            const std::string line = text.substr(start, end - start);
            __android_log_write(ANDROID_LOG_FATAL, "erhe", line.c_str());
        }
        start = end + 1;
    }
}
#endif

} // anonymous namespace

void erhe_set_fatal_handler(const Erhe_fatal_handler handler)
{
    s_fatal_handler = handler;
}

void erhe_report_fatal(const char* const file, const int line, const char* const format, ...)
{
    char message[2048];
    const int prefix_length = std::snprintf(message, sizeof(message), "%s:%d ", file, line);
    if ((prefix_length >= 0) && (static_cast<std::size_t>(prefix_length) < sizeof(message))) {
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message + prefix_length, sizeof(message) - static_cast<std::size_t>(prefix_length), format, arguments);
        va_end(arguments);
    }

    const std::string callstack = erhe_get_callstack();
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_FATAL, "erhe", message);
    __android_log_write(ANDROID_LOG_FATAL, "erhe", "=== Callstack ===");
    android_log_lines(callstack);
    __android_log_write(ANDROID_LOG_FATAL, "erhe", "=================");
#else
    std::printf("%s\n", message);
    std::fflush(stdout);
    std::fprintf(stderr, "=== Callstack ===\n%s\n=================\n", callstack.c_str());
    std::fflush(stderr);
#endif

    if ((s_fatal_handler != nullptr) && !s_in_fatal_handler) {
        s_in_fatal_handler = true;
        s_fatal_handler(message, callstack.c_str());
        s_in_fatal_handler = false;
    }
}

void erhe_dump_callstack()
{
    static const cpptrace::formatter fmt = make_formatter();
#if defined(__ANDROID__)
    // stderr is /dev/null in Android app processes; route to logcat. Emit
    // line-by-line so each frame stays under the per-message size limit.
    __android_log_write(ANDROID_LOG_FATAL, "erhe", "=== Callstack ===");
    android_log_lines(fmt.format(cpptrace::generate_trace()));
    __android_log_write(ANDROID_LOG_FATAL, "erhe", "=================");
#else
    fprintf(stderr, "=== Callstack ===\n");
    fmt.print(stderr, cpptrace::generate_trace());
    fprintf(stderr, "=================\n");
#endif
}

auto erhe_get_callstack() -> std::string
{
    static const cpptrace::formatter fmt = make_formatter();
    return fmt.format(cpptrace::generate_trace());
}
