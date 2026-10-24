#pragma once

#include "erhe_renderer/enums.hpp"
#include "editor_item_bits.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_renderer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/render_bucket_snapshot.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_utility/debug_label.hpp"
#include "renderers/render_frame_results.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics { class Shader_stages; }

#include <functional>
#include <optional>
#include <string>

namespace erhe::graphics { class Base_render_pipeline; class Color_blend_state; }
namespace erhe::scene    { using Layer_id = uint64_t; }

struct Render_style_data;
struct Render_style_appearance;

namespace editor {

class App_context;
class Render_context;
class Scene_root;
class Scene_view;

// editor_aid passes draw editor guides rather than scene content; a render
// with Render_content::scene_only skips them (Composer::render). background
// passes (the sky) fill the pixels no scene surface covers; a render with
// Render_content::scene_surfaces skips them as well.
enum class Composition_pass_kind : unsigned int {
    scene_content = 0,
    editor_aid,
    background
};

class Composition_pass_data
{
public:
    bool                                                                   enabled{true}; // TODO consider using Item visibility flag
    // Tool / rendertarget overlay meshes must not be scaled by camera exposure
    // (issue #230). When set, Composition_pass::render forces exposure = 1.0
    // instead of camera->get_exposure(), so the gizmo handles and the hotbar
    // rendertarget mesh look identical regardless of the camera exposure.
    bool                                                                   ignore_exposure{false};
    // Overlay passes (tool / rendertarget) are rendered AFTER post-processing
    // when it is enabled (in a separate render pass that loads the content depth
    // attachment), so bloom / tonemap do not affect them. When post-processing
    // is disabled they render inline in the content pass. Composer::render uses
    // this to split the content phase from the overlay phase.
    bool                                                                   overlay{false};
    uint32_t                                                               content_wide_line_group{0};
    std::vector<erhe::scene::Layer_id>                                     mesh_layers{};
    std::size_t                                                            non_mesh_vertex_count{0};
    std::vector<erhe::graphics::Base_render_pipeline*>                     base_render_pipelines{};
    erhe::scene_renderer::Blending_mode_policy                             blending_mode_policy{erhe::scene_renderer::Blending_mode_policy::not_set};
    erhe::primitive::Primitive_mode                                        primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::Item_filter                                                      filter{};
    // When set, overrides the per-bucket color-blend state in Forward_renderer
    // (otherwise chosen by blend mode). Used by the selection stencil-mask pass
    // to write depth/stencil only (color_writes_disabled).
    const erhe::graphics::Color_blend_state*                               color_blend_override{nullptr};
    std::shared_ptr<Scene_root>                                            override_scene_root{};
    uint32_t                                                               shader_key_force_enable_mask{0};
    uint32_t                                                               shader_key_force_disable_mask{0};
    erhe::graphics::Shader_stages*                                         shader_stages{nullptr};
    // Force a SHADER_DEBUG variant for this pass's own meshes, instead of taking
    // the scene view's debug mode. The default path can only ever DROP the view's
    // mode per mesh (its filter is content-only), never turn one on - which is
    // what a pass needs when the variant IS the intended look rather than a debug
    // view. Used by the bone pass: Shader_debug::vdotn_tinted is exactly the
    // N.V shading, tinted by the bone's material, that solid bones want.
    // When set, shader_debug_override_filter selects which meshes it applies to.
    std::optional<erhe::scene_renderer::Shader_debug>                      shader_debug_override{};
    erhe::Item_filter                                                      shader_debug_override_filter{};
    // Grid settings (cell sizes, line widths, per-level colors, axis
    // label settings) forwarded to the camera UBO; read by grid.frag.
    erhe::scene_renderer::Grid_parameters                                  grid_parameters{};
    // The grid pass: with a viewport render state in the context
    // (Render_context::render_state), enabled and grid_parameters come from
    // the view's Grid_pass_state (Grid::render() records them per view and
    // frame) instead of the two fields above, which serve the views without
    // one (the headset).
    bool                                                                   grid_from_view{false};
    // Sky settings (horizon / zenith / ground colors, checker pattern)
    // forwarded to the camera UBO; read by sky.frag.
    erhe::scene_renderer::Sky_parameters                                   sky_parameters{};
    std::optional<erhe::scene_renderer::Primitive_interface_settings>      primitive_settings{};
    // The pass draws the selection outline: its primitive settings are the
    // time-animated make_selection_outline_settings(), taking precedence over
    // primitive_settings.
    bool                                                                   selection_outline_pulse{false};
    std::function<void()>                                                  begin{};
    std::function<void()>                                                  end{};
    std::function<const Render_style_data&(const Render_context& context)> get_render_style{};
    // Editor-global appearance (colors / widths / color sources) for this pass's
    // primitive_mode, selected to match get_render_style (Default vs Selection
    // appearance). Read via get_primitive_settings when primitive_settings is
    // not explicitly set. The matching per-view Render_style_data still gates
    // visibility through get_render_style / is_primitive_mode_enabled.
    std::function<const Render_style_appearance&(const Render_context& context)> get_appearance{};
    // Optional render-time activation predicate, evaluated against the current
    // viewport's Render_context (e.g. to gate on a render-style flag). The pass
    // is skipped when this is set and returns false. Distinct from
    // get_render_style / is_primitive_mode_enabled, which gate on the pass's own
    // primitive_mode.
    std::function<bool(const Render_context& context)>                     is_enabled{};
    Composition_pass_kind                                                  kind{Composition_pass_kind::scene_content};
};

// Why a pass did or did not draw on its last render() call. Recorded so the
// composition pass state can be inspected after the fact (Composition Passes
// window, MCP get_composition_passes) - a pass that returns early emits no
// debug marker at all, so a frame capture cannot distinguish "skipped" from
// "drew nothing", which makes a mis-gated pass look like missing geometry.
enum class Composition_pass_result : unsigned int {
    never_rendered = 0,      // render() has not been called yet
    disabled,                // data.enabled is false
    is_enabled_false,        // the is_enabled(context) predicate returned false
    no_scene_root,           // no scene root to render
    primitive_mode_disabled, // the render style has this primitive_mode off
    no_mesh_layers,          // none of data.mesh_layers resolved in the scene
    submitted,               // reached the draw submission (Forward_renderer::render)
    submitted_draw_lists     // reached the draw submission through the scene's Draw_list_scene
};

[[nodiscard]] auto c_str(Composition_pass_result result) -> const char*;

// The selection outline settings at the rendered frame's host time
// (Frame_packet::frame_host_system_time_ns; the render phase reads it as
// App_context::render_phase_host_system_time_ns): color, active item color
// and width pulse between the editor-global Selection_outline_style low /
// high values. A per-frame animation, computed where it is used.
[[nodiscard]] auto make_selection_outline_settings(const App_context& context, int64_t host_system_time_ns) -> erhe::scene_renderer::Primitive_interface_settings;

// The primitive settings a pass draws with: the selection outline pulse, the
// pass's explicit primitive_settings, or its editor-global appearance.
[[nodiscard]] auto get_pass_primitive_settings(const Composition_pass_data& data, const Render_context& context, int64_t host_system_time_ns) -> erhe::scene_renderer::Primitive_interface_settings;

// What Composition_pass::prepare() decided for one pass, one scene view and
// one frame: the gating outcome, the inputs of the path the pass takes and,
// for the bucket path, the prepared draws. Filled in the prepare phase (the
// last M2 step, App_rendering::prepare_composition) and read by
// Composition_pass::render() in the render phase, which then reads none of
// the pass's live gating state (is_enabled, render style, appearance) and no
// Mesh (doc/editor/editor.md "Frame phases and the render thread").
class Composition_pass_prepared
{
public:
    // never_rendered: not prepared (the composer filtered the pass out for
    // the view's Render_content). submitted: the pass draws, through the
    // path the fields below select; anything else is the early-out reason.
    Composition_pass_result                            result            {Composition_pass_result::never_rendered};
    std::size_t                                        mesh_count        {0};
    bool                                               use_wide_lines    {false};
    bool                                               use_draw_lists    {false};
    erhe::scene_renderer::Primitive_interface_settings primitive_settings{};
    // The fullscreen passes' camera UBO inputs: the grid's from the view's
    // render state (Grid::render(), per view), the sky's from the pass data
    // (App_rendering::update_sky_parameters), both on the main thread.
    erhe::scene_renderer::Grid_parameters              grid_parameters   {};
    erhe::scene_renderer::Sky_parameters               sky_parameters    {};
    // The bucket path's draws (Forward_renderer::prepare).
    erhe::scene_renderer::Render_bucket_snapshot       buckets;
    // The draw-list path's culling view, in the view set of the frame of
    // the pass's draw list scene: none from prepare(), set by the view's
    // rendergraph node's pre-execute hook (Composer::add_cull_views(),
    // doc/plans/culling.md phase 6a) before the frame's classification;
    // render_immediate() draws with Render_context::immediate_cull_view
    // (the headset's) or none (unculled).
    erhe::scene_renderer::Cull_view                    cull_view         {erhe::scene_renderer::Cull_view::none};
    // The pass's selection in the frame's GPU command generation of its
    // draw list scene (Draw_list_renderer::add_color_command_selection(),
    // set with cull_view by the same hook); none: CPU-written commands.
    erhe::scene_renderer::Command_selection_id         command_selection {erhe::scene_renderer::Command_selection_id::none};
};

// The draw-list path's blending selection of a pass's
// Blending_mode_policy (Composition_pass::prepare() routes only the
// policies it maps to the draw-list path).
[[nodiscard]] auto get_draw_blending_selection(const Composition_pass_data& data) -> erhe::scene_renderer::Draw_blending_selection;

class Composition_pass;
// The identity of a pass over one scene view in the GPU command generation
// (its retired counts are found by it).
[[nodiscard]] auto get_gpu_command_key(const Composition_pass& composition_pass, const Scene_view& scene_view) -> erhe::scene_renderer::Gpu_command_key;

// The view inputs a draw-list color pass of context culls against
// (Draw_list_renderer::add_color_cull_view(), Composer::add_cull_views())
// and rasterizes with (Composition_pass::render()): the context's views and
// its scene view's depth conventions. One derivation for both, so the
// culled frustum is the rasterized one.
[[nodiscard]] auto get_color_view_parameters(const Render_context& context) -> erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters;

// What the render calls of one frame recorded for one Composition_pass
// (Composition_pass::get_frame_stats()): the outcome of the frame's most
// recent render() call and the scene view it was for, and the CPU time of
// that call plus the running total and call count since construction up to
// and including it.
class Composition_pass_stats
{
public:
    Composition_pass_result result                 {Composition_pass_result::never_rendered};
    std::string             scene_view_name        {};
    std::size_t             mesh_count             {0};
    // Entries drawn by the draw-list path (0 when the pass went through
    // Forward_renderer::render()), and the entries that passed the filter
    // but were outside the view frustum. With GPU-generated commands
    // (Cull_classification::gpu) the counts are those of the newest retired
    // frame whose count readback named this pass and view
    // (Draw_list_scene::get_retired_gpu_color_counts(); frames in flight
    // late, 0 before the first), and draw_list_counts_frame_index names that
    // frame; 0 for counts of the frame itself (CPU-written commands).
    std::size_t             draw_list_entry_count  {0};
    std::size_t             draw_list_culled_count {0};
    uint64_t                draw_list_counts_frame_index{0};
    double                  cpu_time_us            {0.0};
    double                  total_cpu_time_us      {0.0};
    std::size_t             render_call_count      {0};
};

class Composition_pass : public erhe::Item<erhe::Item_base, erhe::Item_base, Composition_pass>
{
public:
    explicit Composition_pass(const Composition_pass&);
    Composition_pass& operator=(const Composition_pass&);
    ~Composition_pass() noexcept override;

