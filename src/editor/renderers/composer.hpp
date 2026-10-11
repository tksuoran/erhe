#pragma once

#include "erhe_item/item.hpp"
#include "editor_item_bits.hpp"
#include "erhe_profile/profile.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/id_pass_prepared.hpp"
#include "erhe_scene_renderer/content_wide_line_feed.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace editor {

class Render_context;
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

    void imgui();

    // TODO Move to children
    ERHE_PROFILE_MUTEX(std::mutex,                 mutex);
    std::vector<std::shared_ptr<Composition_pass>> composition_passes;
};

}
