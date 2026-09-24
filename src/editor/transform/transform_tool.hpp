#pragma once

#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
#include "transform/transform_tool_settings.hpp"
#include "transform/ik_drag.hpp"
#include "transform/lattice_point_transform.hpp"
#include "transform/mesh_component_transform.hpp"
#include "transform/physics_driven_drag.hpp"
#include "transform/rotation_inspector.hpp"
#include "scene/analytic_hover_provider.hpp"
#include "tools/tool.hpp"

#include "windows/property_editor.hpp"

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

    Transform_tool_settings                settings;
    std::vector<Transform_entry>           entries;
    // Arbitrary node whose orientation drives the gizmo in Reference mode.
    std::weak_ptr<erhe::scene::Node>       reference_node;
    glm::vec3                              m_initial_drag_position_from_anchor_initial_state{0.0f};
    float                                  initial_drag_position_distance_to_camera{0.0f};
    erhe::scene::Trs_transform             world_from_anchor_initial_state;
    erhe::scene::Trs_transform             world_from_anchor;
    bool                                   touched             {false};
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

    // Rotate ring k (handle e_handle_rotate_x/y/z for k = 0/1/2) rotates
    // about frames[k][0] and lies in the plane of frames[k][1], frames[k][2]
    // (world space, orthonormal). Normally ring k is gizmo basis axis k. With
    // the Rotation inspector showing Euler angles of a single node the rings
    // form the Euler gimbal instead: ring k rotates about the k-th axis of
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

    void touch();
    void record_transform_operation();

    // A drag driven by code instead of the pointer (the MCP drag_selection
    // tool): begin_scripted_drag() starts a node drag of the selection the
    // way a gizmo press does - including the physics pull of jointed dynamic
    // bodies - the caller then feeds adjust_translation() / adjust() /
    // adjust_scale() once per frame, and end_scripted_drag() records the
    // undoable operation and releases the bodies like a gizmo release.
    // Refused while a pointer drag, a scripted drag or a component selection
    // is active, or when nothing is selected.
    auto begin_scripted_drag(Transform_drag_kind kind) -> bool;
    void end_scripted_drag  ();
    [[nodiscard]] auto is_scripted_drag_active() const -> bool;
    // True while the current drag pulls the node's jointed dynamic body
    // through physics instead of writing the node transform.
    [[nodiscard]] auto is_node_pulled_through_physics(const erhe::scene::Node* node) const -> bool;

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
    void adjust_translation(glm::vec3 translation);
    void adjust_rotation   (glm::vec3 center_of_rotation, glm::quat rotation);
    void adjust_scale      (glm::vec3 center_of_scale,    glm::vec3 scale);
    void update_visibility ();
    void update_transforms ();

    // Mesh component editing (vertex/edge/face). Begins a component edit gesture (drag
    // or numeric edit), commits the queued operation, and reports whether a gesture is
    // in progress. Exposed for Edit_state (numeric fields).
    void begin_component_edit  ();
    void commit_component_edit ();
    [[nodiscard]] auto is_component_mode      () const -> bool { return shared.component_mode; }
    [[nodiscard]] auto is_component_edit_active() const -> bool;

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
    Subtool*                            m_last_active_tool{nullptr};
    Subtool*                            m_rotate_subtool  {nullptr};
    Subtool*                            m_scale_subtool   {nullptr};
    // Dynamic bodies of the dragged nodes, taken over for the drag: jointed
    // ones are pulled through physics, others held kinematic (see
    // Physics_driven_drag). Started with the node drag, ended at release.
    Physics_driven_drag                 m_physics_drag;
    bool                                m_scripted_drag_active{false};
    Rotation_inspector                  m_rotation;

    Property_editor                     m_property_editor;

    Edit_state m_edit_state;

    // Which producer owns the gizmo when shared.component_mode is set: the mesh
    // component selection or a designated lattice node's control point. Decided
    // each idle frame in update_for_view; the façade methods dispatch on it.
    enum class Component_source { none, mesh_components, lattice_point };
    Component_source m_component_source{Component_source::none};

    Mesh_component_transform m_component_transform;
    Lattice_point_transform  m_lattice_point_transform;

    // Interactive FABRIK IK for a translate drag of a bone. Chain discovery
    // runs lazily on the first adjust_translation() of a drag (attempted
    // gates it to once per gesture); on success the chain's ancestor joints
    // are appended to shared.entries so record_transform_operation() covers
    // them. Both reset in end_drag().
    Ik_drag m_ik_drag;
    bool    m_ik_drag_attempted{false};
    // True when m_ik_drag appended ancestor entries: end_drag() then rebuilds
    // shared.entries from the selection so later anchor edits do not iterate
    // the chain joints as if they were selected.
    bool    m_ik_entries_appended{false};

    // IK branch of adjust_translation(): returns true when an IK chain drag
    // consumed the translation (FK translation must not run).
    auto try_translate_ik(glm::vec3 translation) -> bool;

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
