#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

namespace erhe::graphics { class Device; }

namespace erhe::scene_renderer {

struct Program_interface_config
{
    int max_camera_count   {256};
    int max_joint_count    {1000};
    int max_light_count    {32};
    int max_material_count {1000};
    int max_primitive_count{6000};
    int max_draw_count     {6000};
};

class Program_interface
{
public:
    Program_interface(
        erhe::graphics::Device&            graphics_device,
        erhe::dataformat::Vertex_format&   vertex_format,
        const Program_interface_config&    config = {}
    );

    Program_interface(const Program_interface&) = delete;
    void operator=   (const Program_interface&) = delete;
    Program_interface(Program_interface&&)      = delete;
    void operator=   (Program_interface&&)      = delete;

    [[nodiscard]] auto make_prototype(
        const std::filesystem::path&                shader_path,
        erhe::graphics::Shader_stages_create_info&& create_info
    ) -> erhe::graphics::Shader_stages_prototype;

    [[nodiscard]] auto make_prototype(
        const std::filesystem::path&               shader_path,
        erhe::graphics::Shader_stages_create_info& create_info
    ) -> erhe::graphics::Shader_stages_prototype;

    [[nodiscard]] auto make_program(erhe::graphics::Shader_stages_prototype&& prototype) -> erhe::graphics::Shader_stages;

    erhe::graphics::Device&          graphics_device;
    erhe::graphics::Fragment_outputs fragment_outputs;
    erhe::dataformat::Vertex_format& vertex_format;
    Program_interface_config         config;
    Camera_interface                 camera_interface;
    Cube_interface                   cube_interface;
    Joint_interface                  joint_interface;
    Light_interface                  light_interface;
    Material_interface               material_interface;
    Primitive_interface              primitive_interface;
};

} // namespace erhe::scene_renderer
