#include "erhe_graphics/metal/metal_vertex_input_state.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Vertex_input_state_impl::Vertex_input_state_impl(Device& device)
    : m_device{device}
{
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info)
    : m_device{device}
    , m_data  {std::move(create_info)}
{
}

Vertex_input_state_impl::~Vertex_input_state_impl() noexcept = default;

void Vertex_input_state_impl::set(const Vertex_input_state_data& data) { m_data = data; }
void Vertex_input_state_impl::create() {}
void Vertex_input_state_impl::reset()  {}
void Vertex_input_state_impl::update() {}

auto Vertex_input_state_impl::gl_name() const -> unsigned int { return 0; }
auto Vertex_input_state_impl::get_data() const -> const Vertex_input_state_data& { return m_data; }

void Vertex_input_state_impl::on_thread_enter() {}
void Vertex_input_state_impl::on_thread_exit()  {}

} // namespace erhe::graphics
