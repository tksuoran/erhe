#include "erhe_verify/verify.hpp"

#include <cstdio>

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <cstdlib>

void erhe_dump_callstack()
{
    static constexpr int max_frames = 64;
    void* frames[max_frames];
    int frame_count = backtrace(frames, max_frames);
    char** symbols = backtrace_symbols(frames, frame_count);
    if (symbols != nullptr) {
        fprintf(stderr, "=== Callstack ===\n");
        for (int i = 0; i < frame_count; ++i) {
            fprintf(stderr, "  %s\n", symbols[i]);
        }
        fprintf(stderr, "=================\n");
        free(symbols);
    }
}

auto erhe_get_callstack() -> std::string
{
    static constexpr int max_frames = 64;
    void* frames[max_frames];
    int frame_count = backtrace(frames, max_frames);
    char** symbols = backtrace_symbols(frames, frame_count);
    std::string result;
    if (symbols != nullptr) {
        for (int i = 0; i < frame_count; ++i) {
            result += "  ";
            result += symbols[i];
            result += "\n";
        }
        free(symbols);
    }
    return result;
}

#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

void erhe_dump_callstack()
{
    static constexpr int max_frames = 64;
    void* frames[max_frames];
    USHORT frame_count = CaptureStackBackTrace(0, max_frames, frames, nullptr);

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    fprintf(stderr, "=== Callstack ===\n");
    for (USHORT i = 0; i < frame_count; ++i) {
        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), nullptr, symbol)) {
            fprintf(stderr, "  %s\n", symbol->Name);
        } else {
            fprintf(stderr, "  [0x%p]\n", frames[i]);
        }
    }
    fprintf(stderr, "=================\n");
}

auto erhe_get_callstack() -> std::string
{
    static constexpr int max_frames = 64;
    void* frames[max_frames];
    USHORT frame_count = CaptureStackBackTrace(0, max_frames, frames, nullptr);

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    std::string result;
    for (USHORT i = 0; i < frame_count; ++i) {
        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), nullptr, symbol)) {
            result += "  ";
            result += symbol->Name;
            result += "\n";
        } else {
            char addr_buf[32];
            snprintf(addr_buf, sizeof(addr_buf), "  [0x%p]\n", frames[i]);
            result += addr_buf;
        }
    }
    return result;
}

#else

void erhe_dump_callstack()
{
    fprintf(stderr, "=== Callstack dumping not supported on this platform ===\n");
}

auto erhe_get_callstack() -> std::string
{
    return "(callstack not available on this platform)\n";
}

#endif
