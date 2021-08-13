#include "erhe/graphics/vertex_format.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>
#include <cassert>
#include <gsl/assert>

namespace erhe::graphics
{

Vertex_format::Vertex_format() = default;

Vertex_format::~Vertex_format() = default;


void Vertex_format::clear()
{
    m_attributes.clear();
    m_stride = 0;
}

void Vertex_format::make_attribute(const Vertex_attribute::Usage     usage,
                                   const gl::Attribute_type          shader_type,
                                   const Vertex_attribute::Data_type data_type)
//-> Vertex_attribute&
{
    Expects((data_type.dimension >= 1) && (data_type.dimension <= 4));

    const size_t stride = data_type.dimension * size_of_type(data_type.type);

    // Align attributes to their type
    switch (stride)
    {
        case 1:
        {
            break;
        }

        case 2:
        {
            while ((m_stride & 1) != 0)
            {
                ++m_stride;
            }
            break;
        }

        default:
        case 4:
        {
            while ((m_stride & 3) != 0)
            {
                ++m_stride;
            }
            break;
        }
    }

    m_attributes.emplace_back(usage, shader_type, data_type, m_stride, 0);
    auto& attribute = m_attributes.back();

    assert(stride == attribute.stride());
    m_stride += stride;
    //return attribute;
}

auto Vertex_format::match(const Vertex_format& other) const
-> bool
{
    if (m_attributes.size() != other.m_attributes.size())
    {
        return false;
    }

    for (size_t i = 0; i < m_attributes.size(); ++i)
    {
        if (m_attributes[i] != other.m_attributes[i])
        {
            return false;
        }
    }

    return true;
}

auto Vertex_format::has_attribute(const Vertex_attribute::Usage_type usage_type, const unsigned int index) const
-> bool
{
    for (const auto& i : m_attributes)
    {
        if ((i.usage.type == usage_type) && (i.usage.index == index))
        {
            return true;
        }
    }

    return false;
}

auto Vertex_format::find_attribute_maybe(const Vertex_attribute::Usage_type usage_type, const unsigned int index) const
-> const Vertex_attribute*
{
    for (const auto& i : m_attributes)
    {
        if ((i.usage.type == usage_type) && (i.usage.index== index))
        {
            return &(i);
        }
    }

    return nullptr;
}

auto Vertex_format::find_attribute(const Vertex_attribute::Usage_type usage_type, const unsigned int index) const
-> gsl::not_null<const Vertex_attribute*>
{
    for (const auto& i : m_attributes)
    {
        if ((i.usage.type == usage_type) && (i.usage.index == index))
        {
            return &(i);
        }
    }

    FATAL("vertex_attribute not found");
}

} // namespace erhe::graphics
