#include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <algorithm>

namespace erhe::graphics {

static std::vector<Gpu_timer_impl*> s_all_gpu_timers;

void Gpu_timer_impl::on_thread_enter()
{
}

void Gpu_timer_impl::on_thread_exit()
{
}

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_device{&device}
    , m_label {label}
{
    s_all_gpu_timers.push_back(this);
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
    s_all_gpu_timers.erase(
        std::remove(s_all_gpu_timers.begin(), s_all_gpu_timers.end(), this),
        s_all_gpu_timers.end()
    );
}

void Gpu_timer_impl::create()
{
}

void Gpu_timer_impl::reset()
{
    m_last_result   = 0;
    m_query_started = false;
    m_query_ended   = false;
}

// TODO GPU timers need redesign: they should be associated with render passes
// rather than started/stopped ad-hoc (vkCmdResetQueryPool cannot be called
// inside a render pass). For now, all operations are no-ops on Vulkan.

void Gpu_timer_impl::begin()
{
}

void Gpu_timer_impl::end()
{
}

void Gpu_timer_impl::read()
{
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    return m_last_result;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return m_label;
}

void Gpu_timer_impl::end_frame()
{
}

auto Gpu_timer_impl::all_gpu_timers() -> std::vector<Gpu_timer_impl*>
{
    return s_all_gpu_timers;
}

} // namespace erhe::graphics
