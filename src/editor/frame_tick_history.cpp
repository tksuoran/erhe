#include "frame_tick_history.hpp"

namespace editor {

namespace {

const std::chrono::steady_clock::time_point s_process_start = std::chrono::steady_clock::now();

} // anonymous namespace

void Frame_tick_history::record(
    const std::chrono::steady_clock::time_point tick_begin,
    const std::chrono::steady_clock::time_point tick_end,
    const Frame_tick_scene_state                scene_state
)
{
    ++m_latest_tick;
    Frame_tick& entry = m_ticks[m_latest_tick % c_capacity];
    entry.tick        = m_latest_tick;
    entry.end_s       = get_process_time_s(tick_end);
    entry.duration_ms = std::chrono::duration<double, std::milli>{tick_end - tick_begin}.count();
    entry.scene_state = scene_state;
}

auto Frame_tick_history::get_latest_tick() const -> std::uint64_t
{
    return m_latest_tick;
}

auto Frame_tick_history::get_oldest_tick() const -> std::uint64_t
{
    if (m_latest_tick == 0) {
        return 0;
    }
    return (m_latest_tick > c_capacity) ? (m_latest_tick - c_capacity + 1) : 1;
}

auto Frame_tick_history::get(const std::uint64_t tick) const -> const Frame_tick*
{
    if ((tick == 0) || (tick < get_oldest_tick()) || (tick > m_latest_tick)) {
        return nullptr;
    }
    return &m_ticks[tick % c_capacity];
}

auto Frame_tick_history::get_process_time_s(const std::chrono::steady_clock::time_point time) -> double
{
    return std::chrono::duration<double>{time - s_process_start}.count();
}

} // namespace editor
