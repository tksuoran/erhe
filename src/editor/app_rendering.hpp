#pragma once

#include "renderers/composer.hpp"
#include "editor_item_bits.hpp"
#include "renderers/composition_pass.hpp" // TODO remove - for Fill_mode, Blend_mode, Selection_mode
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>

namespace erhe::commands       { class Commands; }
namespace erhe::graphics       { class Texture; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::scene          { class Xformable; using Node = Xformable; }
namespace erhe::scene_renderer { class Content_wide_line_renderer; class Mesh_memory; }
namespace erhe::window         { class Context_window; }

struct Graphics_preset_entry;

namespace editor {

class App_context;
class App_message_bus;
class App_rendering;
class App_settings;
class Grid_pass_state;
class Id_pass_prepared;
class Programs;
class Render_context;
class Renderable;
class Scene_image_view;
class Scene_view;
class Shadow_render_node;

class Capture_frame_command : public erhe::commands::Command
{
public:
    Capture_frame_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Pipeline_renderpasses
{
public:
    // reverse_depth is the static device value (Device::get_reverse_depth()),
    // queried once by the owner and passed here so the init-list can bake the
    // depth state. There is no runtime rebuild -- reverse-Z never changes while
    // the editor runs.
    Pipeline_renderpasses(
        erhe::graphics::Device&            graphics_device,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Programs&                          programs,
        bool                               reverse_depth
    );

    bool                                 m_y_flip;
    erhe::graphics::Vertex_input_state   m_empty_vertex_input;
    // Mirrored (negative determinant) meshes are handled per bucket by
    // Forward_renderer requesting the front-face-flipped pipeline variant
    // (Base_render_pipeline::get_pipeline_for front_face_flip), so a single
    // base pipeline per selection state suffices.
    erhe::graphics::Base_render_pipeline polygon_fill_standard;
    erhe::graphics::Base_render_pipeline polygon_fill_standard_selected;
    // Solid-wireframe overlay: triangle fill, depth less-or-equal, no depth
    // write, no stencil -- draws the expanded fill (with the wireframe variant)
    // at equal depth over the normal polygon fill.
    erhe::graphics::Base_render_pipeline solid_wireframe;
    erhe::graphics::Color_blend_state    line_hidden_blend_state;
    erhe::graphics::Base_render_pipeline line_hidden_blend;
    erhe::graphics::Base_render_pipeline brush_back;
    erhe::graphics::Base_render_pipeline brush_front;
    erhe::graphics::Base_render_pipeline edge_lines;
    erhe::graphics::Base_render_pipeline outline;
    erhe::graphics::Base_render_pipeline corner_points;
    erhe::graphics::Base_render_pipeline polygon_centroids;
    //erhe::graphics::Base_render_pipeline rendertarget_meshes;
    erhe::graphics::Base_render_pipeline sky;
    erhe::graphics::Base_render_pipeline grid;
    // Solid bones use a stencil-assisted multi-pass method: tag the hidden and
    // the visible parts in the stencil, clear the depth under the bones and lay
    // down their own depth, then draw the visible part solid and the hidden
    // part blended. Without it a bone inside a skinned mesh simply loses the
    // depth test and disappears the moment the mesh writes depth.
    // The bone pass runs in the content phase, so its stencil write / test
    // masks must leave the selection bit (7) alone.
    erhe::graphics::Base_render_pipeline bone1_hidden_stencil;
    erhe::graphics::Base_render_pipeline bone2_visible_stencil;
    erhe::graphics::Base_render_pipeline bone3_depth_clear;
    erhe::graphics::Base_render_pipeline bone4_depth;
    erhe::graphics::Base_render_pipeline bone5_visible_color;
    erhe::graphics::Color_blend_state    bone6_hidden_color_blend;
    erhe::graphics::Base_render_pipeline bone6_hidden_color;
};

class App_rendering
{
public:
    App_rendering(
        erhe::commands::Commands&          commands,
        erhe::graphics::Device&            graphics_device,
        App_context&                       context,
        App_message_bus&                   app_message_bus,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Programs&                          programs
    );

