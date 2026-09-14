#include "app_context.hpp"

#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_commit_queue.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

namespace editor {

void App_context::on_item_property_changed(erhe::Item_base& item, const erhe::property::Dependency_property& property)
{
    using erhe::property::Property_flags;
    const uint32_t flags = property.get_metadata(item.get_property_owner_type()).flags;

    // R5.8: an edit dirties the item's defining asset container (undo is an
    // edit too - the file no longer matches the live state either way).
    if ((asset_manager != nullptr) && ((flags & Property_flags::serialize) != 0)) {
        asset_manager->mark_item_dirty(item);
    }

    // Draw lists (Draw_list_scene) partition by material fields that select
    // the pipeline rather than only its uniforms (Draw_list_key: blending
    // class, double-sided, unlit shadow filter). Those are captured when a
    // mesh is registered, so an in-place edit of such a field has to make
    // the lists be rebuilt. The shader variant is derived from the material
    // at draw-list build time as well, so the same rebuild covers it.
    const bool rebuild_draw_lists =
        ((flags & Property_flags::affects_draw_list_partition) != 0) ||
        ((flags & Property_flags::affects_shader_variant) != 0);
    if (rebuild_draw_lists && (app_scenes != nullptr)) {
        for (const std::shared_ptr<Scene_root>& scene_root : app_scenes->get_scene_roots()) {
            if (!scene_root) {
                continue;
            }
            erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
            if (draw_list_scene != nullptr) {
                draw_list_scene->rebuild_all();
            }
        }
    }
}

auto App_context::get_async_in_flight_count() const -> std::size_t
{
    return
        static_cast<std::size_t>(pending_async_ops.load()) +
        static_cast<std::size_t>(running_async_ops.load()) +
        ((operation_stack    != nullptr) ? operation_stack->get_queued_count()     : 0u) +
        ((scene_commit_queue != nullptr) ? scene_commit_queue->get_pending_count() : 0u) +
        ((asset_manager      != nullptr) ? asset_manager->get_load_task_count()    : 0u);
}

auto App_context::is_scene_load_in_flight() const -> bool
{
    return get_async_in_flight_count() != 0;
}

}

