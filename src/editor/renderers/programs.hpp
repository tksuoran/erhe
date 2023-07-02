#pragma once

#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/shader_stages.hpp"

#include <memory>

namespace erhe::graphics {
    class Instance;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor {

enum class Shader_stages_variant : int
{
    standard,
    anisotropic_slope,
    anisotropic_engine_ready,
    circular_brushed_metal,
    debug_depth,
    debug_normal,
    debug_tangent,
    debug_bitangent,
    debug_texcoord,
    debug_vertex_color_rgb,
    debug_vertex_color_alpha,
    debug_omega_o,
    debug_omega_i,
    debug_omega_g,
    debug_misc
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Standard",
    "Anisotropic Slope",
    "Anisotropic Engine-Ready",
    "Circular Brushed Metal",
    "Debug Depth",
    "Debug Normal",
    "Debug Tangent",
    "Debug Bitangent",
    "Debug TexCoord",
    "Debug Vertex Color RGB",
    "Debug Vertex Color Alpha",
    "Debug Omega o",
    "Debug Omega i",
    "Debug Omega g",
    "Debug Miscellaneous"
};

class Programs
{
public:
    static constexpr std::size_t s_texture_unit_count = 15; // for non bindless textures

    Programs(
        erhe::graphics::Instance&                graphics_instance,
        erhe::scene_renderer::Program_interface& program_interface
    );

    // Public members
    int                              shadow_texture_unit{15};
    int                              base_texture_unit{0};
    std::filesystem::path            shader_path;
    erhe::graphics::Shader_resource  default_uniform_block; // for non-bindless textures
    erhe::graphics::Shader_resource* shadow_sampler;
    erhe::graphics::Shader_resource* texture_sampler;

    erhe::graphics::Sampler          nearest_sampler;
    erhe::graphics::Sampler          linear_sampler;
    erhe::graphics::Sampler          linear_mipmap_linear_sampler;

    erhe::graphics::Shader_stages    brdf_slice;
    erhe::graphics::Shader_stages    brush;
    erhe::graphics::Shader_stages    standard;
    erhe::graphics::Shader_stages    anisotropic_slope;
    erhe::graphics::Shader_stages    anisotropic_engine_ready;
    erhe::graphics::Shader_stages    circular_brushed_metal;
    erhe::graphics::Shader_stages    textured;
    erhe::graphics::Shader_stages    sky;
    erhe::graphics::Shader_stages    wide_lines_draw_color;
    erhe::graphics::Shader_stages    wide_lines_vertex_color;
    erhe::graphics::Shader_stages    points;
    erhe::graphics::Shader_stages    depth;
    erhe::graphics::Shader_stages    id;
    erhe::graphics::Shader_stages    tool;
    erhe::graphics::Shader_stages    debug_depth;
    erhe::graphics::Shader_stages    debug_normal;
    erhe::graphics::Shader_stages    debug_tangent;
    erhe::graphics::Shader_stages    debug_bitangent;
    erhe::graphics::Shader_stages    debug_texcoord;
    erhe::graphics::Shader_stages    debug_vertex_color_rgb;
    erhe::graphics::Shader_stages    debug_vertex_color_alpha;
    erhe::graphics::Shader_stages    debug_omega_o;
    erhe::graphics::Shader_stages    debug_omega_i;
    erhe::graphics::Shader_stages    debug_omega_g;
    erhe::graphics::Shader_stages    debug_misc;

    class Shader_stages_builder
    {
    public:
        Shader_stages_builder(
            erhe::graphics::Shader_stages&              shader_stages,
            erhe::graphics::Instance&                   graphics_instance,
            erhe::scene_renderer::Program_interface&    program_interface,
            std::filesystem::path                       shader_path,
            erhe::graphics::Shader_stages_create_info&& create_info
        );
        Shader_stages_builder(Shader_stages_builder&& other);

        erhe::graphics::Shader_stages&          shader_stages;
        erhe::graphics::Shader_stages_prototype prototype;
    };
};

} // namespace editor
