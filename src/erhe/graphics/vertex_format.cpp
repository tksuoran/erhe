#include "erhe/graphics/vertex_format.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/assert>

#include <algorithm>
#include <cassert>

namespace erhe::graphics
{

void Vertex_format::clear()
{
    m_attributes.clear();
    m_stride = 0;
}

void Vertex_format::add(
    const Vertex_attribute& attribute
)
{
    Expects(
        (attribute.data_type.dimension >= 1) &&
        (attribute.data_type.dimension <= 4)
    );

    const size_t stride = attribute.data_type.dimension * size_of_type(attribute.data_type.type);

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

    auto& new_attribute = m_attributes.emplace_back(attribute);
    new_attribute.offset = m_stride;
    assert(stride == m_attributes.back().stride());
    m_stride += stride;
}

auto Vertex_format::match(const Vertex_format& other) const -> bool
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

auto Vertex_format::has_attribute(
    const Vertex_attribute::Usage_type usage_type,
    const unsigned int                 index
) const -> bool
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

auto Vertex_format::find_attribute_maybe(
    const Vertex_attribute::Usage_type usage_type,
    const unsigned int                 index
) const -> const Vertex_attribute*
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

auto Vertex_format::find_attribute(
    const Vertex_attribute::Usage_type usage_type,
    const unsigned int                 index
) const -> gsl::not_null<const Vertex_attribute*>
{
    for (const auto& i : m_attributes)
    {
        if ((i.usage.type == usage_type) && (i.usage.index == index))
        {
            return &(i);
        }
    }

    ERHE_FATAL("vertex_attribute not found\n");
}

} // namespace erhe::graphics
