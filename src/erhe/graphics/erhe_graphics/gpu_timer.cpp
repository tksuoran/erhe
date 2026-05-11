#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pass.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_gpu_timer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_gpu_timer.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_gpu_timer.hpp"
#endif

#include <algorithm>
#include <mutex>

namespace erhe::graphics {

namespace {

std::mutex              s_facade_mutex;
std::vector<Gpu_timer*> s_facade_timers;

} // anonymous namespace

Gpu_timer::Gpu_timer(Render_pass& render_pass, const char* label)
    : m_render_pass{&render_pass}
    , m_impl       {std::make_unique<Gpu_timer_impl>(render_pass, label)}
{
    {
        const std::lock_guard<std::mutex> lock{s_facade_mutex};
        s_facade_timers.push_back(this);
    }
    render_pass.register_gpu_timer(this);
}

Gpu_timer::~Gpu_timer() noexcept
{
    if (m_render_pass != nullptr) {
        m_render_pass->unregister_gpu_timer(this);
    }
    const std::lock_guard<std::mutex> lock{s_facade_mutex};
    s_facade_timers.erase(
        std::remove(s_facade_timers.begin(), s_facade_timers.end(), this),
        s_facade_timers.end()
    );
}

void Gpu_timer::write_begin_timestamp(Command_buffer& command_buffer)
{
    if (m_render_pass == nullptr) {
        return;
    }
    m_impl->write_begin_timestamp(command_buffer);
}

void Gpu_timer::write_end_timestamp(Command_buffer& command_buffer)
{
    if (m_render_pass == nullptr) {
        return;
    }
    m_impl->write_end_timestamp(command_buffer);
}

void Gpu_timer::on_render_pass_destroyed()
{
    m_render_pass = nullptr;
}

auto Gpu_timer::last_result() -> uint64_t
{
    return m_impl->last_result();
}

auto Gpu_timer::label() const -> const char*
{
    return m_impl->label();
}

auto Gpu_timer::all_gpu_timers() -> std::vector<Gpu_timer*>
{
    const std::lock_guard<std::mutex> lock{s_facade_mutex};
    return s_facade_timers;
}

} // namespace erhe::graphics
