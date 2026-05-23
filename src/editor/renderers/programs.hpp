#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"

#include <functional>
#include <string_view>

namespace erhe::graphics       { class Device; }
namespace erhe::scene_renderer {
    class Mesh_memory;
    class Program_interface;
}
namespace tf                   { class Executor; }

namespace editor {

class Init_status_display;

class Programs
{
public:
    Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& program_interface);

    void load_programs(
        tf::Executor&                            executor,
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        erhe::scene_renderer::Program_interface& program_interface,
        Init_status_display&                     init_status_display
    );

    // Public members
    std::vector<std::filesystem::path>       shader_paths;
    erhe::graphics::Reloadable_shader_stages brdf_slice;
    erhe::graphics::Reloadable_shader_stages sky;
    erhe::graphics::Reloadable_shader_stages grid;
    erhe::graphics::Reloadable_shader_stages wide_lines_draw_color;
    erhe::graphics::Reloadable_shader_stages wide_lines_vertex_color;
    //erhe::graphics::Reloadable_shader_stages points;

    // TODO remove - use variants
    //erhe::graphics::Reloadable_shader_stages error;
    //erhe::graphics::Reloadable_shader_stages id;
    //erhe::graphics::Reloadable_shader_stages brush;
    //erhe::graphics::Reloadable_shader_stages textured;
    erhe::graphics::Reloadable_shader_stages depth_to_color;
    erhe::graphics::Reloadable_shader_stages tool;

    class Shader_stages_builder
    {
    public:
        Shader_stages_builder(
            erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages,
            erhe::scene_renderer::Program_interface&  program_interface
        );
        Shader_stages_builder(Shader_stages_builder&& other) noexcept;

        erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages;
        erhe::graphics::Shader_stages_prototype   prototype;
    };
};

}
