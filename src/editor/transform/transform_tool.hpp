#pragma once

#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
#include "transform/transform_tool_settings.hpp"
#include "transform/ik_drag.hpp"
#include "transform/point_set_transform.hpp"
#include "transform/mesh_component_transform.hpp"
#include "transform/physics_driven_drag.hpp"
#include "transform/rotation_inspector.hpp"
#include "level/kit_connector_snap.hpp"
#include "level/kit_piece.hpp"
#include "config/generated/connector_snap_axis_mode.hpp"
#include "config/generated/connector_snap_orientation.hpp"
#include "config/generated/surface_drag_direction.hpp"
#include "level/level_queries.hpp"
#include "level/surface_base_frame.hpp"
#include "scene/analytic_hover_provider.hpp"
#include "tools/tool.hpp"

#include "windows/property_editor.hpp"

#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "tools/tool_window.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/node.hpp"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::imgui {
    class Value_edit_state;
}
namespace erhe::physics {
    enum class Motion_mode : unsigned int;
}
namespace erhe::scene {
    class Mesh;
    class Xformable; using Node = Xformable;
    class Trs_transform;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

struct Transform_tool_config;

namespace tf {
    class Executor;
}

namespace editor {

class Compound_operation;
class Ray_hit_style;
class App_message_bus;
struct Active_scene_changed_message;
class  Active_item_changed_message;
struct Hover_scene_view_message;
struct Selection_message;
struct Animation_update_message;
struct Node_touched_message;
struct Render_scene_view_message;
class Headset_view;
class Node_physics;
class Scene_root;
class Move_tool;
class Rotate_tool;
class Viewport_scene_view;

// The modal toggles of a pointer scalar drag (doc/editor/transform.md
// "Scalar edits"): E even, F flipped, C clamp.
enum class Scalar_drag_option : unsigned int {
    even    = 0,
    flipped = 1,
    clamp   = 2
};

// What a numeric scalar edit (Transform_tool::run_scalar_edit) did.
class Scalar_edit_result
{
public:
    std::size_t slide_vertices{0};
    std::size_t loops         {0}; // edge slide
    std::size_t moved_vertices{0};
};
class Scale_tool;
class Subtool;
class Tools;
class Transform_tool;
class Viewport_scene_view;

class Transform_tool_drag_command : public erhe::commands::Command
{
public:
    Transform_tool_drag_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    App_context& m_context;
};

// Escape during a gizmo drag of nodes: Transform_tool::cancel_node_drag().
class Transform_tool_cancel_drag_command : public erhe::commands::Command
{
public:
    Transform_tool_cancel_drag_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// The keys whose held state suspends the connector snap of a pointer node drag (U27). The held state is read
// from Input_state; the key events this command sees only re-run a drag's last step. Never consumes the key.
class Transform_tool_suspend_connector_snap_command : public erhe::commands::Command
{
public:
    Transform_tool_suspend_connector_snap_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    // Keys only: a key has a held state in Input_state.
    auto accepts_binding(const erhe::commands::Binding_desc& binding, std::string& out_reason) const -> bool override;

private:
    App_context& m_context;
};

// Bakes the current gizmo anchor ("temp node") frame into a real, undoable scene Node.
class Create_frame_node_command : public erhe::commands::Command
{
public:
    Create_frame_node_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Transform_entry
{
public:
    std::shared_ptr<erhe::scene::Node>        node;
    erhe::scene::Trs_transform                parent_from_node_before;
    // The node's authored xformOp stack when the drag started (M8).
    std::optional<erhe::scene::Xform_op_stack> xform_op_stack_before;
    erhe::scene::Trs_transform                world_from_node_before;
    std::optional<erhe::physics::Motion_mode> original_motion_mode;
    erhe::physics::Motion_mode                motion_mode{erhe::physics::Motion_mode::e_none};
    // Whether the last surface step snapped this node to a surface (D4.6).
    bool                                      surface_found{false};
};

class Transform_tool_shared
{
public:
    auto get_visualizations() const -> Handle_visualizations*
    {
        if (!visualizations_ready.load()) {
            return nullptr;
        }
        return visualizations.get();
    }

    [[nodiscard]] auto get_initial_drag_position_in_world() const -> glm::vec3
    {
        return world_from_anchor_initial_state.get_translation() + m_initial_drag_position_from_anchor_initial_state;
    }
    void set_initial_drag_position_in_world(glm::vec3 in_initial_drag_position_in_world)
    {
        m_initial_drag_position_from_anchor_initial_state = in_initial_drag_position_in_world - world_from_anchor_initial_state.get_translation();
    }

