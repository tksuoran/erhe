#pragma once

#include "erhe_item/item.hpp"
#include "editor_item_bits.hpp"
#include "erhe_profile/profile.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/id_pass_prepared.hpp"
#include "erhe_scene_renderer/content_wide_line_feed.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace editor {

class Composition_pass_views;
class Render_context;
class Scene_view;
class Composition_pass;

// One scene view's prepared rendering for one frame: what
// Composition_pass::prepare() decided for every pass of the composer, indexed
// like Composer::composition_passes, the view's id pass
// (Id_renderer::prepare; prepared only when the view renders it) and the
// edge-line feed of the view's wide-line passes
// (Content_wide_line_renderer::prepare_mesh).
class Composer_prepared
{
public:
    uint64_t                               frame_index{0};
    bool                                   prepared   {false};
    // The render phase's pre-execute hook added the frame's culling views
    // into passes (Composer::add_cull_views()); Composer::render() verifies.
    bool                                   cull_views_added{false};
    // The view's PVS query box (doc/plans/pvs.md "Runtime consumption"),
    // computed and registered with the scene's draw lists in the prepare
    // phase (Composer::prepare(): the residency policy's change site,
    // "Residency and streaming"); empty (max < min) for a view without one.
    glm::vec3                              pvs_query_min{ 0.0f};
    glm::vec3                              pvs_query_max{-1.0f};
    std::vector<Composition_pass_prepared> passes;
    Id_pass_prepared                       id_pass;
    erhe::scene_renderer::Content_wide_line_feed wide_line_feed;
};

// A scene view's Composer_prepared, one per frame in flight: the prepare
// phase of frame F fills slot F % frames_in_flight (the last M2 step,
// App_rendering::prepare_composition), the render phase of frame F reads it
// (doc/editor/editor.md "Frame phases and the render thread"). The slots
// keep their capacity, so a steady-state prepare does not allocate.
class Composer_frame_slots
{
public:
    // Main thread, prepare phase.
    [[nodiscard]] auto begin_prepare(uint64_t frame_index, std::size_t frames_in_flight) -> Composer_prepared&;
    // Render phase: the slot prepared for frame_index; verifies it was.
    [[nodiscard]] auto get_prepared(uint64_t frame_index) const -> const Composer_prepared&;
    // Render phase, the view's pre-execute hook: the same slot, for
    // Composer::add_cull_views() to write the passes' culling views into.
    [[nodiscard]] auto get_render_prepared(uint64_t frame_index) -> Composer_prepared&;

private:
    std::vector<Composer_prepared> m_slots;
};

class Composer : public erhe::Item<erhe::Item_base, erhe::Item_base, Composer>
{
public:
    Composer(const Composer&);
    Composer& operator=(const Composer&);
    Composer(Composer&& old) noexcept;
    Composer& operator=(Composer&& old) noexcept;
    ~Composer() noexcept override;

    explicit Composer(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composer"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return editor::Editor_item_types::composer; }

    // Public API
    // Renders the composition passes selected by phase:
    //  - include_content: passes with data.overlay == false (scene content).
    //  - include_overlay: passes with data.overlay == true (tool gizmo /
    //    rendertarget meshes). When post-processing is enabled these are drawn
    //    in a separate pass after it; otherwise both phases run in one pass.
    // See issue #230.
    //
    // With Render_context::composer_prepared set, each pass draws what the
    // prepare phase decided for it (Composition_pass::render(context,
    // prepared)); without, each pass prepares and renders on the calling
    // thread (Composition_pass::render_immediate: the previews in M2, the
    // headset).
    void render(const Render_context& context, bool include_content, bool include_overlay);

    // Prepare phase (main thread): Composition_pass::prepare() of every pass
    // the view of context draws (the Render_content filter render() applies;
    // filtered passes stay never_rendered) into out.passes.
    void prepare(const Render_context& context, int64_t host_system_time_ns, Composer_prepared& out);

    // Render phase, from the scene view's rendergraph node's pre-execute
    // hook (before the frame's culling classification): the culling view of
    // every pass of prepared that draws through draw lists, one view per
    // draw list scene (the passes of one scene view share its camera),
    // added with Draw_list_renderer::add_color_cull_view() from
    // get_color_view_parameters(context) into the pass's
    // Composition_pass_prepared::cull_view. context is the view's render
    // context of the frame, the one its render calls draw with.
    void add_cull_views(const Render_context& context, Composer_prepared& prepared);

    // Render phase, from the headset's rendergraph node's pre-execute hook
    // (before the frame's culling classification), for a view that renders
    // its passes immediately (render_immediate()): per pass, from the
    // pass's gating for context (Composition_pass::prepare() gating_only,
    // the decisions render_immediate() then repeats), out[i] of a
    // draw-list pass of draw_list_scene gets cull_view and the pass's GPU
    // command selection (Draw_list_renderer::add_color_command_selection()),
    // the occlusion test pass gets cull_view made an occlusion view
    // (Draw_list_scene::add_occlusion_view() with occlusion_near_plane and
    // view_count, the views the pass rasterizes), the others none. out is
    // resized to the passes (capacity kept).
    void add_immediate_cull_views(
        const Render_context&                  context,
        erhe::scene_renderer::Draw_list_scene& draw_list_scene,
        erhe::scene_renderer::Cull_view        cull_view,
        const glm::vec4&                       occlusion_near_plane,
        std::size_t                            view_count,
        std::vector<Composition_pass_views>&   out
    );

    void imgui();

    // TODO Move to children
    ERHE_PROFILE_MUTEX(std::mutex,                 mutex);
    std::vector<std::shared_ptr<Composition_pass>> composition_passes;

private:
    // Prepare phase: the view's PVS query box into out, registered with the
    // scene's draw lists (Draw_list_scene::register_pvs_view()).
    void prepare_pvs_query(const Render_context& context, Composer_prepared& out);
};

// The key a scene view's PVS query box is registered under
// (Draw_list_scene::register_pvs_view(); get_pvs_status reports it): the
// view's address, stable for its lifetime.
[[nodiscard]] auto get_pvs_view_key(const Scene_view& scene_view) -> uint64_t;

}
