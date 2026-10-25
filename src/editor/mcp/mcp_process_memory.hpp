#pragma once

#include <nlohmann/json.hpp>

namespace editor {

// The operating system's view of this process' memory (resident set, peak,
// private / anonymous bytes, allocator in-use bytes where available), as
// reported by MCP get_memory_usage under "process"
// (doc/reference/memory_usage_2026_10.md).
[[nodiscard]] auto get_process_memory_json() -> nlohmann::json;

} // namespace editor
