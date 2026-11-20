#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>

struct Threading_config;

namespace editor {

// Main-thread time a frame spends applying background results, against threading.background_apply_soft_limit_ms
// (doc/editor/editor.md "Background apply meter"). Main thread only; defers nothing.
class Background_apply_meter
{
public:
    static constexpr std::size_t c_max_owners{8};

    // Adds its lifetime to the frame's total, under owner (a string literal).
    class Scope
    {
    public:
        Scope(Background_apply_meter& meter, const char* owner);
        ~Scope() noexcept;
        Scope(const Scope&) = delete;
        auto operator=(const Scope&) -> Scope& = delete;
        Scope(Scope&&) = delete;
        auto operator=(Scope&&) -> Scope& = delete;

    private:
        Background_apply_meter&               m_meter;
        const char*                           m_owner;
        std::chrono::steady_clock::time_point m_begin;
    };

    explicit Background_apply_meter(const Threading_config& config);

    // Once per tick, at its end: warns when the frame's total exceeds the limit (at most once per second), then
    // starts the next frame's total.
    void end_frame();

private:
    void add(const char* owner, std::chrono::steady_clock::duration duration);

    class Entry
    {
    public:
        const char*                         owner{nullptr};
        std::chrono::steady_clock::duration duration{};
    };

    const Threading_config&                              m_config;
    std::array<Entry, c_max_owners>                      m_entries{};
    std::size_t                                          m_entry_count{0};
    std::chrono::steady_clock::duration                  m_total{};
    std::optional<std::chrono::steady_clock::time_point> m_last_warning_time{};
};

} // namespace editor
