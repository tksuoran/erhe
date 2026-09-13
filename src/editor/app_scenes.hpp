#pragma once

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe {
    class Item_host;
}
namespace erhe::graphics {
    class Command_buffer;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class App_context;
class Scene_root;
class Time_context;

class App_scenes
{
public:
    explicit App_scenes(App_context& context);
    ~App_scenes() noexcept;

    void register_scene_root                 (const std::shared_ptr<Scene_root>& scene_root);
    void unregister_scene_root               (Scene_root* scene_root);
    // Forwards a scene's source-path change to the Asset_manager so the
    // scene's container record follows (R5.3: first save binds, save-as
    // re-homes). Called by Scene_root::set_source_path while registered.
    void notify_scene_source_path_changed    (Scene_root& scene_root);
    void sanity_check                        ();

    void before_physics_simulation_steps     ();
    void update_physics_simulation_fixed_step(const Time_context& time);
    void after_physics_simulation_steps      ();
    void update_layout_nodes                 ();
    void update_node_transforms              ();
    // Main thread, once per frame before any scene renders: applies queued
    // draw list changes of every registered scene root
    // (doc/draw_list_renderer_plan.md, threading contract).
    void flush_draw_lists                    ();
    // Main thread, once per frame before flush_draw_lists(): rebuilds the
    // primitives of every mesh whose Gprim.display_color changed, so the one
    // color of the surface is in the vertex data the renderers read
    // (Buffer_mesh::has_vertex_colors). Change-driven - a frame in which
    // nothing was written walks the registered roots and finds empty queues.
    void rebuild_display_colors              ();
    // Step 2 of the per-frame material schedule
    // (doc/draw_list_material_set_plan.md D6), for every registered root:
    // reconcile each set against the root's content library, apply the
    // forward set's enqueued object references, then update both sets. Runs
    // AFTER flush_draw_lists(), which is where draw-list records are written
    // and where draw-list object materials are referenced - so the copy this
    // writes covers every slot a record can name this frame.
    void update_material_sets                (erhe::graphics::Command_buffer& command_buffer);

    [[nodiscard]] auto get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&;

    // True when the Item_host is one of the registered scene roots. Parts
    // that cache scene-hosted items across frames (window targets, tool
    // state) use this to self-heal when the hosting scene is closed: the
    // cached shared_ptr keeps the item alive, so weak_ptr expiry can never
    // signal the close (see AGENTS.md "Scene-hosted references").
    [[nodiscard]] auto is_host_registered(const erhe::Item_host* item_host) -> bool;

    // Returns the sole registered scene root when exactly one is registered,
    // nullptr otherwise. Used as a fallback target for commands that look for
    // a scene before any viewport has been hovered or anything is selected.
    [[nodiscard]] auto get_single_scene_root() -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto scene_combo(const char* label, std::shared_ptr<Scene_root>& in_out_selected_entry, bool empty_option) const -> bool;

    void imgui();

private:
    void rebuild_display_color(erhe::scene::Mesh& mesh);

    App_context&                                m_context;
    ERHE_PROFILE_MUTEX(std::mutex,              m_mutex);
    std::vector<std::shared_ptr<Scene_root>>    m_scene_roots;
    // Scratch of rebuild_display_colors(); cleared after use, capacity kept.
    std::vector<std::shared_ptr<Scene_root>>          m_display_color_roots;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>   m_display_color_meshes;
};

}
