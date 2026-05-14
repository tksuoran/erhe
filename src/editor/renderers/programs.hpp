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

// Per-call shader override that bypasses Standard_shader_variants. The
// "debug_*" / "anisotropic_*" / "circular_brushed_metal" values that
// used to live here moved onto Standard_variant_key axes:
//   - anisotropic_slope / anisotropic_engine_ready -> Bxdf_model
//   - circular_brushed_metal                       -> material.use_circular_brushed_metal
//   - debug_*                                      -> Shader_debug (per viewport)
// Only the handful that still represent independent override shaders
// remain.
enum class Shader_stages_variant : int {
    not_set,
    standard,
    error,
    debug_depth
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Not Set",
    "Standard",
    "Error",
    "Depth"
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
    erhe::scene_renderer::Variant_handle    textured;
    erhe::scene_renderer::Variant_handle    sky;
    erhe::scene_renderer::Variant_handle    grid;
    erhe::scene_renderer::Variant_handle    wide_lines_draw_color;
    erhe::scene_renderer::Variant_handle    wide_lines_vertex_color;
    erhe::scene_renderer::Variant_handle    points;
    erhe::scene_renderer::Variant_handle    id;
    erhe::scene_renderer::Variant_handle    tool;
    erhe::scene_renderer::Variant_handle    debug_depth;
    erhe::scene_renderer::Variant_handle    debug_shadow;

private:
    // Name -> handle map. Built in the ctor; backs get_multiview(name).
    // Each Variant_handle is owned by Programs (as a public member); the
    // map holds a non-owning pointer.
    std::unordered_map<std::string, erhe::scene_renderer::Variant_handle*> m_handles_by_name;
};

}
