#pragma once

#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_attribute_mapping.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Buffer;
class Instance;
class Vertex_input_state;
class Vertex_format;

class Vertex_attribute_mappings final
{
public:
    explicit Vertex_attribute_mappings(
        erhe::graphics::Instance& instance
    );

    Vertex_attribute_mappings(
        erhe::graphics::Instance&                             instance,
        const std::initializer_list<Vertex_attribute_mapping> mappings
    );

    void collect_attributes(
        std::vector<Vertex_input_attribute>& attributes,
        const Buffer*                        vertex_buffer,
        const Vertex_format&                 vertex_format
    ) const;

    std::vector<Vertex_attribute_mapping> mappings;

private:
    erhe::graphics::Instance& m_instance;
};

} // namespace erhe::graphics
