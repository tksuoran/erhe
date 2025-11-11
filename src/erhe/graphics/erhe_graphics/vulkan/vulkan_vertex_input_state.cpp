#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_graphics/vulkan/vulkan_vertex_input_state.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <memory>
#include <sstream>

namespace erhe::graphics {

Vertex_input_state_impl::Vertex_input_state_impl(Device& device)
    : m_device{device}
{
    create();
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info)
    : m_device{device}
    , m_data  {std::move(create_info)}
{
    create();
}

Vertex_input_state_impl::~Vertex_input_state_impl() noexcept
{
}

void Vertex_input_state_impl::set(const Vertex_input_state_data& data)
{
    m_data = data;
    update();
}

void Vertex_input_state_impl::reset()
{
}

void Vertex_input_state_impl::create()
{
    update();
}

void Vertex_input_state_impl::update()
{
}

auto Vertex_input_state_impl::get_data() const -> const Vertex_input_state_data&
{
    return m_data;
}

} // namespace erhe::graphics
