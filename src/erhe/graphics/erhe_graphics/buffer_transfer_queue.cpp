// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Buffer_transfer_queue::Buffer_transfer_queue(Device& device)
    : m_device{device}
{
}

Buffer_transfer_queue::~Buffer_transfer_queue() noexcept
{
    // flush(); TODO causes GL errors in shutdown, investigate
}

void Buffer_transfer_queue::enqueue(const Buffer* buffer, const std::size_t offset, std::vector<uint8_t>&& data)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(
        log_buffer,
        "queued buffer {} transfer offset = {} size = {}",
        buffer.gl_name(),
        offset,
        data.size()
    );
    m_queued.emplace_back(buffer, offset, std::move(data));
}

void Buffer_transfer_queue::flush()
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    for (const auto& entry : m_queued) {
        SPDLOG_LOGGER_TRACE(
            log_buffer,
            "buffer upload {} {} transfer offset = {} size = {}",
            gl::c_str(entry.target.target()),
            entry.target->gl_name(),
            entry.target_offset,
            entry.data.size()
        );
        m_device.upload_to_buffer(*entry.target, entry.target_offset, entry.data.data(), entry.data.size());
    }
    m_queued.clear();
}

} // namespace erhe::graphics
