#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <gsl/gsl>

#include <memory>
#include <vector>

namespace erhe::graphics
{

class Vertex_format final
{
public:
    Vertex_format();
    ~Vertex_format();

    void clear();

    void make_attribute(
        const Vertex_attribute::Usage     usage,
        const gl::Attribute_type          shader_type,
        const Vertex_attribute::Data_type data_type
    );

    auto has_attribute(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> bool;

    auto find_attribute_maybe(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> const Vertex_attribute*;

    auto find_attribute(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> gsl::not_null<const Vertex_attribute*>;

    auto stride() const -> size_t
    {
        return m_stride;
    }

    auto match(const Vertex_format& other) const -> bool;

private:
    std::vector<Vertex_attribute> m_attributes;
    size_t                        m_stride{0};
};

} // namespace erhe::graphics
