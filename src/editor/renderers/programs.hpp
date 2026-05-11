#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace erhe::graphics       { class Device; }
namespace erhe::scene_renderer { class Program_interface; }
namespace tf                   { class Executor; }

namespace editor {

using Init_message_fn = std::function<void(std::string_view)>;

enum class Shader_stages_variant : int {
    not_set,
    standard,
    error,
    anisotropic_slope,
    anisotropic_engine_ready,
    circular_brushed_metal,
    debug_depth,
    debug_vertex_normal,
    debug_fragment_normal,
    debug_normal_texture,
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
    debug_metallic,
    debug_roughness,
    debug_occlusion,
    debug_emissive,
    debug_shadowmap_texels,
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
    "Vertex Normal",
    "Fragment Normal",
    "Normal Texture",
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
    "Metallic",
    "Roughness",
    "Occlusion",
    "Emissive",
    "Shadowmap Texels",
    "Debug Miscellaneous"
};

class Programs
{
public:
    Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& program_interface);

    void load_programs(
        tf::Executor&                            executor,
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface,
        const Init_message_fn&                   init_message
    );

    [[nodiscard]] auto get_variant_shader_stages(Shader_stages_variant variant) const -> const erhe::graphics::Shader_stages*;

    // Look up the multiview-compiled variant of a shader by its
    // create_info name (e.g. "standard", "wide_lines"). Returns
    // nullptr when multiview was disabled at startup or when the
    // requested shader was never registered. The single-view variants
    // remain accessible via the Reloadable_shader_stages fields above
    // and are unchanged by multiview being on or off.
    [[nodiscard]] auto get_multiview(std::string_view name) const -> const erhe::graphics::Shader_stages*;

    // Public members
    std::vector<std::filesystem::path>       shader_paths;
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
    erhe::graphics::Reloadable_shader_stages wide_lines_draw_color;
    erhe::graphics::Reloadable_shader_stages wide_lines_vertex_color;
    erhe::graphics::Reloadable_shader_stages points;
    erhe::graphics::Reloadable_shader_stages depth;
    erhe::graphics::Reloadable_shader_stages id;
    erhe::graphics::Reloadable_shader_stages tool;
    erhe::graphics::Reloadable_shader_stages debug_depth;
    erhe::graphics::Reloadable_shader_stages debug_vertex_normal;
    erhe::graphics::Reloadable_shader_stages debug_fragment_normal;
    erhe::graphics::Reloadable_shader_stages debug_normal_texture;
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
    erhe::graphics::Reloadable_shader_stages debug_metallic;
    erhe::graphics::Reloadable_shader_stages debug_roughness;
    erhe::graphics::Reloadable_shader_stages debug_occlusion;
    erhe::graphics::Reloadable_shader_stages debug_emissive;
    erhe::graphics::Reloadable_shader_stages debug_shadowmap_texels;
    erhe::graphics::Reloadable_shader_stages debug_shadow;
    erhe::graphics::Reloadable_shader_stages debug_misc;

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

private:
    // Multiview shader variants keyed by create_info.name. Populated
    // during load_programs() when program_interface.config.max_view_count
    // >= 2; empty otherwise. Each entry holds a Reloadable_shader_stages
    // whose create_info has enable_multiview(view_count) applied.
    std::map<std::string, std::unique_ptr<erhe::graphics::Reloadable_shader_stages>> m_multiview_variants;
};

}
