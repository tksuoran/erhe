#include "scene/draw_list_scene_dependencies.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "renderers/material_set_factory.hpp"

#include "erhe_scene_renderer/program_interface.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_session.hpp"
#endif

namespace editor {

auto make_draw_list_scene_dependencies(App_context& context) -> Draw_list_scene_dependencies
{
    Draw_list_scene_dependencies dependencies{};
    dependencies.mesh_memory          = context.mesh_memory;
    dependencies.graphics_device      = context.graphics_device;
    dependencies.shader_variant_cache = context.shader_variant_cache;
    dependencies.primitive_interface  = (context.program_interface != nullptr) ? &context.program_interface->primitive_interface : nullptr;
    dependencies.gpu_cull_classifier  = context.gpu_cull_classifier;
    dependencies.exclude_unlit_from_shadows =
        (context.editor_settings != nullptr) && context.editor_settings->exclude_unlit_primitives;
    // The draw-list set holds the materials of a root that renders content,
    // so it gets the full texture heap limit.
    if (context.material_set_factory != nullptr) {
        dependencies.material_set_create_info = context.material_set_factory->make_create_info("Draw list material set");
    }

    // Same view-count enumeration as prewarm.cpp: single view (key value 0)
    // always; the XR view count when the headset session renders multiview.
    dependencies.multiview_view_counts.push_back(0u);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (context.headset_view != nullptr) {
        erhe::xr::Headset* headset = context.headset_view->get_headset();
        if (headset != nullptr) {
            erhe::xr::Xr_session* xr_session = headset->get_xr_session();
            if ((xr_session != nullptr) && xr_session->is_multiview_enabled()) {
                const uint32_t xr_view_count = xr_session->get_view_count();
                if (xr_view_count > 1u) {
                    dependencies.multiview_view_counts.push_back(xr_view_count);
                }
            }
        }
    }
#endif
    return dependencies;
}

auto make_scene_root_material_set_create_info(
    App_context& context,
    const char*  debug_label
) -> erhe::scene_renderer::Material_set_create_info
{
    if (context.material_set_factory == nullptr) {
        return erhe::scene_renderer::Material_set_create_info{};
    }
    return context.material_set_factory->make_create_info(debug_label);
}

} // namespace editor
