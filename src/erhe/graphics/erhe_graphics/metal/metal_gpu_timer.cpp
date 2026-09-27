#include "erhe_graphics/metal/metal_gpu_timer.hpp"
#include "erhe_graphics/gpu_timer.hpp"

namespace erhe::graphics {

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_label{label}
{
    static_cast<void>(device);
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
}

void Gpu_timer_impl::write_begin_timestamp(Command_buffer& command_buffer)
{
    static_cast<void>(command_buffer);
}

void Gpu_timer_impl::write_end_timestamp(Command_buffer& command_buffer)
{
    static_cast<void>(command_buffer);
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    return 0;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

} // namespace erhe::graphics
