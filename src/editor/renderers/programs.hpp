#pragma once

#include "erhe_scene_renderer/variant_handle.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe::graphics       { class Device; class Shader_stages; }
namespace erhe::scene_renderer { class Program_interface; class Shader_variant_cache; }
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

// Owns one Variant_handle per shader the editor uses. Each handle holds
// a Shader_variant_key and resolves through the shared
// Shader_variant_cache; the cache compiles on miss.
//
// Multiview siblings live inside the same handle:
// handle.shader_stages() returns the single-view stages,
// handle.multiview_shader_stages() returns the multiview-compiled
// sibling (or nullptr when the editor was built with
// max_view_count < 2).
class Programs
{
public:
    Programs(
        erhe::graphics::Device&                     graphics_device,
        erhe::scene_renderer::Program_interface&    program_interface,
        erhe::scene_renderer::Shader_variant_cache& cache
    );

    void load_programs(
        tf::Executor&                            executor,
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface,
        const Init_message_fn&                   init_message
    );

    [[nodiscard]] auto get_variant_shader_stages(Shader_stages_variant variant) -> const erhe::graphics::Shader_stages*;

    // Look up the multiview-compiled sibling of a shader by name. Returns
    // nullptr when the editor was built with max_view_count < 2 or when
    // no shader matches the name.
    [[nodiscard]] auto get_multiview(std::string_view name) -> const erhe::graphics::Shader_stages*;

    std::vector<std::filesystem::path>            shader_paths;

    erhe::scene_renderer::Variant_handle    error;
    erhe::scene_renderer::Variant_handle    brdf_slice;
    erhe::scene_renderer::Variant_handle    brush;
    erhe::scene_renderer::Variant_handle    standard;
    erhe::scene_renderer::Variant_handle    anisotropic_slope;
    erhe::scene_renderer::Variant_handle    anisotropic_engine_ready;
    erhe::scene_renderer::Variant_handle    circular_brushed_metal;
    erhe::scene_renderer::Variant_handle    textured;
    erhe::scene_renderer::Variant_handle    sky;
    erhe::scene_renderer::Variant_handle    grid;
    erhe::scene_renderer::Variant_handle    wide_lines_draw_color;
    erhe::scene_renderer::Variant_handle    wide_lines_vertex_color;
    erhe::scene_renderer::Variant_handle    points;
    erhe::scene_renderer::Variant_handle    id;
    erhe::scene_renderer::Variant_handle    tool;
    erhe::scene_renderer::Variant_handle    debug_depth;
    erhe::scene_renderer::Variant_handle    debug_vertex_normal;
    erhe::scene_renderer::Variant_handle    debug_fragment_normal;
    erhe::scene_renderer::Variant_handle    debug_normal_texture;
    erhe::scene_renderer::Variant_handle    debug_tangent;
    erhe::scene_renderer::Variant_handle    debug_vertex_tangent_w;
    erhe::scene_renderer::Variant_handle    debug_bitangent;
    erhe::scene_renderer::Variant_handle    debug_texcoord;
    erhe::scene_renderer::Variant_handle    debug_base_color_texture;
    erhe::scene_renderer::Variant_handle    debug_vertex_color_rgb;
    erhe::scene_renderer::Variant_handle    debug_vertex_color_alpha;
    erhe::scene_renderer::Variant_handle    debug_aniso_strength;
    erhe::scene_renderer::Variant_handle    debug_aniso_texcoord;
    erhe::scene_renderer::Variant_handle    debug_vdotn;
    erhe::scene_renderer::Variant_handle    debug_ldotn;
    erhe::scene_renderer::Variant_handle    debug_hdotv;
    erhe::scene_renderer::Variant_handle    debug_joint_indices;
    erhe::scene_renderer::Variant_handle    debug_joint_weights;
    erhe::scene_renderer::Variant_handle    debug_omega_o;
    erhe::scene_renderer::Variant_handle    debug_omega_i;
    erhe::scene_renderer::Variant_handle    debug_omega_g;
    erhe::scene_renderer::Variant_handle    debug_vertex_valency;
    erhe::scene_renderer::Variant_handle    debug_polygon_edge_count;
    erhe::scene_renderer::Variant_handle    debug_metallic;
    erhe::scene_renderer::Variant_handle    debug_roughness;
    erhe::scene_renderer::Variant_handle    debug_occlusion;
    erhe::scene_renderer::Variant_handle    debug_emissive;
    erhe::scene_renderer::Variant_handle    debug_shadowmap_texels;
    erhe::scene_renderer::Variant_handle    debug_shadow;
    erhe::scene_renderer::Variant_handle    debug_misc;

private:
    // Name -> handle map. Built in the ctor; backs get_multiview(name).
    // Each Variant_handle is owned by Programs (as a public member); the
    // map holds a non-owning pointer.
    std::unordered_map<std::string, erhe::scene_renderer::Variant_handle*> m_handles_by_name;
};

}
