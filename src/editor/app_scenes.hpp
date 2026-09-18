#pragma once

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe {
    class Item_base;
    class Item_host;
}
namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Command_buffer;
}
namespace erhe::primitive {
    class Triangle_soup;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class App_context;
class Draw_mode;
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
    // Main thread, once per frame before flush_draw_lists(): kicks off the
    // rebuild of the primitives of every mesh whose Gprim.display_color
    // changed, so the one color of the surface is in the vertex data the
    // renderers read (Buffer_mesh::has_vertex_colors). Change-driven - a frame
    // in which nothing was written walks the registered roots and finds empty
    // queues. The builds themselves run on executor workers and swap in
    // through Scene_commit_queue (doc/frame-time-after-usd-import-plan.md R5).
    void rebuild_display_colors              ();
    // Main thread, once per frame beside rebuild_display_colors(): builds the
    // card proxy of every draw-mode attachment whose values, extent or
    // placement changed (doc/usd_compatibility.md, "Draw modes").
    // Change-driven for the same reason and in the same shape.
    void rebuild_draw_mode_proxies           ();
    // Step 2 of the per-frame material schedule
    // (doc/draw_list_material_set.md D6), for every registered root:
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
    // One mesh's slot in a display-color build: the primitive at this index of
    // this mesh's list is the one the built Primitive replaces.
    class Display_color_target
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        primitive_index{0};
    };

    // One display-color build of one flush. The key - the source geometry or
    // triangle soup, the color, the normal style and whether the vertex format
    // is the skinned one - decides the built bytes completely, so every target
    // of one record receives the same std::shared_ptr<Primitive>.
    class Display_color_build
    {
    public:
        const void*                                     source_key   {nullptr};
        glm::vec4                                       color        {0.0f, 0.0f, 0.0f, 1.0f};
        erhe::primitive::Normal_style                   normal_style {erhe::primitive::Normal_style::none};
        bool                                            skinned      {false};
        std::shared_ptr<erhe::geometry::Geometry>       geometry     {};
        std::shared_ptr<erhe::primitive::Triangle_soup> triangle_soup{};
        std::vector<Display_color_target>               targets      {};
    };

    // Whether a queued mesh can take the deferred path at all: it needs the
    // mesh sinks, an executor, the commit queue, and worker contexts for the
    // GPU buffer build. Without them the rebuild runs synchronously, which is
    // what every backend without worker contexts (GL, null window) does.
    [[nodiscard]] auto can_defer_display_color_rebuild() const -> bool;
    // Appends this mesh's slots to m_display_color_builds, sharing a record
    // with the slots already queued whose key matches. False when the mesh is
    // not one async_for_nodes_with_mesh dispatches (not a content node of this
    // root); the caller then rebuilds it synchronously.
    [[nodiscard]] auto queue_display_color_build(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& mesh) -> bool;
    // Dispatches one task per queued record and clears the scratch.
    void dispatch_display_color_builds(const std::shared_ptr<Scene_root>& scene_root);
    // Executor worker: builds the record's Primitive and enqueues the swap of
    // every target through App_context::scene_commit_queue.
    static void build_display_color_primitive(
        App_context&                       context,
        const std::shared_ptr<Scene_root>& scene_root,
        const Display_color_build&         build,
        const erhe::primitive::Build_info& build_info
    );
    // Main-thread rebuild of one mesh, used where the deferred path is not
    // available.
    void rebuild_display_color(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& mesh);

    App_context&                                m_context;
    ERHE_PROFILE_MUTEX(std::mutex,              m_mutex);
    std::vector<std::shared_ptr<Scene_root>>    m_scene_roots;
    // Scratch of rebuild_display_colors(); cleared after use, capacity kept.
    std::vector<std::shared_ptr<Scene_root>>          m_display_color_roots;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>   m_display_color_meshes;
    std::vector<Display_color_build>                  m_display_color_builds;
    std::vector<std::shared_ptr<erhe::Item_base>>     m_display_color_items;
    // Scratch of rebuild_draw_mode_proxies(); cleared after use, capacity kept.
    std::vector<std::shared_ptr<Scene_root>>          m_draw_mode_roots;
    std::vector<std::shared_ptr<Draw_mode>>           m_draw_mode_rebuilds;
};

}
