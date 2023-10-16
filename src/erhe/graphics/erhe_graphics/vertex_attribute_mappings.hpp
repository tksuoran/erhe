#pragma once

#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_attribute_mapping.hpp"

#include <gsl/pointers>

#include <igl/Buffer.h>
//#include <igl/Device.h>

namespace igl {
    class IDevice;
}

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Buffer;
class Instance;
class Vertex_input_state;
class Vertex_format;

class Vertex_input_attribute
{
public:
    uint32_t                   layout_location{0};
    const igl::IBuffer*        vertex_buffer;
    uint32_t                   stride;
    Glsl_attribute_type        shader_type; // encodes dimension
    igl::VertexAttributeFormat data_type;
    std::size_t                offset;
    uint32_t                   divisor;
};

class Vertex_attribute_mappings final
{
public:
    explicit Vertex_attribute_mappings(igl::IDevice& device);

    Vertex_attribute_mappings(
        igl::IDevice&                                         device,
        const std::initializer_list<Vertex_attribute_mapping> mappings_in
    );

    void collect_attributes(
        std::vector<Vertex_input_attribute>& attributes,
        const igl::IBuffer*                  vertex_buffer,
        const Vertex_format&                 vertex_format
    ) const;

    std::vector<Vertex_attribute_mapping> mappings;

private:
    igl::IDevice& m_device;
};

} // namespace erhe::graphics