    // A pass without frame statistics (the previews' own passes, rendered
    // on the main thread).
    explicit Composition_pass(std::string_view name);
    // A pass of the editor's composer: its render phase calls record frame
    // statistics, one Composition_pass_stats per frame in flight.
    Composition_pass(std::string_view name, std::size_t frames_in_flight);

    // Prepare phase (main thread): evaluates the pass's gating for the view
    // of context and, for the bucket path, buckets its meshes into
    // out.buckets. host_system_time_ns is the prepared frame's host time (the
    // selection outline pulse).
    void prepare(const Render_context& context, int64_t host_system_time_ns, Composition_pass_prepared& out) const;

    // Render phase: draws what prepare() decided for this view and frame.
    virtual void render(const Render_context& context, const Composition_pass_prepared& prepared);

    // prepare() and render() back to back on the calling thread, through
    // this pass's own scratch: for the callers that render where they
    // prepare - the previews (main thread, M2) and the headset (render
    // phase; doc/editor/render_thread.md section 2.2 lists its live reads).
    void render_immediate(const Render_context& context);
    void imgui();

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composition_pass"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return editor::Editor_item_types::composition_pass; }

    Composition_pass_data data;

    // Main thread: the statistics the render calls of frame_index recorded
    // (App_context::last_rendered_frame_index for the last rendered frame);
    // null when the pass was not rendered in that frame, has no frame
    // statistics, or nothing was rendered yet. Only render() calls in the
    // render phase record statistics (P4 measurement:
    // doc/erhe/draw_list_renderer.md).
    [[nodiscard]] auto get_frame_stats(std::optional<uint64_t> frame_index) const -> const Composition_pass_stats*;
    // Main thread: the CPU time totals of a frame's statistics since the last
    // reset_cpu_time_stats(), which takes the totals of the given frame (the
    // last rendered one) as the new baseline; the running totals stay the
    // render phase's.
    [[nodiscard]] auto get_total_cpu_time_us(const Composition_pass_stats& stats) const -> double;
    [[nodiscard]] auto get_render_call_count(const Composition_pass_stats& stats) const -> std::size_t;
    void reset_cpu_time_stats(std::optional<uint64_t> frame_index);

private:
    Composition_pass_prepared                                       m_immediate_prepared;
    // One per frame in flight, written by the render phase's render() calls.
    Render_frame_results<Composition_pass_stats>                    m_frame_stats;
    // Render phase owned: the running totals.
    double                                                          m_total_cpu_time_us{0.0};
    std::size_t                                                     m_render_call_count{0};
    // Main thread owned: the totals reset_cpu_time_stats() took.
    double                                                          m_total_cpu_time_baseline_us{0.0};
    std::size_t                                                     m_render_call_count_baseline{0};
};

}
