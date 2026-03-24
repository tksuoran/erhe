#include "erhe_graphics/null/null_gpu_timer.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

std::mutex                   Gpu_timer_impl::s_mutex;
std::vector<Gpu_timer_impl*> Gpu_timer_impl::s_all_gpu_timers;
Gpu_timer_impl*              Gpu_timer_impl::s_active_timer{nullptr};
std::size_t                  Gpu_timer_impl::s_index{0};

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_device{device}
    , m_label {label}
{
    const std::lock_guard lock{s_mutex};
    s_all_gpu_timers.push_back(this);
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
    const std::lock_guard lock{s_mutex};
    s_all_gpu_timers.erase(
        std::remove(s_all_gpu_timers.begin(), s_all_gpu_timers.end(), this),
        s_all_gpu_timers.end()
    );
}

void Gpu_timer_impl::create()
{
    // No-op in null backend
}

void Gpu_timer_impl::reset()
{
    // No-op in null backend
}

void Gpu_timer_impl::begin()
{
    // No-op in null backend
}

void Gpu_timer_impl::end()
{
    // No-op in null backend
}

void Gpu_timer_impl::read()
{
    // No-op in null backend
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    return 0;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

void Gpu_timer_impl::on_thread_enter()
{
    // No-op in null backend
}

void Gpu_timer_impl::on_thread_exit()
{
    // No-op in null backend
}

void Gpu_timer_impl::end_frame()
{
    const std::lock_guard lock{s_mutex};
    ++s_index;
}

auto Gpu_timer_impl::all_gpu_timers() -> std::vector<Gpu_timer_impl*>
{
    const std::lock_guard lock{s_mutex};
    return s_all_gpu_timers;
}

} // namespace erhe::graphics
