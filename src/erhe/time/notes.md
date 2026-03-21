# erhe_time

## Purpose
Time-related utilities providing high-precision sleep, scoped timers for profiling initialization and frame phases, and timestamp string formatting. The sleep implementation uses platform-specific high-resolution timers (Windows waitable timers) for sub-millisecond accuracy.

## Key Types
- `Timer` -- Named timer that records start/end time points. All timers register in a global list accessible via `Timer::all_timers()`. Provides `format_duration()` for human-readable output.
- `Scoped_timer` -- RAII wrapper that calls `Timer::begin()` on construction and `Timer::end()` on destruction.

## Public API
- `sleep_initialize()` -- Must be called once before using sleep functions. Returns success.
- `sleep_for(duration)` -- Sleeps for the given `std::chrono` duration with high precision.
- `sleep_for_100ns(count)` -- Sleeps for the given number of 100-nanosecond intervals.
- `timestamp()` / `timestamp_short()` -- Returns current date/time as a formatted string.
- `format_duration(duration)` -- Formats a `steady_clock::duration` as a human-readable string.

## Dependencies
- fmt (string formatting)
- erhe::log (private, for logging)
- erhe::profile (private, for profiling mutex)

## Notes
- On Windows, `sleep_for` uses `CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` for better than 1ms precision.
- The global timer list is thread-safe (mutex-protected).
