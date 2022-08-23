#pragma once

#include "renderers/camera_buffer.hpp"
#include "renderers/light_buffer.hpp"
#include "renderers/material_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"

#include <memory>

namespace erhe::graphics
{

class Sampler;
class Shader_stages;

} // namespace erhe::graphics

namespace editor {

class Program_interface
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Program_interface"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Program_interface ();
    ~Program_interface() noexcept override;
    Program_interface (const Program_interface&) = delete;
    void operator=    (const Program_interface&) = delete;
    Program_interface (Program_interface&&)      = delete;
    void operator=    (Program_interface&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    class Shader_resources
    {
    public:
        Shader_resources(
            std::size_t max_material_count,
            std::size_t max_light_count,
            std::size_t max_camera_count,
            std::size_t max_primitive_count
        );

        erhe::graphics::Fragment_outputs          fragment_outputs;
        erhe::graphics::Vertex_attribute_mappings attribute_mappings;

        Camera_interface    camera_interface;
        Light_interface     light_interface;
        Material_interface  material_interface;
        Primitive_interface primitive_interface;
    };

    std::unique_ptr<Shader_resources> shader_resources;
};

}
