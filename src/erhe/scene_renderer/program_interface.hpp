#pragma once

#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene_renderer/camera_buffer.hpp"
#include "erhe/scene_renderer/joint_buffer.hpp"
#include "erhe/scene_renderer/light_buffer.hpp"
#include "erhe/scene_renderer/material_buffer.hpp"
#include "erhe/scene_renderer/primitive_buffer.hpp"

namespace erhe::graphics
{
    class Instance;
}

namespace erhe::scene_renderer
{

class Program_interface
{
public:
    explicit Program_interface(
        erhe::graphics::Instance& graphics_instance
    );

    Program_interface(const Program_interface&) = delete;
    void operator=   (const Program_interface&) = delete;
    Program_interface(Program_interface&&)      = delete;
    void operator=   (Program_interface&&)      = delete;

    [[nodiscard]] auto make_prototype(
        erhe::graphics::Instance&                   graphics_instance,
        const std::filesystem::path&                shader_path,
        erhe::graphics::Shader_stages_create_info&& create_info
    ) -> erhe::graphics::Shader_stages_prototype;

    [[nodiscard]] auto make_prototype(
        erhe::graphics::Instance&                  graphics_instance,
        const std::filesystem::path&               shader_path,
        erhe::graphics::Shader_stages_create_info& create_info
    ) -> erhe::graphics::Shader_stages_prototype;

    [[nodiscard]] auto make_program(
        erhe::graphics::Shader_stages_prototype&& prototype
    ) -> erhe::graphics::Shader_stages;

    erhe::graphics::Fragment_outputs          fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings attribute_mappings;

    Camera_interface    camera_interface;
    Joint_interface     joint_interface;
    Light_interface     light_interface;
    Material_interface  material_interface;
    Primitive_interface primitive_interface;
};

} // namespace erhe::scene_renderer
