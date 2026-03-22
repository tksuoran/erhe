# erhe_log -- Code Review

## Summary
A logging library wrapping spdlog, providing configurable log sinks (console, file, MSVC debug output, and in-memory stores for UI display), TOML-based log level configuration, fmt formatters for glm and Geogram types, and timestamping. The design is practical and well-suited to the editor's needs. The main concern is the use of a Meyers singleton.

## Strengths
- Dual in-memory stores (`tail` and `frame`) enable both persistent log history and per-frame logging in the editor UI
- TOML-based log level configuration allows runtime tuning without recompilation
- Comprehensive fmt formatters for glm and Geogram vector types (vec2/3/4, dvec, ivec, uvec)
- `Store_log_sink` properly uses spdlog's built-in mutex via `base_sink<std::mutex>`
- The `get_groupname`/`get_basename` functions support hierarchical logger naming (e.g., "erhe.item.log")

## Issues
- **[moderate]** `Log_sinks` uses a Meyers singleton with `create_sinks()` called separately from construction (log.cpp:157-173). If `make_logger()` is called before `initialize_log_sinks()`, the shared_ptrs will be null, causing a crash. There is no guard against this.
- **[moderate]** `get_levelname` (log.cpp:231-235) has a double semicolon (`;;`) which is harmless but indicates a possible copy-paste artifact.
- **[minor]** Repetitive boilerplate in `log_glm.hpp` and `log_geogram.hpp` -- every formatter has an identical `parse()` method. A base template or macro could reduce this significantly.
- **[minor]** `Entry::level` is initialized with a magic number `2` with a comment referencing `SPDLOG_LEVEL_INFO` (log.hpp:33). Using the actual enum value would be clearer.
- **[minor]** `timestamp()` and `timestamp_short()` (timestamp.cpp) duplicate most of their implementation. Only the format string differs.
- **[minor]** `Store_log_sink::get_serial()` is not thread-safe -- it reads `m_serial` without holding the mutex, though it is only called for approximate display purposes.

## Suggestions
- Add a guard in `make_logger()` to verify sinks have been created (e.g., `ERHE_VERIFY(m_sink_log_file != nullptr)`) to catch initialization-order bugs early.
- Use `spdlog::level::info` instead of the magic number `2` for `Entry::level` default.
- Factor out the common `parse()` implementation in the fmt formatters using a CRTP base or a macro.
- Merge the duplicated code between `timestamp()` and `timestamp_short()` into a shared helper.
- Fix the double semicolon in `get_levelname`.
