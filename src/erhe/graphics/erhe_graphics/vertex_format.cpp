#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cassert>

namespace erhe::graphics {

Vertex_format::Vertex_format()
{
}

Vertex_format::Vertex_format(std::initializer_list<Vertex_attribute> attributes)
{
    for (auto& attribute : attributes) {
        add_attribute(attribute);
    }
}

void Vertex_format::align_to(const std::size_t alignment)
{
    ERHE_VERIFY(alignment > 0);

    while ((m_stride % alignment) != 0) {
        ++m_stride;
    }
}

void Vertex_format::add_attribute(const Vertex_attribute& attribute)
{
    const std::size_t attribute_stride = erhe::dataformat::get_format_size(attribute.data_type);
    const std::size_t alignment = erhe::dataformat::get_component_byte_size(attribute.data_type);
    align_to(alignment);
    auto& new_attribute = m_attributes.emplace_back(attribute);
    new_attribute.offset = m_stride;
    m_stride += attribute_stride;
}

auto Vertex_format::match(const Vertex_format& other) const -> bool
{
    if (m_attributes.size() != other.m_attributes.size()) {
        return false;
    }

    for (size_t i = 0; i < m_attributes.size(); ++i) {
        if (m_attributes[i] != other.m_attributes[i]) {
            return false;
        }
    }

    return true;
}

auto Vertex_format::has_attribute(Vertex_attribute::Usage_type usage_type, unsigned int index) const -> bool
{
    for (const auto& i : m_attributes) {
        if ((i.usage.type == usage_type) && (i.usage.index == index)) {
            return true;
        }
    }

    return false;
}

auto Vertex_format::find_attribute_maybe(Vertex_attribute::Usage_type usage_type, unsigned int index) const
-> const Vertex_attribute*
{
    for (const auto& i : m_attributes) {
        if ((i.usage.type == usage_type) && (i.usage.index== index)) {
            return &(i);
        }
    }

    return nullptr;
}

auto Vertex_format::find_attribute(Vertex_attribute::Usage_type usage_type, unsigned int index) const
-> const Vertex_attribute*
{
    for (const auto& i : m_attributes) {
        if ((i.usage.type == usage_type) && (i.usage.index == index)) {
            return &(i);
        }
    }

    ERHE_FATAL("vertex_attribute not found");
}

void Vertex_format::add_to(Shader_resource& vertex_struct, Shader_resource& vertices_block)
{
    for (const auto& attribute : m_attributes) {
        vertex_struct.add(attribute);
    }
    vertices_block.add_struct("vertices", &vertex_struct, erhe::graphics::Shader_resource::unsized_array);
}

} // namespace erhe::graphics