    [[nodiscard]] auto create_shadow_node_for_scene_view(
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_settings&                   app_settings,
        Scene_view&                     scene_view
    ) -> std::shared_ptr<Shadow_render_node>;

    // Main thread, M2 after the views prepared: every shadow node copies its
    // requested configuration, with the render settings copy's shadow
    // technique, for the render phase of this frame to apply
    // (Shadow_render_node::prepare).
    void prepare_shadow_render_nodes();

    // Main thread, the last M2 step (after update_material_sets() and the
    // lightmap prepare phase, which settle the forward material slots and
    // the lightmap uv transforms the records carry): every view that draws
    // the composer in the render phase - the viewports and the scene image
    // views - prepares its composition into its frame slot
    // (Composer_frame_slots; doc/editor/editor.md "Frame phases and the
    // render thread").
    // Then every shadow node prepares its bucket path casters
    // (Shadow_render_node::prepare_casters).
    void prepare_composition(uint64_t frame_index, std::size_t frames_in_flight, int64_t host_system_time_ns);
    // One view's composition (called by the views' prepare_composition()):
    // the sky pass parameters for the view's scene, then Composer::prepare.
    void prepare_composer(const Render_context& context, int64_t host_system_time_ns, Composer_prepared& out);
    // Render phase, from a view's rendergraph node's pre-execute hook: the
    // view's color culling views of the frame (Composer::add_cull_views()).
    void add_composer_cull_views(const Render_context& context, Composer_prepared& prepared);
    // Render phase, from the headset's rendergraph node's pre-execute hook:
    // the per-pass views of a view that renders its passes immediately
    // (Composer::add_immediate_cull_views()).
    void add_immediate_composer_cull_views(
        const Render_context&                  context,
        erhe::scene_renderer::Draw_list_scene& draw_list_scene,
        erhe::scene_renderer::Cull_view        cull_view,
        const glm::vec4&                       occlusion_near_plane,
        std::size_t                            view_count,
        std::vector<Composition_pass_views>&   out
    );
    // Main thread: Scene_image_view lists itself for prepare_composition()
    // for its lifetime.
    void register_scene_image_view  (Scene_image_view* scene_image_view);
    void unregister_scene_image_view(Scene_image_view* scene_image_view);

    [[nodiscard]] auto get_shadow_node_for_view(const Scene_view& scene_view) -> std::shared_ptr<Shadow_render_node>;
    [[nodiscard]] auto get_all_shadow_nodes    () -> const std::vector<std::shared_ptr<Shadow_render_node>>&;
    [[nodiscard]] auto is_capturing            () const -> bool;

    // Drop the Shadow_render_node from m_all_shadow_render_nodes. Caller
    // is expected to be Scene_builder_viewport_resources_operation's undo
    // path tearing down a Viewport_scene_view: the only other shared_ptr
    // owner is that operation, so dropping ours lets the node's destructor
    // unregister it from the Rendergraph. Returns true if a matching entry
    // was found and erased.
    auto destroy_shadow_node(const std::shared_ptr<Shadow_render_node>& shadow_render_node) -> bool;

