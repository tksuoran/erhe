#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/gl/gl_gpu_timer.hpp"

namespace erhe::graphics {

Gpu_timer::Gpu_timer(Device& device, const char* label)
    : m_impl{std::make_unique<Gpu_timer_impl>(device, label)}
{
}
Gpu_timer::~Gpu_timer() noexcept
{
}
void Gpu_timer::begin()
{
    m_impl->begin();
}
void Gpu_timer::end()
{
    m_impl->end();
}
void Gpu_timer::read()
{
    m_impl->read();
}
auto Gpu_timer::last_result() -> uint64_t
{
    return m_impl->last_result();
}
auto Gpu_timer::label() const -> const char*
{
    return m_impl->label();
}

Scoped_gpu_timer::Scoped_gpu_timer(Gpu_timer& timer)
    : m_timer{timer}
{
    m_timer.begin();
}

Scoped_gpu_timer::~Scoped_gpu_timer() noexcept
{
    m_timer.end();
}

} // namespace erhe::graphics
