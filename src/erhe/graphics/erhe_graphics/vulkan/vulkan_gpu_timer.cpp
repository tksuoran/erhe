#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>
#include <thread>


namespace erhe::graphics {

void Gpu_timer_impl::on_thread_enter()
{
}

void Gpu_timer_impl::on_thread_exit()
{
}

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
{
    //ERHE_FATAL("Not implemented");
    static_cast<void>(device);
    static_cast<void>(label);
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
}

void Gpu_timer_impl::create()
{
}

void Gpu_timer_impl::reset()
{
}

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
    return 0;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return "";
}

void Gpu_timer_impl::end_frame()
{
}

auto Gpu_timer_impl::all_gpu_timers() -> std::vector<Gpu_timer_impl*>
{
    return {};
}

} // namespace erhe::graphics