    // Finalize the reference frame after a producer (node selection or mesh
    // component selection) has set world_from_anchor_initial_state. In Reference
    // mode the explicit reference node replaces the frame entirely; otherwise the
    // producer-supplied frame stands. Always leaves a valid frame and mirrors it
    // into world_from_anchor. Consumers read world_from_anchor without caring
    // which origin produced it; Global (world axes) is applied at the gizmo/basis
    // via Transform_tool_settings::use_anchor_orientation().
    void apply_reference_frame();

    // The node-origin frame (doc/editor/transform.md "Gizmo position"): the
    // anchor is the gizmo's place; the origin frame is what the Transform
    // window shows and numeric edits change.
    [[nodiscard]] auto get_world_from_origin              () const -> erhe::scene::Trs_transform;
    [[nodiscard]] auto get_world_from_origin_initial_state() const -> erhe::scene::Trs_transform;
    [[nodiscard]] auto has_origin_offset                  () const -> bool;
    // Places the anchor at its fixed offset from an updated origin frame.
    void set_anchor_from_origin(const erhe::scene::Trs_transform& world_from_origin);

    Transform_tool_settings                settings;
    std::vector<Transform_entry>           entries;
    // Arbitrary node whose orientation drives the gizmo in Reference mode.
    std::weak_ptr<erhe::scene::Node>       reference_node;
    glm::vec3                              m_initial_drag_position_from_anchor_initial_state{0.0f};
    float                                  initial_drag_position_distance_to_camera{0.0f};
    erhe::scene::Trs_transform             world_from_anchor_initial_state;
    erhe::scene::Trs_transform             world_from_anchor;
    // Translation-only, in anchor space; identity unless the gizmo sits at a bounding box center.
    erhe::scene::Trs_transform             anchor_from_origin;
    // Refreshed by Edit_state each draw; the Transform window's world-mode values point here.
    erhe::scene::Trs_transform             world_from_origin;
    // True when a mesh component selection (vertex/edge/face) is driving the gizmo
    // instead of node selection. When set, shared.entries is empty and the gizmo
    // edits geometry vertices via Mesh_component_transform.
    bool                                   component_mode      {false};
    std::atomic<bool>                      visualizations_ready{false};
    std::unique_ptr<Handle_visualizations> visualizations      {};
};

class Edit_state
{
public:
    Edit_state();
    Edit_state(
        Transform_tool_shared& shared,
        Transform_tool&        transform_tool,
        Rotation_inspector&    rotation_inspector,
        Property_editor&       property_editor
    );

    bool                               m_multiselect       {false};
    std::shared_ptr<erhe::scene::Node> m_first_node        {};
    glm::mat4                          m_world_from_parent {1.0f};
    bool                               m_use_world_mode    {false};
    erhe::scene::Trs_transform*        m_transform         {nullptr};
    erhe::scene::Trs_transform*        m_rotation_transform{nullptr};

    glm::vec3                          m_scale      {1.0f};
    glm::quat                          m_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                          m_translation{0.0f};
    glm::vec3                          m_skew       {0.0f};

    // Per-component channel locks of the (single, local-mode) edited node
    // (doc/plans/rigging/ik_settings.md section 2): grey out locked widgets. The
    // commit paths (apply_*_edit) mask locked components regardless, which
    // also covers MCP callers.
    std::array<bool, 3>                m_lock_translation{false, false, false};
    std::array<bool, 3>                m_lock_rotation   {false, false, false};
    std::array<bool, 3>                m_lock_scale      {false, false, false};

