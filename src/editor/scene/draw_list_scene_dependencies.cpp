#include "scene/draw_list_scene_dependencies.hpp"

#include "app_context.hpp"

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
    dependencies.shader_variant_cache = context.shader_variant_cache;

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

} // namespace editor