    void trigger_capture            ();
    // include_overlay=false renders only content passes (the overlay -- tool /
    // rendertarget meshes -- is then drawn after post-processing by
    // render_overlay). When post-processing is disabled, pass true to render
    // content + overlay in the same pass. See issue #230.
    void render_viewport_main       (const Render_context& context, bool include_overlay);
    void render_overlay             (const Render_context& context);
    void render_viewport_renderables(const Render_context& context);
    // Renders the selected composition-pass phases (content and/or overlay).
    // The VR path renders both in one pass (no post-processing). Draws the
    // composition prepared in context.composer_prepared; without one (the
    // headset) the sky parameters are refreshed and each pass prepares
    // where it renders.
    void render_composer            (const Render_context& context, bool include_content, bool include_overlay);
    // M2, from Viewport_scene_view::prepare_composition (the last M2 step,
    // after Id_renderer::begin_frame handed the frame its region scan): the
    // view's id pass into its frame slot, under the gates render_id()
    // renders under.
    void prepare_id                 (const Render_context& context, Id_pass_prepared& out);
    // Render phase: draws the id pass prepare_id() prepared for the view
    // (Render_context::composer_prepared).
    void render_id                  (const Render_context& context);
    void process_start_capture      ();
    void process_end_capture        ();
    // The grid composition pass inputs of a view without a viewport render
    // state (the headset, which prepares its composition where it renders
    // it). A viewport records its grid into its render state instead
    // (Viewport_scene_view::record_grid()).
    void set_grid                   (const Grid_pass_state& grid);
    // Refreshes the sky composition pass parameters from the sky config effective
    // for the render context's scene (per-scene override or editor-global default).
    void update_sky_parameters      (const Render_context& context);

    // The lightmap atlas the forward passes sample, chosen by the main
    // thread: the editor's lightmap prepare phase (baker or streamer) and
    // the MCP direct bake. M2 copies it into the Frame_packet and the render
    // phase hands the packet's texture to the forward renderer, so R binds
    // the atlas of the frame it renders while M2 of a later frame chooses
    // the next one. Main thread only.
    void set_lightmap_texture(const std::shared_ptr<erhe::graphics::Texture>& texture);
    [[nodiscard]] auto get_lightmap_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;

    void add   (Renderable* renderable);
    void remove(Renderable* renderable);

    auto make_composition_pass(std::string_view name) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view        name,
        Composition_pass_data&& data,
        bool                    selected
    ) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view                                             name,
        Composition_pass_data&&                                      data,
        std::initializer_list<erhe::graphics::Base_render_pipeline*> pipelines
    ) -> std::shared_ptr<Composition_pass>;
    auto make_composition_pass(
        std::string_view                           name,
        const std::shared_ptr<Composition_pass>&   base_pass,
        erhe::scene_renderer::Blending_mode_policy blending_mode_policy
    ) -> std::shared_ptr<Composition_pass>;

    // Read-only access to the Composer's pass list for callers that need
    // to walk the variant-aware Base_render_pipeline pointers. Returns
    // the underlying vector by const reference WITHOUT taking the
    // Composer's mutex, so callers must guarantee single-threaded access
    // at the point of call. Intended exclusively for init-time use (the
    // shader-variant prewarm in renderers/prewarm.cpp); do not call from
    // the render thread.
    [[nodiscard]] auto composition_passes() const -> const std::vector<std::shared_ptr<Composition_pass>>&;

    // M2 (the viewports' prepare_composition, the headset's
    // prepare_wide_line_feed): the edge-line feed of the wide-line passes for
    // one view, gated as the passes draw (enabled, the view's render style),
    // with each pass's primitive settings at the prepared frame's host time
    // and each mesh fed at its forced LOD level, read from the scene's
    // Draw_list_scene (doc/plans/selection_outline_lod.md); edge lines are
    // not content, so the view's shader debug mode and fill path do not
    // change them. The caller holds the scene root's item_host_mutex (the
    // feed reads the meshes' Buffer_mesh variants, which a worker may be
    // replacing).
    void prepare_wide_line_feed(
        const Render_context&                         context,
        int64_t                                       host_system_time_ns,
        erhe::scene_renderer::Content_wide_line_feed& out
    ) const;

    void imgui();
    void request_renderdoc_capture();

