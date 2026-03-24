#include "erhe_graphics/null/null_vertex_input_state.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

std::mutex                          Vertex_input_state_impl::s_mutex;
std::vector<Vertex_input_state_impl*> Vertex_input_state_impl::s_all_vertex_input_states;

Vertex_input_state_impl::Vertex_input_state_impl(Device& device)
    : m_device{device}
{
    const std::lock_guard lock{s_mutex};
    s_all_vertex_input_states.push_back(this);
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info)
    : m_device{device}
    , m_data  {std::move(create_info)}
{
    const std::lock_guard lock{s_mutex};
    s_all_vertex_input_states.push_back(this);
}

Vertex_input_state_impl::~Vertex_input_state_impl() noexcept
{
    const std::lock_guard lock{s_mutex};
    s_all_vertex_input_states.erase(
        std::remove(s_all_vertex_input_states.begin(), s_all_vertex_input_states.end(), this),
        s_all_vertex_input_states.end()
    );
}

void Vertex_input_state_impl::set(const Vertex_input_state_data& data)
{
    m_data = data;
}

void Vertex_input_state_impl::create()
{
    // No-op in null backend
}

void Vertex_input_state_impl::reset()
{
    // No-op in null backend
}

void Vertex_input_state_impl::update()
{
    // No-op in null backend
}

auto Vertex_input_state_impl::gl_name() const -> unsigned int
{
    return 0;
}

auto Vertex_input_state_impl::get_data() const -> const Vertex_input_state_data&
{
    return m_data;
}

void Vertex_input_state_impl::on_thread_enter()
{
    // No-op in null backend
}

void Vertex_input_state_impl::on_thread_exit()
{
    // No-op in null backend
}

} // namespace erhe::graphics
