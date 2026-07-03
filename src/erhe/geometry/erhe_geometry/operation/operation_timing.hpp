#pragma once

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

// Collects wall-clock durations of named geometry-operation phases (the
// Catmull-Clark build phases, attribute interpolation, sanitize, the
// process() reprocess tail). Inert unless a collector is installed for the
// calling thread: normal runs pay one null check per phase scope, no clock
// reads and no allocations. The timing harness in
// src/erhe/geometry/test/test_timing_harness.cpp installs a collector,
// runs an operation chain, and reads the recorded phases back.
class Operation_timing
{
public:
    // Installs the given collector as the calling thread's active collector
    // and returns the previous one so the caller can restore it.
    static auto install(Operation_timing* collector) -> Operation_timing*;
    [[nodiscard]] static auto active() -> Operation_timing*;

    void add(const char* phase, std::chrono::steady_clock::duration duration);
    void clear();

    // Sum of all recorded durations for the given phase name, in milliseconds.
    [[nodiscard]] auto get_milliseconds(std::string_view phase) const -> double;

    [[nodiscard]] auto entries() const -> const std::vector<std::pair<const char*, std::chrono::steady_clock::duration>>&;

private:
    std::vector<std::pair<const char*, std::chrono::steady_clock::duration>> m_entries;
};

// RAII phase scope. When no collector is installed on this thread the
// constructor and destructor are a single branch each plus one
// erhe::log::set_breadcrumb() call (so a watchdog attributes a stall to the
// phase it happened in); the phase name must be a string literal (stored as
// pointer, not copied).
class Scoped_phase_timer
{
public:
    explicit Scoped_phase_timer(const char* phase);
    ~Scoped_phase_timer();
    Scoped_phase_timer(const Scoped_phase_timer&)            = delete;
    Scoped_phase_timer& operator=(const Scoped_phase_timer&) = delete;

private:
    const char*                           m_phase;
    Operation_timing*                     m_collector; // nullptr -> inactive
    std::chrono::steady_clock::time_point m_start;
};

} // namespace erhe::geometry::operation
