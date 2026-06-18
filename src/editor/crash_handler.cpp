#include "crash_handler.hpp"

#if defined(_WIN32)

// clang-format off
#include <windows.h>
#include <dbghelp.h> // MiniDumpWriteDump; links dbghelp.lib
// clang-format on

#include <crtdbg.h>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace editor {

namespace {

// Set once the first crashing thread starts writing a dump, so a second fault
// (or an abort() raised while the dump is being written) does not recurse.
std::atomic<bool> s_dump_in_progress{false};

void write_minidump(EXCEPTION_POINTERS* exception_pointers)
{
    bool expected = false;
    if (!s_dump_in_progress.compare_exchange_strong(expected, true)) {
        return;
    }

    CreateDirectoryW(L"logs", nullptr);

    wchar_t   path[MAX_PATH];
    const DWORD pid  = GetCurrentProcessId();
    const DWORD tick = GetTickCount();
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"logs\\editor_crash_%lu_%lu.dmp", pid, tick);

    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[crash] failed to create minidump file (gle=%lu)\n", GetLastError());
        std::fflush(stderr);
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = exception_pointers;
    mei.ClientPointers    = FALSE;

    // Thread contexts (stacks) plus data segments (globals) - enough to walk the
    // faulting call stack post-mortem without producing a huge full-memory dump.
    const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithDataSegs);

    const BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(),
        pid,
        file,
        dump_type,
        exception_pointers ? &mei : nullptr,
        nullptr,
        nullptr
    );
    CloseHandle(file);

    if (ok) {
        std::fprintf(stderr, "[crash] wrote minidump: %ws\n", path);
    } else {
        std::fprintf(stderr, "[crash] MiniDumpWriteDump failed (gle=%lu)\n", GetLastError());
    }
    std::fflush(stderr);
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_pointers)
{
    // If a debugger attached after startup, let it take the exception instead of
    // writing a dump and terminating.
    if (IsDebuggerPresent()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = (exception_pointers != nullptr) && (exception_pointers->ExceptionRecord != nullptr)
        ? exception_pointers->ExceptionRecord->ExceptionCode
        : 0;
    std::fprintf(stderr, "[crash] unhandled exception (code=0x%08lx)\n", code);
    std::fflush(stderr);
    write_minidump(exception_pointers);
    return EXCEPTION_EXECUTE_HANDLER; // terminate the process
}

void abort_handler(int)
{
    // If a debugger attached after startup, break into it at the abort site
    // rather than writing a dump and terminating.
    if (IsDebuggerPresent()) {
        __debugbreak();
        return;
    }
    // abort() raises SIGABRT. Intercepting it here runs before the CRT's default
    // handler (which would show the modal "abort() has been called" dialog), so
    // the dialog never appears. There are no EXCEPTION_POINTERS for an abort, but
    // the dump still snapshots every thread's stack, including the abort path.
    std::fprintf(stderr, "[crash] abort() called\n");
    std::fflush(stderr);
    write_minidump(nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
}

} // namespace

void install_crash_handler()
{
    // Under a debugger (e.g. F5 from Visual Studio) install nothing: leave the
    // default behaviour so asserts, abort() and access violations break into the
    // debugger and the live faulting state can be inspected. This handler exists
    // only for unattended / headless runs, where the alternative is a modal
    // dialog that hangs the run. (The handlers also re-check IsDebuggerPresent()
    // to cover a debugger that attaches after startup.)
    if (IsDebuggerPresent()) {
        return;
    }

    // Suppress the Windows Error Reporting GUI and critical-error dialogs so a
    // crash fails fast instead of blocking on a popup.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Route the debug CRT's assert / error / warn reports (_CrtDbgReport, used by
    // assert() and _ASSERTE) to stderr instead of the modal dialog. No effect in
    // the release CRT.
    for (const int report_type : {_CRT_ASSERT, _CRT_ERROR, _CRT_WARN}) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }

    // Do not let abort() pop the CRT message box or hand off to WER before our
    // SIGABRT handler runs.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, abort_handler);

    SetUnhandledExceptionFilter(unhandled_exception_filter);
}

} // namespace editor

#else // !_WIN32

namespace editor {

void install_crash_handler()
{
}

} // namespace editor

#endif
