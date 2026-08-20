#include "operations/material_change_operation.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include <fmt/format.h>

namespace editor {

namespace {

// Draw lists (Draw_list_scene) partition by material fields that select the
// pipeline rather than only its uniforms: the blending class and the
// double-sided flag (Draw_list_key). Those are captured when a mesh is
// registered, so an in-place edit of a material already in use has to make
// the lists be rebuilt - unlike the per-pass bucketing path, which re-reads
// the material every frame. Everything else in Material_data reaches the
// shader through Material_buffer, which is re-uploaded each frame anyway.
[[nodiscard]] auto changes_draw_list_partitioning(
    const erhe::primitive::Material_data& before,
    const erhe::primitive::Material_data& after
) -> bool
{
    return
        (before.blending_mode != after.blending_mode) ||
        (before.double_sided  != after.double_sided ) ||
        (before.bxdf_model    != after.bxdf_model   ); // unlit is a shadow-list filter
}

void rebuild_draw_lists(App_context& context)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        if (draw_list_scene != nullptr) {
            draw_list_scene->rebuild_all();
        }
    }
}

} // anonymous namespace

Material_change_operation::Material_change_operation(
    const std::shared_ptr<erhe::primitive::Material>& material,
    const erhe::primitive::Material_data&             before,
    const erhe::primitive::Material_data&             after
)
    : m_material{material}
    , m_before{before}
    , m_after{after}
{
    set_description(fmt::format("Material change {}", m_material->get_name()));
    m_usership.set_user_label("undo stack: material change");
}

Material_change_operation::~Material_change_operation() noexcept = default;

void Material_change_operation::execute(App_context& context)
{
    if ((m_usership.get_state() == Asset_resolve_state::unresolved) && (context.asset_manager != nullptr)) {
        m_usership.adopt(*context.asset_manager, m_material);
    }
    // TODO Lock the item
    const bool repartition = changes_draw_list_partitioning(m_before, m_after);
    m_material->data = m_after;
    // R5.8: the edit dirties the material's defining container (undo is an
    // edit too - the file no longer matches the live state either way).
    if (context.asset_manager != nullptr) {
        context.asset_manager->mark_item_dirty(*m_material);
    }
    if (repartition) {
        rebuild_draw_lists(context);
    }
}

void Material_change_operation::undo(App_context& context)
{
    // TODO Lock the item
    const bool repartition = changes_draw_list_partitioning(m_after, m_before);
    m_material->data = m_before;
    if (context.asset_manager != nullptr) {
        context.asset_manager->mark_item_dirty(*m_material);
    }
    if (repartition) {
        rebuild_draw_lists(context);
    }
}

}
