#include "assets/asset_load_task.hpp"

#include <utility>

namespace editor {

auto c_str(const Asset_load_state state) -> const char*
{
    switch (state) {
        case Asset_load_state::queued:    return "queued";
        case Asset_load_state::running:   return "running";
        case Asset_load_state::resident:  return "resident";
        case Asset_load_state::done:      return "done";
        case Asset_load_state::failed:    return "failed";
        case Asset_load_state::cancelled: return "cancelled";
        default:                          return "?";
    }
}

auto is_settled(const Asset_load_state state) -> bool
{
    return
        (state == Asset_load_state::done)   ||
        (state == Asset_load_state::failed) ||
        (state == Asset_load_state::cancelled);
}

Asset_load_handle::Asset_load_handle(std::filesystem::path path)
    : m_path{std::move(path)}
{
}

auto Asset_load_handle::get_state() const -> Asset_load_state
{
    return m_state.load(std::memory_order_acquire);
}

auto Asset_load_handle::get_progress() const -> float
{
    return m_progress.load(std::memory_order_relaxed);
}

auto Asset_load_handle::get_error() const -> std::string
{
    const std::lock_guard<std::mutex> lock{m_error_mutex};
    return m_error;
}

auto Asset_load_handle::is_settled() const -> bool
{
    return editor::is_settled(get_state());
}

void Asset_load_handle::request_cancel()
{
    m_cancel_requested.store(true, std::memory_order_release);
}

auto Asset_load_handle::is_cancel_requested() const -> bool
{
    return m_cancel_requested.load(std::memory_order_acquire);
}

void Asset_load_handle::set_state(const Asset_load_state state)
{
    m_state.store(state, std::memory_order_release);
}

void Asset_load_handle::set_progress(const float progress)
{
    m_progress.store(progress, std::memory_order_relaxed);
}

void Asset_load_handle::set_failed(std::string error)
{
    {
        const std::lock_guard<std::mutex> lock{m_error_mutex};
        m_error = std::move(error);
    }
    set_state(Asset_load_state::failed);
}

Asset_load_task::Asset_load_task(std::shared_ptr<Asset_load_handle> handle)
    : m_handle{std::move(handle)}
{
}

Asset_load_task::~Asset_load_task() noexcept = default;

} // namespace editor