    // Joint_weight_ramp debug mode state, owned by Weight_display:
    //   debug_joint_indices.x = 0xffffffffu when no joint is active (the
    //     shader's "missing data" magenta), 0 when one is;
    //   debug_joint_indices.y = 1 to show zero-weight vertices as black;
    //   debug_target_joint    = the active joint itself. Matched per joint
    //     slot in the joint buffer, so it lights up every skin that uses the
    //     joint - a character rig's skins all share the rig's joint nodes.
    glm::uvec4                        debug_joint_indices{0xffffffffu, 0, 0, 0};
    std::weak_ptr<erhe::scene::Node>  debug_target_joint;
    std::vector<glm::vec4>            debug_joint_colors;
    std::shared_ptr<Composition_pass> selection_outline;
    std::shared_ptr<Composition_pass> hover_outline;
    std::shared_ptr<Composition_pass> edge_lines_not_selected;
    std::shared_ptr<Composition_pass> edge_lines_selected;
    // Edge-lines-only pass for geometry graph ghost meshes (Houdini
    // template flag): meshes flagged visible | render_wireframe (and
    // deliberately not `content`, so no other pass touches them). Not
    // gated by the per-viewport edge-lines render style; fixed dim
    // appearance via primitive_settings. Fed by Viewport_scene_view /
    // Headset_view in color mode (invisible in id-buffer edge mode).
    std::shared_ptr<Composition_pass> ghost_edge_lines;
    std::shared_ptr<Composition_pass> translucent_outline;
    // Overlay pass that draws rendertarget meshes (e.g. the hotbar), ignoring
    // camera exposure and rendered after post-processing when enabled (#230).
    std::shared_ptr<Composition_pass> rendertarget;
    // Same, for the meshes flagged editor::Editor_item_flags::view_anchored: runs only in the
    // view the hotbar is anchored to.
    std::shared_ptr<Composition_pass> rendertarget_view_anchored;

private:
    void handle_graphics_settings_changed(Graphics_preset_entry* graphics_preset);

    [[nodiscard]] auto get_render_pipeline_state(
        const Composition_pass& renderpass,
        bool                    selected
    ) -> erhe::graphics::Base_render_pipeline*;

    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    erhe::message_bus::Subscription<Graphics_settings_message> m_graphics_settings_subscription;
    App_context&              m_context;
    // Device frames in flight: the composer passes' frame statistics slots.
    std::size_t               m_frames_in_flight{0};

    // Commands
    Capture_frame_command     m_capture_frame_command;

    Pipeline_renderpasses             m_pipeline_passes;
    Composer                          m_composer;
    std::shared_ptr<Composition_pass> m_grid_composition_pass;
    std::shared_ptr<Composition_pass> m_sky_composition_pass;

    // TODO Re-add per-render-pass GPU timers when the composer's pipeline
    // passes own their own Render_pass objects.

    bool                      m_trigger_capture{false};
    std::vector<std::shared_ptr<Shadow_render_node>> m_all_shadow_render_nodes;

    ERHE_PROFILE_MUTEX(std::mutex, m_renderables_mutex);
    std::vector<Renderable*>       m_renderables;

    // Main thread only (register / unregister / prepare_composition).
    std::vector<Scene_image_view*> m_scene_image_views;

    // Main thread only (set_lightmap_texture).
    std::shared_ptr<erhe::graphics::Texture> m_lightmap_texture;
};

static constexpr unsigned int s_stencil_edge_lines               =  1u; // 0 inc
// 2 and 3 were the tool-mesh hidden / visible tags of the mesh-rendered
// transform gizmo; left unused so the values below keep their meaning.
// Hidden / visible tags of the solid bone pass.
static constexpr unsigned int s_stencil_bone_mesh_hidden         =  4u;
static constexpr unsigned int s_stencil_bone_mesh_visible        =  5u;

static constexpr unsigned int s_stencil_line_renderer_grid_minor =  8u;
static constexpr unsigned int s_stencil_line_renderer_grid_major =  9u;
static constexpr unsigned int s_stencil_line_renderer_selection  = 10u;
static constexpr unsigned int s_stencil_line_renderer_tools      = 11u;

}