    erhe::imgui::Value_edit_state      m_translate_state{};
    erhe::imgui::Value_edit_state      m_rotate_quaternion_state;
    erhe::imgui::Value_edit_state      m_rotate_euler_state;
    erhe::imgui::Value_edit_state      m_rotate_axis_angle_state;
    erhe::imgui::Value_edit_state      m_scale_state;
    erhe::imgui::Value_edit_state      m_skew_state;
};

// Per-component transform channel locks (doc/plans/rigging/ik_settings.md
// section 2): masks locked LOCAL components of the node's parent-from-node
// transform back to their reference (pre-edit) values. Call after applying
// any transform edit to a node; every Transform tool delta path, the
// numeric-edit commit paths, and the MCP direct set-transform path do.
void enforce_channel_locks(erhe::scene::Node& node, const erhe::scene::Trs_transform& parent_from_node_before);

class Transform_tool
    : public Tool
    , public Analytic_hover_provider
{
public:
    static constexpr int c_priority{1};

    Transform_tool(
        const Transform_tool_config&       transform_tool_config,
        tf::Executor&                      executor,
        erhe::commands::Commands&          commands,
        erhe::imgui::Imgui_renderer&       imgui_renderer,
        erhe::imgui::Imgui_windows&        imgui_windows,
        App_context&                       app_context,
        App_message_bus&                   app_message_bus,
        Headset_view&                      headset_view,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Tools&                             tools,
        Move_tool&                         move_tool,
        Rotate_tool&                       rotate_tool,
        Scale_tool&                        scale_tool
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Analytic_hover_provider. The pick writes the gizmo hover
    // state (hovered handle, grab point, box face, rotation sphere crossings)
    // for the view it picks; on_drag_ready() starts a drag only from the state
    // of the view the press happened in.
    [[nodiscard]] auto get_analytic_hover_name() const -> const std::string& override;
    [[nodiscard]] auto pick_analytic_hover(
        Scene_view& scene_view,
        glm::vec3   ray_origin,
        glm::vec3   ray_direction
    ) -> std::optional<Hover_entry> override;
    void clear_analytic_hover(Scene_view& scene_view) override;

    // Public API
    void viewport_toolbar();

    [[nodiscard]] auto is_transform_tool_active() const -> bool;

    // Commands
    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;
    void end_drag     ();

    // The Transform window's Rotation group (MCP get_transform_rotation).
    [[nodiscard]] auto get_rotation_inspector() const -> const Rotation_inspector&;

    // The IK drag state; the IK limit visualization reads the running drag's
    // resolved constraints from it.
    [[nodiscard]] auto get_ik_drag() const -> const Ik_drag& { return m_ik_drag; }

    // Rotate ring k (handle e_handle_rotate_x/y/z for k = 0/1/2) rotates
    // about frames[k][0] and lies in the plane of frames[k][1], frames[k][2]
    // (world space, orthonormal). Normally ring k is gizmo basis axis k. With
    // the Rotation inspector showing Euler angles of a single node and its
    // Orthogonal Gizmo unchecked, the rings form the Euler gimbal instead:
    // ring k rotates about the k-th axis of
    // the Euler order in the frame left by the rotations before it, so
    // dragging ring k changes exactly Euler angle k.
    [[nodiscard]] auto get_rotate_ring_frames(const glm::mat3& basis) const -> Rotate_ring_frames;

    // For Handle_visualizations
    [[nodiscard]] auto get_active_handle  () const -> Handle;
    [[nodiscard]] auto get_hover_handle   () const -> Handle;
    // Verification hook (MCP debug_set_transform_hover): handle hover comes
    // from the pointer, which a headless run does not have.
    void debug_set_hover_handle(Handle handle);
    // World-space point on the hovered gizmo handle (analytic pick or
    // box-face hit), or nullopt when no handle is hovered. Richer than the
    // gizmo's tool_slot entry: the XR controller ray stops here, which
    // excludes the arcball region and box faces and falls back to the
    // rotation sphere exit.
    [[nodiscard]] auto get_hover_handle_position_in_world() const -> std::optional<glm::vec3>;
    // Where the control ray enters the rotation sphere this frame (clamped
    // to the ray origin when starting inside), or nullopt when it misses.
    // The XR controller ray recolors from this point onward.
    [[nodiscard]] auto get_ray_sphere_entry_position_in_world() const -> std::optional<glm::vec3>;
    // First crossing of any gizmo axis plane inside the rotation sphere,
    // or nullopt. The XR controller ray darkens from this point onward.
    [[nodiscard]] auto get_ray_sphere_plane_crossing_position_in_world() const -> std::optional<glm::vec3>;

    // Records one undo entry when at least one entry's local transform differs from its drag-start value.
    void record_transform_operation();
    // An identity delta puts every entry back at its drag-start pose exactly (no round-off from recomposing); a drag's
    // connector snap then snaps it as any step.
    void restore_start_pose();
    // restore_start_pose() without the connector snap, the layout preview step and the gizmo refresh.
    void restore_start_entries();
    // After a subtool wrote a step's node transforms itself (box scale per node): the connector snap, the live layout
    // preview and the gizmo refresh.
    void finish_drag_step();

    // A drag driven by code instead of the pointer (the MCP drag_selection
    // tool): begin_scripted_drag() starts a node drag of the selection the
    // way a gizmo press does - including the physics pull of jointed dynamic
    // bodies - the caller then feeds adjust_translation() / adjust() /
    // adjust_scale() once per frame, and end_scripted_drag() records the
    // undoable operation and releases the bodies like a gizmo release.
    // Refused while a pointer drag, a scripted drag or a component selection
    // is active, or when nothing is selected.
    // A translate drag snaps the transform anchor to the surface with `surface` (D4.6, U51), along surface.direction or, with
    // camera_ray, the last viewport camera's ray through the anchor; a drag of any kind snaps to connectors within
    // connector_snap_distance meters (D3.13, 0 = off), turning by connector_snap_orientation, rotate with surface.orient (U50).
    auto begin_scripted_drag(
        Transform_drag_kind        kind,
        const Surface_options&     surface,
        Surface_drag_direction     surface_direction_mode,
        float                      connector_snap_distance,
        Connector_snap_orientation connector_snap_orientation
    ) -> bool;
    void end_scripted_drag  ();
    // Cancels the gizmo drag of nodes (pointer or scripted): the entries, their bodies and the moved layouts'
    // children return to the drag start and nothing is recorded. False when no node drag is active.
    auto cancel_node_drag   () -> bool;
    [[nodiscard]] auto is_node_pointer_drag_active() const -> bool;
    [[nodiscard]] auto is_scripted_drag_active() const -> bool;
    // True while the current drag pulls the node's jointed dynamic body
    // through physics instead of writing the node transform.
    [[nodiscard]] auto is_node_pulled_through_physics(const erhe::scene::Node* node) const -> bool;

    // D4.6 (doc/editor/level_tools.md "Transform tool"): snaps each moved mesh of the selection to
    // the surface under it, all moved meshes excluded from the rays; returns how many found one.
    auto snap_to_surface(const Surface_options& options) -> std::size_t;
    // U51 (translate drag steps): snaps the transform anchor along the options' unit direction and moves the moved set by
    // its displacement, turned about it with orient; returns how many entries moved with it.
    auto snap_anchor_to_surface(const Surface_options& options, Orient_half_turn half_turn) -> std::size_t;

    // D3.13 once, outside a drag (MCP transform_selection): moves the selection so its connector nearest another
    // kit piece's connector within distance meets it, turned by the orientation (U50, rotate with orient); true when it snapped.
    auto snap_to_connectors(float distance, Connector_snap_orientation orientation, Surface_orient orient) -> bool;
    // The pair of the last connector snap step (kept after the drag ends) and the counts gathered at its begin.
    [[nodiscard]] auto get_connector_snap() const -> const Kit_connector_drag_snap&;
    // U27: a suspend key event; during a pointer node drag whose held state changed it re-runs the last step.
    void on_connector_snap_suspend_key_edge();
    // U27: a key of the suspend command is down now (Input_state), with its binding's modifiers.
    [[nodiscard]] auto is_connector_snap_suspend_held() const -> bool;
    // U27: the step's pointer motion from the press (before the grid snap); the connector snap starts at the first nonzero one.
    void note_translate_drag_motion(const glm::vec3& motion);
    // U27: a rotate or scale step's pointer motion from the press is nonzero (the angle or factor before the angle snap).
    void note_node_drag_motion();

    // Create a real, undoable scene Node at the current gizmo anchor frame.
    void create_node_from_anchor();

    // Exposed for Node_transform_operation
    void update_target_nodes(erhe::scene::Node* node_filter);

    // The node the gizmo drives for a given selected node. Identity except for
    // a skinned mesh whose skin is posed by joints outside its own subtree:
    // skinning ignores the mesh node's transform, so the skin's transform root
    // is driven instead. Public so the Transform window can report when the
    // gizmo is not acting on the node the user selected.
    [[nodiscard]] static auto resolve_transform_target(
        const std::shared_ptr<erhe::scene::Node>& node
    ) -> std::shared_ptr<erhe::scene::Node>;

    // Re-derive the gizmo anchor after a reference-frame setting (mode or
    // reference node) is changed from the UI. The mesh-component path already
    // re-derives the anchor every idle frame, so only the node-selection path
    // needs this explicit refresh.
    void on_reference_settings_changed();

    // Interface for Properties window
    void transform_properties();

    // Numeric edit application, shared by the Transform window (Edit_state)
    // and the MCP server. When local is true and exactly one node is selected,
    // the value is applied in parent space; otherwise it is applied in world
    // (anchor) space. No-op when the selection contains no nodes.
    void apply_translation_edit(glm::vec3 translation, bool local);
    void apply_rotation_edit   (glm::quat rotation,    bool local);
    void apply_scale_edit      (glm::vec3 scale,       bool local);
    void apply_skew_edit       (glm::vec3 skew,        bool local);

    // Interface for Subtool usage
    void adjust            (const glm::mat4& updated_world_from_anchor);
    void adjust_from_origin(const erhe::scene::Trs_transform& updated_world_from_origin);
    void adjust_translation(glm::vec3 translation);
    void adjust_rotation   (glm::vec3 center_of_rotation, glm::quat rotation);
    void adjust_scale      (glm::vec3 center_of_scale,    glm::vec3 scale);
    void update_visibility ();
    void update_transforms ();

    // Mesh component editing (vertex/edge/face). Begins a component edit gesture (drag
    // or numeric edit), commits the queued operation, and reports whether a gesture is
    // in progress. Exposed for Edit_state (numeric fields).
    void begin_component_edit  (Component_edit_origin origin);
    void commit_component_edit ();
    [[nodiscard]] auto is_component_mode      () const -> bool { return shared.component_mode; }
    [[nodiscard]] auto is_component_edit_active() const -> bool;

    // Pointer-driven scalar edit of the mesh component selection
    // (doc/editor/transform.md "Scalar edits"), started from the component
    // tool (G) at the pointer position in `view`, without a gizmo handle.
    // update_scalar_drag() runs once per frame and maps the pointer motion
    // since the start to the factor (only while the drag is active, and only
    // when the pointer or an option changed); confirm commits, and
    // cancel_component_edit() restores the start state. Refused while a
    // gizmo drag, a scripted drag or another component edit is active.
    // With a topology_step (loop cut) the edit covers the step's mesh
    // primitive only and its confirm / cancel commit / cancel the step too
    // (Mesh_component_transform::begin_scalar()).
    auto begin_scalar_drag        (Scalar_edit_kind kind, Viewport_scene_view& view, const Scalar_topology_step* topology_step = nullptr) -> bool;
    void update_scalar_drag       ();
    void confirm_scalar_drag      ();
    auto toggle_scalar_drag_option(Scalar_drag_option option) -> bool;
    [[nodiscard]] auto is_scalar_drag_active() const -> bool { return m_scalar_drag.active; }
    // Cancels the active mesh component or point set edit - the pointer
    // scalar drag, or the edit of a gizmo drag - restoring its start state
    // and queueing no operation. False when no such edit is active.
    auto cancel_component_edit    () -> bool;
    // The gizmo is anchored on a hovered or selected point of a set drawn as gizmos (doc/editor/point_set_editing.md "Hover gizmo").
    [[nodiscard]] auto is_point_gizmo        () const -> bool;
    // The view's control ray picks a gizmo handle now (not the hover state of the last hover update).
    [[nodiscard]] auto is_pointer_on_handle  (Scene_view& scene_view) -> bool;
    // The point set whose selected or hovered point anchors the gizmo; null when another producer does.
    [[nodiscard]] auto get_gizmo_point_set   () const -> const IPoint_set*;
    // The tool toggles; on a point gizmo the translate handles alone, and only while a point is hovered.
    [[nodiscard]] auto get_shown_handle_tools() const -> Shown_handle_tools;
    // The point set whose gesture is in progress; null outside one.
    [[nodiscard]] auto get_point_set_edit() const -> const IPoint_set*;
    // A scalar edit the caller drives itself (the inset mode of
    // Mesh_component_selection_tool, doc/editor/mesh_modeling.md): begin with
    // a topology step, apply steps, commit; cancel_component_edit() cancels.
    // begin_scalar_edit() is refused (false) while a pointer scalar drag, a
    // gizmo drag, a scripted drag or another component edit is active.
    auto begin_scalar_edit (Scalar_edit_kind kind, const Scalar_topology_step& topology_step) -> bool;
    void apply_scalar_edit (const Scalar_input& input);
    void commit_scalar_edit();
    [[nodiscard]] auto is_scalar_edit_active() const -> bool;
    // Numeric scalar edit (MCP slide_mesh_components): begin, one step with
    // `input`, commit. False (error set) when refused.
    auto run_scalar_edit(
        Scalar_edit_kind            kind,
        const Scalar_input&         input,
        Scalar_edit_result&         result,
        std::string&                error,
        const Scalar_topology_step* topology_step = nullptr
    ) -> bool;

    Transform_tool_shared shared;

private:
    void window_imgui       ();
    void on_hover_scene_view(Hover_scene_view_message& message);
    void on_selection       (Selection_message& message);
    void on_active_scene    (Active_scene_changed_message& message);
    void on_active_item     (Active_item_changed_message& message);
    void on_animation_update(Animation_update_message& message);
    void on_node_touched    (Node_touched_message& message);
    void on_render_scene_view(Render_scene_view_message& message);
    void on_close_scene     (Close_scene_message& message);
    void on_items_removed   (Items_removed_message& message);
    void update_for_view    (Scene_view* scene_view);
    // The handle visualizations hold ONE view-dependent state (view distance,
    // view scale), which every rendered viewport rewrites for itself. Picking
    // and dragging happen in the hovered view, so they evaluate the handles
    // for that view first; otherwise they would use whichever viewport was
    // rendered last (wrong handle sizes with several viewports open).
    void evaluate_handles_for(Scene_view& scene_view);
    // True when scene_view shows the active scene (the scene the gizmo
    // targets); the gizmo is visible, hoverable and draggable only there.
    [[nodiscard]] auto is_scene_view_of_active_scene(Scene_view* scene_view) const -> bool;
    void update_hover       (Scene_view& scene_view, glm::vec3 ray_origin, glm::vec3 ray_direction);
    void clear_hover_state  ();
    auto update_box_face_hover(Scene_view* scene_view) -> bool;
    [[nodiscard]] auto get_cast_ray_style() const -> Ray_hit_style;
    void render_rays        (erhe::scene::Node& node);
    void render_initial_position_ray();
    // Draws the constraint of the hovered handle: the plane rectangle + grid
    // for a plane translation handle, the rotation plane for a rotate ring.
    // Axis handles have no preview (the axis guide line was dropped).
    void render_hover_preview(const Render_context& context);
    // Active translate drag feedback (replaces the hover previews for the
    // duration of the drag): axis drag draws only the traveled segment from
    // initial to current drag point; plane drag draws an axis-aligned
    // rectangle spanning the two points, edges color-coded per axis, plus
    // the diagonal in the plane handle's color.
    void render_translate_drag_guides(const Render_context& context);
    // Numeric feedback for the active drag. Translate: bare coordinates -
    // initial position printed at its own window projection, current position
    // and the yellow delta stacked below the hover mesh name. Scale: labeled
    // initial/current anchor scale under the gizmo. Rotation feedback is drawn
    // by Rotate_tool::render() (angle readout at the protractor ring).
    void render_drag_readout(const Render_context& context);
    // When the gizmo anchor is outside the view frustum of the scene view's
    // whole-view camera (Scene_view::get_camera(): the viewport camera on
    // desktop, in XR the union of both eye frusta), draws a yellow x-ray
    // triangle at the edge of the view pointing toward the gizmo.
    void render_offscreen_indicator(const Render_context& context);

    // Apply a gizmo-produced world-from-anchor transform to the selected mesh
    // components (used by adjust_* and the numeric edits when component_mode).
    void apply_component_transform(const glm::mat4& updated_world_from_anchor);

    Tool_window                         m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message>  m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Selection_message>         m_selection_subscription;
    erhe::message_bus::Subscription<Active_scene_changed_message> m_active_scene_subscription;
    erhe::message_bus::Subscription<Active_item_changed_message>  m_active_item_subscription;
    erhe::message_bus::Subscription<Animation_update_message>  m_animation_update_subscription;
    erhe::message_bus::Subscription<Node_touched_message>      m_node_touched_subscription;
    erhe::message_bus::Subscription<Render_scene_view_message> m_render_scene_view_subscription;
    erhe::message_bus::Subscription<Close_scene_message>       m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message>     m_items_removed_subscription;
    Transform_tool_drag_command         m_drag_command;
    Transform_tool_cancel_drag_command  m_cancel_drag_command;
    Transform_tool_suspend_connector_snap_command m_suspend_connector_snap_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;
    Create_frame_node_command           m_create_frame_node_command;
    // The view whose pick produced the hover state below (nullptr: none).
    Scene_view*                         m_hover_state_scene_view{nullptr};
    // The view the active drag started in.
    Scene_view*                         m_drag_scene_view       {nullptr};
    Handle                              m_hover_handle {Handle::e_handle_none};
    Handle                              m_active_handle{Handle::e_handle_none};
    Handle                              m_box_face_hover_handle  {Handle::e_handle_none};
    glm::vec3                           m_box_face_hover_position{0.0f};
    bool                                m_box_face_hover_active   {false};
    // World-space grab point from the analytic handle pick (Handle_visualizations::pick).
    glm::vec3                           m_pick_position          {0.0f};
    // Control-ray intersections with the rotation sphere this frame
    // (independent of the handle pick): the XR controller ray recolors at
    // the entry and stops at the exit when no visible handle is in front.
    std::optional<glm::vec3>            m_ray_sphere_entry       {};
    std::optional<glm::vec3>            m_ray_sphere_exit        {};
    std::optional<glm::vec3>            m_ray_sphere_plane_crossing{};
    bool                                m_pick_active            {false};
    std::shared_ptr<erhe::scene::Node>  m_tool_node;
    Subtool*                            m_hover_tool      {nullptr};
    Subtool*                            m_active_tool     {nullptr};
    Subtool*                            m_rotate_subtool  {nullptr};
    Subtool*                            m_scale_subtool   {nullptr};
    // Dynamic bodies of the dragged nodes, taken over for the drag: jointed
    // ones are pulled through physics, others held kinematic (see
    // Physics_driven_drag). Started with the node drag, ended at release.
    Physics_driven_drag                 m_physics_drag;
    bool                                m_scripted_drag_active{false};
    // The surface options of the running translate drag (D4.6); off outside one.
    Surface_options                     m_drag_surface{};
    std::optional<Surface_options>      m_surface_snapped_options; // the options of the last snap that rested entries (S2.1)
    Surface_drag_direction              m_drag_surface_direction{Surface_drag_direction::explicit_vector};
    // Orient's half turn of the running drag, decided once from where it started (a camera ray flips sign at eye height).
    std::optional<Orient_half_turn>     m_drag_orient_half_turn;
    // Scratch of snap_to_surface(): the moved meshes and their descendants, and per entry the move.
    std::vector<const erhe::scene::Mesh*> m_surface_excluded;
    std::vector<glm::vec3>                m_surface_deltas;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_moved_nodes; // drag end: the entries' nodes, kept for its capacity
    // D3.13: the connector snap of the running node drag, and the moved roots at its begin (scratch).
    Kit_connector_drag_snap             m_connector_snap;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_connector_roots;
    // U27: read at the drag's begin; the suspend command's effective bindings (refreshed when bindings change);
    // whether the last step was suspended; the pointer has moved since the press; the last step's translation.
    Connector_snap_axis_mode              m_connector_snap_axis_mode{Connector_snap_axis_mode::full_offset};
    Connector_snap_orientation            m_connector_snap_orientation{Connector_snap_orientation::off}; // U50: rotate only with Orient To Surface
    std::vector<erhe::commands::Binding_desc> m_suspend_bindings;
    bool                                  m_connector_snap_step_suspended{false};
    bool                                  m_connector_snap_motion{false};
    glm::vec3                             m_last_drag_translation{0.0f};
    // Whether this edit began the live layout preview of the moved layouts (Level_kits::begin_layout_move_preview()).
    bool                                m_layout_preview_begun{false};
    Kit_snap_pair                       m_preview_snap; // K9.1: the step's snap pair, handed to the flow preview
    Rotation_inspector                  m_rotation;

    Property_editor                     m_property_editor;

    Edit_state m_edit_state;

    // Which producer owns the gizmo, decided each idle frame in update_for_view:
    // none - the node selection; shared.entries is index-aligned with the
    //        selection's transform targets (update_target_nodes() relies on it).
    // mesh_components - a mesh component mode is active, whether or not its
    //        selection currently anchors the gizmo (shared.component_mode says);
    //        shared.entries stays empty.
    // point_set - the selected points of a point set tool (doc/editor/point_set_editing.md).
    // The node-selection message handlers act only while it is none; the
    // facade methods dispatch on it.
    enum class Component_source { none, mesh_components, point_set };
    Component_source m_component_source{Component_source::none};
    [[nodiscard]] auto is_node_selection_driving() const -> bool { return m_component_source == Component_source::none; }

    Mesh_component_transform m_component_transform;
    Point_set_transform      m_point_set_transform;
    IPoint_set*              m_point_set{nullptr}; // the point set anchoring the gizmo while m_component_source is point_set

    // The pointer scalar drag. `view` is the view it started in; it is only
    // dereferenced while it is still the hovered view.
    class Scalar_drag
    {
    public:
        bool                 active        {false};
        Viewport_scene_view* view          {nullptr};
        glm::vec2            start_position{0.0f};
        glm::vec2            last_position {0.0f};
        bool                 applied       {false}; // last_position / last_clamp are valid
        bool                 last_clamp    {true};
        unsigned int         last_side     {0};     // edge slide: side of the last clamped step
        bool                 even          {false};
        bool                 flipped       {false};
        bool                 clamp         {true};
    };
    Scalar_drag m_scalar_drag;

    // Interactive FABRIK IK for a translate drag of a bone. Chain discovery
    // runs lazily on the first adjust_translation() of a drag (attempted
    // gates it to once per gesture); on success the chain's ancestor joints
    // are appended to shared.entries so record_transform_operation() covers
    // them. Both reset in end_ik_drag().
    Ik_drag m_ik_drag;
    bool    m_ik_drag_attempted{false};
    // True when m_ik_drag appended ancestor entries: end_ik_drag() then rebuilds
    // shared.entries from the selection so later anchor edits do not iterate
    // the chain joints as if they were selected.
    bool    m_ik_entries_appended{false};

    // IK branch of adjust_translation(): returns true when an IK chain drag
    // consumed the translation (FK translation must not run).
    auto try_translate_ik(glm::vec3 translation) -> bool;

    // The surface step of a translate drag (D4.6), after the tool wrote the step's transforms.
    void apply_drag_surface_step();
    // D3.13: gathers the connectors at a node drag's begin (after m_physics_drag.begin()), then per step (after the
    // step's translate, rotation or scale) translates the moved roots by the snap offset of the unsnapped step pose.
    void begin_connector_snap(float distance, Connector_snap_orientation orientation);
    void apply_drag_connector_step();
    // U50: the turn the step's snap asks for: none, full, or about the lead's up axis (rotate with Orient To Surface).
    [[nodiscard]] auto get_connector_snap_turn() const -> Kit_connector_snap_turn;
    // U44: why the step's aligned motion cannot move the moved roots (locked channels, shear); aligned when it can.
    [[nodiscard]] auto check_aligned_motion(const Kit_join_pose& motion) const -> Kit_connector_snap_alignment;
    void apply_aligned_motion(const Kit_join_pose& motion);
    // U27: the dragged handle's axis or plane with Connector_snap_axis_mode::stay_on_axis; free otherwise.
    [[nodiscard]] auto get_connector_snap_constraint() const -> Kit_connector_snap_constraint;
    // False when a moved root's locked translate channel would hold back part of the offset.
    [[nodiscard]] auto can_translate_moved_roots(const glm::vec3& offset) const -> bool;
    // After the tool wrote a step's node transforms: the moved surface-snapped kit layouts follow live (D3.9).
    void follow_layout_preview();
    void end_layout_preview();
    // True when an ancestor of the node is a target the drag moves: the node rides along rigidly.
    [[nodiscard]] auto has_moved_ancestor_entry(const erhe::scene::Node& node) const -> bool;
    // A pointer drag whose only free axis is the surface direction's axis lifts off the surface.
    [[nodiscard]] auto is_surface_axis_drag(const glm::vec3& direction) const -> bool;
    // U51 camera ray: the unit direction from the drag's viewport camera (a scripted drag: get_camera_ray_scene_view()'s)
    // through the anchor; false without a camera or with the anchor at the camera.
    [[nodiscard]] auto get_camera_ray_through(const glm::vec3& anchor, glm::vec3& out_direction) const -> bool;
    [[nodiscard]] auto get_camera_ray_scene_view() const -> std::shared_ptr<Viewport_scene_view>;
    [[nodiscard]] auto get_drag_start_half_turn(const glm::vec3& step_direction) const -> Orient_half_turn;
    // Not locked against viewport transforms, not spring-driven, no moved ancestor: moves with a surface step.
    [[nodiscard]] auto is_surface_moved_root(const Transform_entry& entry) const -> bool;
    [[nodiscard]] auto get_lead_entry() const -> const Transform_entry*;
    [[nodiscard]] auto find_lead_face_rest(erhe::raytrace::IScene& committed_rt_scene, const glm::vec3& anchor, const Surface_options& options) const -> Point_surface;
    void clear_surface_found();
    // The meshes of the entries and below them into m_surface_excluded.
    void gather_surface_excluded();
    // Ends the IK part of a drag (pointer or scripted), after its operation
    // was recorded.
    void end_ik_drag();

    // Chain visualization of a running IK drag, drawn exactly while
    // m_ik_drag.is_active() (doc/plans/rigging/ik_drag_options.md section 2).
    void render_ik_drag(const Render_context& context);

    // Reused scratch of the IK drag visualization: the chain joints' world
    // positions and the line list built from them. Both are cleared at the
    // point of use and again after use, so a drag frame allocates nothing
    // once they have reached their high-water mark. They hold no node
    // reference, so nothing scene-hosted outlives the gesture.
    std::vector<glm::vec3> m_ik_drag_positions;
    Ik_drag_line_buffer    m_ik_drag_lines;

    // Reused scratch for the Reference node picker popup (cleared + refilled each frame).
    std::vector<std::shared_ptr<erhe::Item_base>> m_reference_candidates;

    // Reused scratch for update_target_nodes: the selection resolved to gizmo
    // target nodes and de-duplicated (cleared + refilled on each update).
    std::vector<std::shared_ptr<erhe::scene::Node>> m_target_nodes;

    // Non-empty while the gizmo drives a node other than the selected one
    // (skinned mesh -> skin transform root); shown in the Transform window.
    std::string m_transform_target_note;
};

}
