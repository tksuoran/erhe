#pragma once

#include <cstdint>
#include <vector>

namespace erhe::scene_renderer {
    class Mesh_memory;
    class Primitive_interface;
    class Shader_variant_cache;
}

namespace editor {

class App_context;

// What Scene_root needs to own a Draw_list_scene
// (doc/draw_list_renderer_requirements.md R1b). Scene roots constructed
// without these (nullptr) get no Draw_list_scene and their passes render
// through the Forward_renderer / Shadow_renderer fallback.
class Draw_list_scene_dependencies
{
public:
    erhe::scene_renderer::Mesh_memory*          mesh_memory         {nullptr};
    erhe::scene_renderer::Shader_variant_cache* shader_variant_cache{nullptr};
    // Layout of the per-entry primitive records
    // (Program_interface::primitive_interface, shared by every renderer).
    erhe::scene_renderer::Primitive_interface*  primitive_interface {nullptr};
    // Multiview view counts to resolve up front (R19): 0 = single view,
    // plus the XR view count when a multiview headset session is active.
    std::vector<uint32_t>                       multiview_view_counts{};

    [[nodiscard]] auto is_valid() const -> bool
    {
        return (mesh_memory != nullptr) && (shader_variant_cache != nullptr) && (primitive_interface != nullptr);
    }
};

// Collects the dependencies from the running editor. Valid only after
// Editor::fill_app_context() (mesh_memory / shader_variant_cache pointers
// set); returns an invalid (all-null) instance before that.
[[nodiscard]] auto make_draw_list_scene_dependencies(App_context& context) -> Draw_list_scene_dependencies;

} // namespace editor
