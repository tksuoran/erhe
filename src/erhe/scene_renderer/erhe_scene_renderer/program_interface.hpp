#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/glyph_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <memory>

namespace erhe::graphics { class Device; }

namespace erhe::scene_renderer {

class Mesh_memory;

struct Program_interface_config
{
    std::vector<std::filesystem::path> shader_paths;

    int max_camera_count   {256};
    int max_joint_count    {1000};
    int max_light_count    {32};
    int max_material_count {1000};
    int max_primitive_count{6000};
    int max_draw_count     {6000};
    // Number of per-pass views packed into the camera UBO's cameras[N]
    // array. 1 = single-view (default). 2 = stereo / OpenXR multiview.
    // Vulkan multiview requires the device feature to be enabled and
    // is wired up only on the multiview render path.
    int view_count     {1};
};

class Program_interface
{
public:
    Program_interface(
        erhe::graphics::Device&   graphics_device,
        Mesh_memory&              mesh_memory,
        Program_interface_config& config
    );

    Program_interface(const Program_interface&) = delete;
    void operator=   (const Program_interface&) = delete;
    Program_interface(Program_interface&&)      = delete;
    void operator=   (Program_interface&&)      = delete;

    [[nodiscard]] auto make_prototype(erhe::graphics::Shader_stages_create_info&& create_info) -> erhe::graphics::Shader_stages_prototype;
    [[nodiscard]] auto make_prototype(erhe::graphics::Shader_stages_create_info& create_info) -> erhe::graphics::Shader_stages_prototype;
    [[nodiscard]] auto make_program  (erhe::graphics::Shader_stages_prototype&& prototype) -> erhe::graphics::Shader_stages;

    erhe::graphics::Device&                            graphics_device;
    erhe::graphics::Fragment_outputs                   fragment_outputs;
    Mesh_memory&                                       mesh_memory;
    Program_interface_config                           config;
    Camera_interface                                   camera_interface;
    Cube_interface                                     cube_interface;
    Glyph_interface                                    glyph_interface;
    Joint_interface                                    joint_interface;
    Light_interface                                    light_interface;
    Material_interface                                 material_interface;
    Primitive_interface                                primitive_interface;
    std::unique_ptr<erhe::graphics::Bind_group_layout> bind_group_layout;
};

} // namespace erhe::scene_renderer
