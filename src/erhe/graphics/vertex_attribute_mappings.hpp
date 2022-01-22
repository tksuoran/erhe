#pragma once

#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mapping.hpp"

#include <gsl/pointers>

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Buffer;
class Vertex_input_state;
class Vertex_format;

class Vertex_attribute_mappings final
{
public:
    void add(
        const Vertex_attribute_mapping& attribute
    );

    void collect_attributes(
        std::vector<Vertex_input_attribute>& attributes,
        const Buffer*                        vertex_buffer,
        const Vertex_format&                 vertex_format
    ) const;

    std::vector<Vertex_attribute_mapping> mappings;
};

} // namespace erhe::graphics
