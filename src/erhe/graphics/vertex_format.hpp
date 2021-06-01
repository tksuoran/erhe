#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <gsl/gsl>

#include <memory>
#include <vector>

namespace erhe::graphics
{

class Vertex_format
{
public:
    Vertex_format();
    ~Vertex_format();

    void clear();

    void make_attribute(Vertex_attribute::Usage     usage,
                        gl::Attribute_type          shader_type,
                        Vertex_attribute::Data_type data_type);
    //-> Vertex_attribute&;

    auto has_attribute(Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const
    -> bool;

    auto find_attribute_maybe(Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const
    -> const Vertex_attribute*;

    auto find_attribute(Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const
    -> gsl::not_null<const Vertex_attribute*>;

    auto stride() const
    -> size_t
    {
        return m_stride;
    }

    auto match(const Vertex_format& other) const
    -> bool;

private:
    std::vector<Vertex_attribute> m_attributes;
    size_t                        m_stride{0};
};

} // namespace erhe::graphics
