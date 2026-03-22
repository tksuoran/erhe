# erhe_log

## Purpose
Logging infrastructure built on spdlog. Provides logger creation with
configurable levels (via `logging.toml`), an in-memory log store sink for
displaying log entries in the editor UI, and fmt formatters for glm and
Geogram vector types.

## Key Types
- `Entry` -- a stored log entry with serial number, timestamp, message, logger name, level, repeat count
- `Store_log_sink` -- spdlog sink that accumulates entries in a `std::deque<Entry>` for UI display
- `make_logger()` / `make_frame_logger()` -- factory functions that create configured spdlog loggers

## Public API
```cpp
auto log = erhe::log::make_logger("my_subsystem");
log->info("message {}", value);

// Access stored entries for UI:
erhe::log::get_tail_store_log().access_entries([](auto& entries){ ... });
```
- `console_init()` / `initialize_log_sinks()` / `log_to_console()` -- initialization
- `timestamp()` / `timestamp_short()` -- formatted timestamp strings
- `get_groupname()` / `get_basename()` / `get_levelname()` -- string utilities

## Dependencies
- External: spdlog, fmt
- No erhe library dependencies (leaf library)

## Notes
- Log levels are read from `logging.toml` at startup.
- `log_glm.hpp` provides `fmt::formatter` specializations for all glm vec/dvec/ivec types (2/3/4).
- `log_geogram.hpp` provides `fmt::formatter` specializations for all Geogram vector types.
- Two store sinks exist: a "tail" store (persistent) and a "frame" store (per-frame).
- The `Store_log_sink` deduplicates consecutive identical messages via `repeat_count`.
