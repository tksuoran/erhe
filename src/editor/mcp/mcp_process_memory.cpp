// Process memory figures for MCP get_memory_usage. Kept in its own
// translation unit so the platform headers (windows.h) stay out of the MCP
// server sources.

#include "mcp/mcp_process_memory.hpp"

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#   include <psapi.h>
#   include <array>
#elif defined(__ANDROID__) || defined(__linux__)
#   include <malloc.h>
#   include <array>
#   include <cstdlib>
#   include <cstring>
#   include <fstream>
#   include <string>
#endif

#include <cstddef>

namespace editor {

// The operating system's view of the process memory, for cross-checking the
// per-owner figures of get_memory_usage. Windows: PROCESS_MEMORY_COUNTERS_EX
// (working set = resident, private usage = committed private bytes).
// Linux / Android: /proc/self/status (VmRSS / VmHWM and the anonymous / file
// split) plus the allocator's own in-use figure (mallinfo).
auto get_process_memory_json() -> nlohmann::json
{
    nlohmann::json result = nlohmann::json::object();
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0) {
        result["working_set_bytes"]      = static_cast<std::size_t>(counters.WorkingSetSize);
        result["peak_working_set_bytes"] = static_cast<std::size_t>(counters.PeakWorkingSetSize);
        result["private_bytes"]          = static_cast<std::size_t>(counters.PrivateUsage);
        result["peak_pagefile_bytes"]    = static_cast<std::size_t>(counters.PeakPagefileUsage);
    }
    // The CPU heaps (the CRT allocates from the process heap): what is
    // allocated in them, without the driver's private commitments that
    // private_bytes also counts.
    std::array<HANDLE, 256> heaps{};
    const DWORD heap_count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data());
    std::size_t heap_allocated_bytes = 0;
    std::size_t heap_committed_bytes = 0;
    for (DWORD i = 0; (i < heap_count) && (i < heaps.size()); ++i) {
        HEAP_SUMMARY summary{};
        summary.cb = sizeof(summary);
        if (HeapSummary(heaps[i], 0, &summary) != 0) {
            heap_allocated_bytes += static_cast<std::size_t>(summary.cbAllocated);
            heap_committed_bytes += static_cast<std::size_t>(summary.cbCommitted);
        }
    }
    result["heap_count"]           = static_cast<std::size_t>(heap_count);
    result["heap_allocated_bytes"] = heap_allocated_bytes;
    result["heap_committed_bytes"] = heap_committed_bytes;
#elif defined(__ANDROID__) || defined(__linux__)
    static constexpr std::array<const char*, 7> c_keys{"VmRSS", "VmHWM", "RssAnon", "RssFile", "RssShmem", "VmSwap", "VmSize"};
    std::ifstream status{"/proc/self/status"};
    std::string   line;
    while (std::getline(status, line)) {
        for (const char* key : c_keys) {
            const std::size_t key_length = std::strlen(key);
            if ((line.compare(0, key_length, key) == 0) && (line.size() > key_length) && (line[key_length] == ':')) {
                const unsigned long long kib = std::strtoull(line.c_str() + key_length + 1, nullptr, 10);
                result[std::string{key} + "_bytes"] = static_cast<std::size_t>(kib * 1024ull);
            }
        }
    }
#   if defined(__ANDROID__)
    const struct mallinfo info = mallinfo();
    // (bionic's fordblks is not a meaningful free figure, so only in-use.)
    result["malloc_in_use_bytes"] = static_cast<std::size_t>(info.uordblks);
#   elif defined(__GLIBC__)
    const struct mallinfo2 info = mallinfo2();
    result["malloc_in_use_bytes"] = static_cast<std::size_t>(info.uordblks + info.hblkhd);
    result["malloc_free_bytes"]   = static_cast<std::size_t>(info.fordblks);
#   endif
#endif
    return result;
}

} // namespace editor
