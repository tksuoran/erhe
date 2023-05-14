#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <gsl/gsl>

#include <memory>
#include <vector>

namespace erhe::graphics
{

class Shader_resource;

/// Describes vertex data layout in buffer / memory
class Vertex_format final
{
public:
    Vertex_format();
    explicit Vertex_format(const std::initializer_list<Vertex_attribute> attributes);

    void add_attribute(
        const Vertex_attribute& attribute
    );

    [[nodiscard]] auto has_attribute(
        Vertex_attribute::Usage_type usage_type,
        unsigned int                 index = 0
    ) const -> bool;

    [[nodiscard]] auto find_attribute_maybe(
        Vertex_attribute::Usage_type usage_type,
        unsigned int                 index = 0
    ) const -> const Vertex_attribute*;

    [[nodiscard]] auto find_attribute(
        Vertex_attribute::Usage_type usage_type,
        unsigned int                 index = 0
    ) const -> gsl::not_null<const Vertex_attribute*>;

    [[nodiscard]] auto stride() const -> size_t
    {
        return m_stride;
    }

    [[nodiscard]] auto match(const Vertex_format& other) const -> bool;

    void add_to(Shader_resource& shader_resource);

private:
    void align_to(std::size_t alignment);

    std::vector<Vertex_attribute> m_attributes;
    std::size_t                   m_stride{0};
};

} // namespace erhe::graphics
