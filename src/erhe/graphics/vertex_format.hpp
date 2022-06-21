#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <gsl/gsl>

#include <memory>
#include <vector>

namespace erhe::graphics
{

/// Describes vertex data layout in buffer / memory
class Vertex_format final
{
public:
    void clear();

    void add(
        const Vertex_attribute& attribute
    );

    [[nodiscard]] auto has_attribute(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> bool;

    [[nodiscard]] auto find_attribute_maybe(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> const Vertex_attribute*;

    [[nodiscard]] auto find_attribute(
        const Vertex_attribute::Usage_type usage_type,
        const unsigned int                 index = 0
    ) const -> gsl::not_null<const Vertex_attribute*>;

    [[nodiscard]] auto stride() const -> size_t
    {
        return m_stride;
    }

    [[nodiscard]] auto match(const Vertex_format& other) const -> bool;

private:
    std::vector<Vertex_attribute> m_attributes;
    std::size_t                   m_stride{0};
};

} // namespace erhe::graphics
