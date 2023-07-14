#pragma once

#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/shader_stages.hpp"

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
    debug_vdotn,
    debug_ldotn,
    debug_hdotv,
    debug_joint_indices,
    debug_joint_weights,
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
    "Debug V.N",
    "Debug L.N",
    "Debug H.V",
    "Debug Joint Indices",
    "Debug Joint Weights",
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

    erhe::graphics::Reloadable_shader_stages brdf_slice;
    erhe::graphics::Reloadable_shader_stages brush;
    erhe::graphics::Reloadable_shader_stages standard;
    erhe::graphics::Reloadable_shader_stages anisotropic_slope;
    erhe::graphics::Reloadable_shader_stages anisotropic_engine_ready;
    erhe::graphics::Reloadable_shader_stages circular_brushed_metal;
    erhe::graphics::Reloadable_shader_stages textured;
    erhe::graphics::Reloadable_shader_stages sky;
    erhe::graphics::Reloadable_shader_stages wide_lines_draw_color;
    erhe::graphics::Reloadable_shader_stages wide_lines_vertex_color;
    erhe::graphics::Reloadable_shader_stages points;
    erhe::graphics::Reloadable_shader_stages depth;
    erhe::graphics::Reloadable_shader_stages id;
    erhe::graphics::Reloadable_shader_stages tool;
    erhe::graphics::Reloadable_shader_stages debug_depth;
    erhe::graphics::Reloadable_shader_stages debug_normal;
    erhe::graphics::Reloadable_shader_stages debug_tangent;
    erhe::graphics::Reloadable_shader_stages debug_bitangent;
    erhe::graphics::Reloadable_shader_stages debug_texcoord;
    erhe::graphics::Reloadable_shader_stages debug_vertex_color_rgb;
    erhe::graphics::Reloadable_shader_stages debug_vertex_color_alpha;
    erhe::graphics::Reloadable_shader_stages debug_vdotn;
    erhe::graphics::Reloadable_shader_stages debug_ldotn;
    erhe::graphics::Reloadable_shader_stages debug_hdotv;
    erhe::graphics::Reloadable_shader_stages debug_joint_indices;
    erhe::graphics::Reloadable_shader_stages debug_joint_weights;
    erhe::graphics::Reloadable_shader_stages debug_omega_o;
    erhe::graphics::Reloadable_shader_stages debug_omega_i;
    erhe::graphics::Reloadable_shader_stages debug_omega_g;
    erhe::graphics::Reloadable_shader_stages debug_misc;

    class Shader_stages_builder
    {
    public:
        Shader_stages_builder(
            erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages,
            erhe::graphics::Instance&                 graphics_instance,
            erhe::scene_renderer::Program_interface&  program_interface,
            std::filesystem::path                     shader_path
        );
        Shader_stages_builder(Shader_stages_builder&& other);

        erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages;
        erhe::graphics::Shader_stages_prototype   prototype;
    };
};

} // namespace editor
