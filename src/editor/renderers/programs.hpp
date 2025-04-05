#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"

namespace erhe::graphics {
    class Instance;
}
namespace erhe::scene_renderer {
    class Program_interface;
}
namespace tf {
    class Executor;
}

namespace editor {

enum class Shader_stages_variant : int {
    not_set,
    standard,
    error,
    anisotropic_slope,
    anisotropic_engine_ready,
    circular_brushed_metal,
    debug_depth,
    debug_normal,
    debug_tangent,
    debug_vertex_tangent_w,
    debug_bitangent,
    debug_texcoord,
    debug_base_color_texture,
    debug_vertex_color_rgb,
    debug_vertex_color_alpha,
    debug_aniso_strength,
    debug_aniso_texcoord,
    debug_vdotn,
    debug_ldotn,
    debug_hdotv,
    debug_joint_indices,
    debug_joint_weights,
    debug_omega_o,
    debug_omega_i,
    debug_omega_g,
    debug_vertex_valency,
    debug_polygon_edge_count,
    debug_misc
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Not Set",
    "Standard",
    "Error",
    "Anisotropic Slope",
    "Anisotropic Engine-Ready",
    "Circular Brushed Metal",
    "Depth",
    "Normal",
    "Tangent",
    "Vertex Tangent W",
    "Bitangent",
    "TexCoord",
    "Base Color Texture",
    "Vertex Color RGB",
    "Vertex Color Alpha",
    "Aniso Strength",
    "Aniso TexCoord",
    "V.N",
    "L.N",
    "H.V",
    "Joint Indices",
    "Joint Weights",
    "Omega o",
    "Omega i",
    "Omega g",
    "Vertex Valency",
    "Polygon Edge Count",
    "Debug Miscellaneous"
};

class Programs
{
public:
    static constexpr std::size_t s_texture_unit_count = 15; // for non bindless textures

    Programs(erhe::graphics::Instance& graphics_instance);

    void load_programs(tf::Executor& executor, erhe::graphics::Instance& graphics_instance, erhe::scene_renderer::Program_interface& program_interface);

    [[nodiscard]] auto get_variant_shader_stages(Shader_stages_variant variant) const -> const erhe::graphics::Shader_stages*;

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

    erhe::graphics::Reloadable_shader_stages error;
    erhe::graphics::Reloadable_shader_stages brdf_slice;
    erhe::graphics::Reloadable_shader_stages brush;
    erhe::graphics::Reloadable_shader_stages standard;
    erhe::graphics::Reloadable_shader_stages anisotropic_slope;
    erhe::graphics::Reloadable_shader_stages anisotropic_engine_ready;
    erhe::graphics::Reloadable_shader_stages circular_brushed_metal;
    erhe::graphics::Reloadable_shader_stages textured;
    erhe::graphics::Reloadable_shader_stages sky;
    erhe::graphics::Reloadable_shader_stages grid;
    erhe::graphics::Reloadable_shader_stages fat_triangle;
    erhe::graphics::Reloadable_shader_stages wide_lines_draw_color;
    erhe::graphics::Reloadable_shader_stages wide_lines_vertex_color;
    erhe::graphics::Reloadable_shader_stages points;
    erhe::graphics::Reloadable_shader_stages depth;
    erhe::graphics::Reloadable_shader_stages id;
    erhe::graphics::Reloadable_shader_stages tool;
    erhe::graphics::Reloadable_shader_stages debug_depth;
    erhe::graphics::Reloadable_shader_stages debug_normal;
    erhe::graphics::Reloadable_shader_stages debug_tangent;
    erhe::graphics::Reloadable_shader_stages debug_vertex_tangent_w;
    erhe::graphics::Reloadable_shader_stages debug_bitangent;
    erhe::graphics::Reloadable_shader_stages debug_texcoord;
    erhe::graphics::Reloadable_shader_stages debug_base_color_texture;
    erhe::graphics::Reloadable_shader_stages debug_vertex_color_rgb;
    erhe::graphics::Reloadable_shader_stages debug_vertex_color_alpha;
    erhe::graphics::Reloadable_shader_stages debug_aniso_strength;
    erhe::graphics::Reloadable_shader_stages debug_aniso_texcoord;
    erhe::graphics::Reloadable_shader_stages debug_vdotn;
    erhe::graphics::Reloadable_shader_stages debug_ldotn;
    erhe::graphics::Reloadable_shader_stages debug_hdotv;
    erhe::graphics::Reloadable_shader_stages debug_joint_indices;
    erhe::graphics::Reloadable_shader_stages debug_joint_weights;
    erhe::graphics::Reloadable_shader_stages debug_omega_o;
    erhe::graphics::Reloadable_shader_stages debug_omega_i;
    erhe::graphics::Reloadable_shader_stages debug_omega_g;
    erhe::graphics::Reloadable_shader_stages debug_vertex_valency;
    erhe::graphics::Reloadable_shader_stages debug_polygon_edge_count;
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
        //Shader_stages_builder& operator=(Shader_stages_builder&& other);

        erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages;
        erhe::graphics::Shader_stages_prototype   prototype;
    };
};

} // namespace editor
