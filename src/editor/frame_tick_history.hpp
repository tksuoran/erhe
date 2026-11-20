#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace editor {

enum class Frame_tick_scene_state : unsigned int {
    loading = 0, // App_context::is_scene_load_in_flight() at the tick's end
    idle
};

class Frame_tick
{
public:
    std::uint64_t          tick       {0};   // 1 for the first main-loop tick
    double                 end_s      {0.0}; // since the editor process started (get_process_time_s())
    double                 duration_ms{0.0}; // start_frame() begin to frame() end: the loop period
    Frame_tick_scene_state scene_state{Frame_tick_scene_state::loading};
};

// The main loop's recent tick durations, for wall-clock measurements (MCP get_frame_ticks;
// doc/agents/mcp_server_usage.md "get_frame_ticks"). Main thread only; record() never allocates.
class Frame_tick_history
{
public:
    static constexpr std::size_t c_capacity{8192};

    void record(
        std::chrono::steady_clock::time_point tick_begin,
        std::chrono::steady_clock::time_point tick_end,
        Frame_tick_scene_state                scene_state
    );

    // 0 when nothing was recorded; ticks older than get_oldest_tick() were overwritten.
    [[nodiscard]] auto get_latest_tick() const -> std::uint64_t;
    [[nodiscard]] auto get_oldest_tick() const -> std::uint64_t;
    // nullptr for a tick outside [get_oldest_tick(), get_latest_tick()].
    [[nodiscard]] auto get(std::uint64_t tick) const -> const Frame_tick*;

    // Seconds from the editor's static initialization, which runs before main().
    [[nodiscard]] static auto get_process_time_s(std::chrono::steady_clock::time_point time) -> double;

private:
    std::array<Frame_tick, c_capacity> m_ticks{};
    std::uint64_t                      m_latest_tick{0};
};

} // namespace editor
