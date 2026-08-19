#include "assets/asset_load_tick_context.hpp"

#include "config/generated/load_config.hpp"

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto to_size(const int value) -> std::size_t
{
    return (value > 0) ? static_cast<std::size_t>(value) : std::size_t{0};
}

} // anonymous namespace

Frame_load_budget::Frame_load_budget(const Load_config& load_config)
    : m_deadline{
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float, std::milli>{std::max(0.0f, load_config.load_time_slice_ms)}
        )
    }
    , m_gpu_upload_bytes{to_size(load_config.gpu_upload_bytes_per_frame   )}
    , m_io_read_bytes   {to_size(load_config.io_read_bytes_per_frame      )}
    , m_residency_items {to_size(load_config.max_residency_items_per_frame)}
    , m_publish_items   {to_size(load_config.max_publish_items_per_frame  )}
{
}

auto Frame_load_budget::is_exhausted() const -> bool
{
    return std::chrono::steady_clock::now() >= m_deadline;
}

auto Frame_load_budget::take_gpu_upload_bytes(const std::size_t requested) -> std::size_t
{
    const std::size_t granted = std::min(requested, m_gpu_upload_bytes);
    m_gpu_upload_bytes -= granted;
    return granted;
}

void Frame_load_budget::give_back_gpu_upload_bytes(const std::size_t byte_count)
{
    m_gpu_upload_bytes += byte_count;
}

auto Frame_load_budget::take_io_read_bytes(const std::size_t requested) -> std::size_t
{
    const std::size_t granted = std::min(requested, m_io_read_bytes);
    m_io_read_bytes -= granted;
    return granted;
}

auto Frame_load_budget::take_residency_item() -> bool
{
    if (m_residency_items == 0) {
        return false;
    }
    --m_residency_items;
    return true;
}

auto Frame_load_budget::take_publish_item() -> bool
{
    if (m_publish_items == 0) {
        return false;
    }
    --m_publish_items;
    return true;
}

} // namespace editor
