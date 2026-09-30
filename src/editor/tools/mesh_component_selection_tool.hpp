#pragma once

#include "tools/tool.hpp"

#include "app_message.hpp"
#include "tools/mesh_component_selection.hpp" // Mesh_component_mode
#include "tools/screen_snap.hpp"
#include "transform/mesh_component_transform.hpp" // Scalar_input, Scalar_topology_step
#include "erhe_commands/command.hpp"
#include "erhe_geometry/operation/bevel_edges.hpp"
#include "erhe_geometry/operation/inset_faces.hpp"
#include "erhe_geometry/operation/knife_cut.hpp"
#include "erhe_geometry/topology.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include <geogram/basic/numeric.h>

#include <glm/glm.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class App_message_bus;
class Mesh_component_selection;
class Mesh_component_selection_tool;
class Scene_view;
class Viewport_scene_view;

// Gesture sub-mode of the component selection tool. Click is the legacy
// single-pick behavior; Box drags a rectangle; Paint drags a brush disk
// (Blender Circle Select). In Face mode Box and Paint scan the GPU id-buffer
// over a screen region to gather visible faces; in Vertex and Edge mode they
// project the vertices of the view's meshes on the CPU and test the region
// (doc/editor/mesh_component_selection.md section 7).
enum class Component_gesture_mode {
    click = 0,
    box   = 1,
    paint = 2
};

// How a region (box / brush) select combines with the current selection,
// after the plain gesture's clear.
enum class Region_select_operation : unsigned int {
    add      = 0,
    subtract = 1
};

// Left-mouse command that selects a mesh sub-component (vertex / edge / face)
// under the pointer. It only becomes ready while a component mode is active,
// so in Object mode it stays inactive and the object Selection handles the
// click unchanged.
class Component_select_command : public erhe::commands::Command
{
public:
    Component_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

// Left-mouse drag that box-selects components. Ready only in Box gesture
// sub-mode + a mesh component mode; once the drag moves it becomes the active
// mouse command (blocking the single-click command). In Face mode the selection
// is committed a few frames after release, when the async id-buffer region scan
// completes; in Vertex / Edge mode on the first gesture_update after release
// (see Mesh_component_selection_tool::gesture_update).
class Component_box_select_command : public erhe::commands::Command
{
public:
    Component_box_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    App_context& m_context;
};

// Per-frame update command (bound via bind_command_to_update) that drives the
// deferred box commit and paint accumulation, and keeps the brush-radius wheel
// command armed while paint-selecting. Never consumes input.
class Component_gesture_update_command : public erhe::commands::Command
{
public:
    Component_gesture_update_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// Left-mouse drag that paint-selects components under a brush disk (Blender
// Circle Select). Ready only in Paint gesture sub-mode + a mesh component mode. Bound
// with call_on_button_down_without_motion so a single click selects under the
// brush too. Modifiers (captured at stroke start): plain/Shift add (plain also
// clears first), Ctrl subtract. Components are added continuously along the path.
class Component_paint_select_command : public erhe::commands::Command
{
public:
    Component_paint_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    App_context& m_context;
};

// Mouse wheel that resizes the paint brush. Kept Ready (by gesture_update) while
// paint-selecting + hovering a viewport so it out-ranks the fly-camera zoom for
// the wheel (see Commands::sort_mouse_wheel_bindings); inactive otherwise so the
// wheel zooms the camera as usual.
class Component_brush_radius_command : public erhe::commands::Command
{
public:
    Component_brush_radius_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
};

// Key that switches the gesture sub-mode (B -> Box, C -> Paint), a shortcut for
// the toolbar combo. Only consumes the key while in a mesh component mode, so the
// key falls through to other bindings (e.g. brush preview on C) otherwise.
class Component_gesture_hotkey_command : public erhe::commands::Command
{
public:
    Component_gesture_hotkey_command(erhe::commands::Commands& commands, App_context& context, const char* name, Component_gesture_mode mode);
    auto try_call() -> bool override;

private:
    App_context&           m_context;
    Component_gesture_mode m_mode;
};

// Key that grows (Select More) the mesh component selection by one border ring,
// in the current component mode. Consumes the key only while in a component mode
// (returns false in Object mode so the key falls through). Blender: Ctrl+Numpad+.
class Component_grow_selection_command : public erhe::commands::Command
{
public:
    Component_grow_selection_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// Key that shrinks (Select Less) the mesh component selection by one border ring,
// in the current component mode. Consumes the key only while in a component mode
// (returns false in Object mode so the key falls through). Blender: Ctrl+Numpad-.
class Component_shrink_selection_command : public erhe::commands::Command
{
public:
    Component_shrink_selection_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// Which loop select gesture a click is: Alt+click (loop) or Ctrl+Alt+click
// (ring). Face mode runs the face loop for both.
enum class Loop_select_gesture : unsigned int {
    loop = 0,
    ring = 1
};

// Left-mouse release with Alt (loop) or Ctrl+Alt (ring), each also with
// Shift: loop / ring select from the edge nearest to the pointer
// (doc/editor/mesh_component_selection.md section 2). Ready only while a mesh
// component mode is active and a component is under the pointer.
class Component_loop_select_command : public erhe::commands::Command
{
public:
    Component_loop_select_command(erhe::commands::Commands& commands, App_context& context, const char* name, Loop_select_gesture gesture);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context&        m_context;
    Loop_select_gesture m_gesture;
};

// The selection commands of doc/editor/mesh_component_selection.md section 2.
enum class Component_selection_action : unsigned int {
    select_all                   = 0, // Ctrl+A
    select_none                  = 1, // Alt+A
    invert                       = 2, // Ctrl+I
    select_linked_under_cursor   = 3, // L
    select_linked_from_selection = 4  // Ctrl+L
};

[[nodiscard]] auto c_str(Component_selection_action action) -> const char*;

// Key command running one Component_selection_action. Consumes the key only
// while a mesh component mode is active (returns false otherwise, so the key
// falls through to other bindings).
class Component_selection_action_command : public erhe::commands::Command
{
public:
    Component_selection_action_command(erhe::commands::Commands& commands, App_context& context, Component_selection_action action);
    auto try_call() -> bool override;

private:
    App_context&               m_context;
    Component_selection_action m_action;
};

// G: starts the pointer-driven slide of the mesh component selection
// (doc/editor/transform.md "Scalar edits"): vertex slide in vertex mode, edge
// slide in edge and face mode. Consumes the key only when the slide starts.
class Component_slide_command : public erhe::commands::Command
{
public:
    Component_slide_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// The modal actions of a running component slide.
enum class Component_modal_action : unsigned int {
    confirm        = 0, // Enter
    cancel         = 1, // Escape (also cancels a gizmo component edit)
    toggle_even    = 2, // E
    toggle_flipped = 3, // F
    toggle_clamp   = 4, // C (Alt held: unclamped while held; the knife: cut through)
    confirm_click  = 5, // left press: confirm (the knife: add a cut point)
    cancel_click   = 6  // right press: cancel (the knife: end the polyline)
};

[[nodiscard]] auto c_str(Component_modal_action action) -> const char*;

// Key or mouse button command running one Component_modal_action. Consumes
// its input only while a component slide runs (cancel: while any mesh
// component edit runs), so the key falls through to its other bindings
// otherwise. The mouse button commands are Ready for the length of the slide,
// which ranks them above the other left / right press commands.
class Component_modal_command : public erhe::commands::Command
{
public:
    Component_modal_command(erhe::commands::Commands& commands, App_context& context, const char* name, Component_modal_action action);
    void try_ready          () override;
    auto try_call           () -> bool override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context&           m_context;
    Component_modal_action m_action;
};

// The loop cut keys (doc/editor/mesh_modeling.md): Ctrl+R starts the loop cut
// mode; while it runs the others change the preview's cut count and
// smoothness. Enter / left click confirm and Escape / right click cancel
// through Component_modal_command.
enum class Loop_cut_action : unsigned int {
    start           = 0, // Ctrl+R
    more_cuts       = 1, // PageUp, numpad plus
    fewer_cuts      = 2, // PageDown, numpad minus
    more_smoothness = 3, // Alt+PageUp, Alt+numpad plus
    less_smoothness = 4, // Alt+PageDown, Alt+numpad minus
    type_digit      = 5  // 0 .. 9 (main row and numpad): typed cut count
};

[[nodiscard]] auto c_str(Loop_cut_action action) -> const char*;

// Key command running one Loop_cut_action. start consumes the key only when
// the loop cut mode starts; the others only while it runs, so the keys fall
// through to their other bindings (fly camera PageUp / PageDown, hotbar
// digits) otherwise.
class Component_loop_cut_command : public erhe::commands::Command
{
public:
    Component_loop_cut_command(erhe::commands::Commands& commands, App_context& context, const char* name, Loop_cut_action action, int digit);
    auto try_call() -> bool override;

private:
    App_context&    m_context;
    Loop_cut_action m_action;
    int             m_digit;
};

// Mouse wheel while the loop cut mode runs: the cut count, or with Alt the
// smoothness. Kept Ready while the mode runs so it out-ranks the fly-camera
// zoom; inactive otherwise.
class Component_loop_cut_wheel_command : public erhe::commands::Command
{
public:
    Component_loop_cut_wheel_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
};

// Mouse wheel while the bevel mode runs: after S the segment count, with
// Shift the profile; otherwise it falls through (fly-camera zoom). Kept Ready
// while the mode runs; inactive otherwise.
class Component_bevel_wheel_command : public erhe::commands::Command
{
public:
    Component_bevel_wheel_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    App_context& m_context;
};

// What a loop cut did (Mesh_component_selection_tool::loop_cut()).
class Loop_cut_result
{
public:
    std::size_t                 ring_length   {0};
    erhe::geometry::Walk_shape  ring_shape    {erhe::geometry::Walk_shape::open};
    std::size_t                 inner_edges   {0}; // the selection afterwards (0: the single edge case)
    std::size_t                 slide_vertices{0};
    std::size_t                 moved_vertices{0};
    std::size_t                 loops         {0};
};

// The inset keys (doc/editor/mesh_modeling.md): I starts the inset mode in
// face mode; while it runs O, I, B and R toggle outset, individual, boundary
// and relative offset (E toggles even offset through Component_modal_command,
// Enter / left click confirm and Escape / right click cancel).
enum class Inset_action : unsigned int {
    start             = 0, // I
    toggle_outset     = 1, // O
    toggle_individual = 2, // I
    toggle_boundary   = 3, // B
    toggle_relative   = 4  // R
};

[[nodiscard]] auto c_str(Inset_action action) -> const char*;

// Key command running one Inset_action. start consumes the key only when the
// inset mode starts, the toggles only while it runs, so the keys fall through
// to their other bindings (B box gesture) otherwise.
class Component_inset_command : public erhe::commands::Command
{
public:
    Component_inset_command(erhe::commands::Commands& commands, App_context& context, const char* name, Inset_action action);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    Inset_action m_action;
};

// The bevel keys (doc/editor/mesh_modeling.md): Ctrl+B starts the bevel mode
// in edge or vertex mode; while it runs W cycles the offset type (offset,
// width), L toggles loop slide, S hands the mouse wheel to the segment count,
// PageUp / PageDown change the segment count and ] / [ the profile (Enter /
// left click confirm and Escape / right click cancel through
// Component_modal_command).
enum class Bevel_action : unsigned int {
    start              = 0, // Ctrl+B
    cycle_offset_type  = 1, // W
    toggle_loop_slide  = 2, // L
    segments_wheel     = 3, // S: the wheel changes the segment count from now on
    more_segments      = 4, // PageUp
    fewer_segments     = 5, // PageDown
    more_profile       = 6, // ]
    less_profile       = 7  // [
};

[[nodiscard]] auto c_str(Bevel_action action) -> const char*;

// Key command running one Bevel_action. start consumes the key only when the
// bevel mode starts, the others only while it runs, so the keys fall through
// to their other bindings (fly camera W, select linked L) otherwise.
class Component_bevel_command : public erhe::commands::Command
{
public:
    Component_bevel_command(erhe::commands::Commands& commands, App_context& context, const char* name, Bevel_action action);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    Bevel_action m_action;
};

// The knife keys (doc/editor/mesh_modeling.md): K starts the knife mode;
// while it runs Space confirms, Ctrl+Z removes the last cut point, A cycles
// the angle constraint and X / Y / Z lock an axis. Enter confirms, Escape
// cancels, C toggles cut through and the clicks add points / end the
// polyline through Component_modal_command.
enum class Knife_action : unsigned int {
    start       = 0, // K
    confirm     = 1, // Space
    undo_point  = 2, // Ctrl+Z
    cycle_angle = 3, // A
    lock_x      = 4, // X
    lock_y      = 5, // Y
    lock_z      = 6  // Z
};

[[nodiscard]] auto c_str(Knife_action action) -> const char*;

// Key command running one Knife_action. start consumes the key only when the
// knife mode starts, the others only while it runs, so the keys fall through
// to their other bindings (hotbar Space, fly camera A, brush Z / X) otherwise.
class Component_knife_command : public erhe::commands::Command
{
public:
    Component_knife_command(erhe::commands::Commands& commands, App_context& context, const char* name, Knife_action action);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    Knife_action m_action;
};

// The knife's angle constraint (A cycles off -> screen -> relative -> off):
// the candidate point's screen direction from the previous point is rounded
// to 30 degree steps, measured from the screen X axis (screen) or from the
// screen direction of the edge the previous point lies on (relative; as
// screen when the previous point is not on an edge).
enum class Knife_angle_constraint : unsigned int {
    off      = 0,
    screen   = 1,
    relative = 2
};

[[nodiscard]] auto c_str(Knife_angle_constraint constraint) -> const char*;

// The knife's axis lock (X / Y / Z; the same key again unlocks): the
// candidate point is the point of the world axis line through the previous
// point nearest to the pointer ray.
enum class Knife_axis_lock : unsigned int {
    none = 0,
    x    = 1,
    y    = 2,
    z    = 3
};

// The projection of a Knife_view built from world-space camera data.
enum class Knife_projection : unsigned int {
    perspective  = 0,
    orthographic = 1
};

// A Knife_view in the mesh space of `node` from a world-space view:
// clip_from_mesh = clip_from_world * world_from_node, the eye and the view
// direction transformed to mesh space.
[[nodiscard]] auto make_knife_view(
    const glm::mat4&         clip_from_world,
    const glm::vec3&         eye_in_world,
    const glm::vec3&         view_direction_in_world,
    Knife_projection         projection,
    float                    viewport_width,
    float                    viewport_height,
    const erhe::scene::Node& node
) -> erhe::geometry::operation::Knife_view;

// What a knife cut did (Mesh_component_selection_tool::knife_cut()).
class Knife_cut_result
{
public:
    bool        changed     {false}; // false: the points made no cut edge, nothing was done
    std::size_t cut_vertices{0};
    std::size_t cut_edges   {0};
};

// What an inset did (Mesh_component_selection_tool::inset()).
class Inset_result
{
public:
    bool        changed       {false}; // false: the region has no boundary edges, nothing was done
    std::size_t inset_vertices{0};
    std::size_t inset_facets  {0};
    std::size_t rim_facets    {0};
};

// What a bevel did (Mesh_component_selection_tool::bevel()).
class Bevel_result
{
public:
    bool        changed          {false}; // false: no selected edge could be beveled, nothing was done
    std::size_t beveled_edges    {0};
    std::size_t boundary_vertices{0};
    std::size_t edge_facets      {0};
    std::size_t vertex_facets    {0};
};

// Blender-style mesh component selection tool. A background tool whose mode
// (Object / Vertex / Edge / Face, held by Mesh_component_selection) controls
// whether it intercepts viewport clicks. Renders the current selection and the
// hovered component into the desktop viewport via Primitive_renderer. Initial
// scope: non-skinned meshes, single active mesh, no editing.
class Mesh_component_selection_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Mesh_component_selection_tool(
        erhe::commands::Commands&    commands,
        App_context&                 context,
        App_message_bus&             app_message_bus,
        Mesh_component_selection&    mesh_component_selection,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Contributes the mode combo + clear button to the viewport toolbar
    // (called from Viewport_scene_view::viewport_toolbar).
    void viewport_toolbar();

    // Called by Component_select_command.
    [[nodiscard]] auto try_ready() const -> bool;
    auto               on_select()       -> bool;

    // Gesture sub-mode (click / box / paint), surfaced in viewport_toolbar and
    // settable from the B / C hotkeys.
    [[nodiscard]] auto get_gesture_mode() const -> Component_gesture_mode;
    void               set_gesture_mode(Component_gesture_mode mode);
    // B / C hotkey: switch to `mode` and consume, but only while in a mesh
    // component mode (returns false otherwise so the key falls through).
    [[nodiscard]] auto try_set_gesture_hotkey(Component_gesture_mode mode) -> bool;

    // Called by Component_loop_select_command: the section 4.2 dispatch of
    // doc/plans/mesh_modeling.md. The mode picks the walk (face mode: face
    // loop; otherwise the gesture's edge loop or edge ring), Shift in
    // modifier_mask extends, or deselects when the walked set is already
    // selected, and a plain Alt+click on a boundary edge whose loop is
    // already selected cycles to the whole boundary loop and back.
    [[nodiscard]] auto on_loop_select(Loop_select_gesture gesture, uint32_t modifier_mask) -> bool;

    // The edge loop delimit loop select uses: outer corners, plus creases
    // while the toolbar's "Loop stops at creases" is checked.
    [[nodiscard]] auto get_loop_delimit() const -> erhe::geometry::Edge_loop_delimit;

    // Called by the editor's key event handler when the Shift / Ctrl / Alt
    // state changes: the loop preview follows the modifiers.
    void on_modifiers_changed();

    // Called by Component_grow_selection_command / Component_shrink_selection_command
    // (Blender Select More / Select Less). Grow/shrink the component selection by
    // one border ring in the current mode; consumes the key (returns true) only
    // while a component mode is active, so in Object mode the key falls through.
    [[nodiscard]] auto grow_selection  () -> bool;
    [[nodiscard]] auto shrink_selection() -> bool;

    // Called by Component_selection_action_command and the toolbar buttons.
    // Runs `action` and returns true only while a mesh component mode is
    // active (and, for select_linked_under_cursor, a component is hovered).
    [[nodiscard]] auto run_selection_action(Component_selection_action action) -> bool;

    // Called by Component_slide_command (G): starts the pointer slide in the
    // hovered viewport. Called by Component_modal_command: runs the action
    // while the slide runs.
    [[nodiscard]] auto begin_slide     () -> bool;
    [[nodiscard]] auto run_modal_action(Component_modal_action action) -> bool;
    // True while a pointer slide runs.
    [[nodiscard]] auto is_slide_active () const -> bool;
    // True while a pointer slide, the loop cut, inset, bevel or knife mode runs:
    // the selection gestures stand down and the modal click commands own the
    // clicks.
    [[nodiscard]] auto is_modal_active () const -> bool;

    // Loop cut (doc/editor/mesh_modeling.md). begin_loop_cut() (Ctrl+R)
    // starts the loop cut mode over a hovered content mesh in a component
    // mode; run_loop_cut_action() / adjust_loop_cut_wheel() change the preview
    // while it runs (false when it does not run, so the input falls through).
    [[nodiscard]] auto begin_loop_cut       () -> bool;
    [[nodiscard]] auto run_loop_cut_action  (Loop_cut_action action, int digit) -> bool;
    [[nodiscard]] auto adjust_loop_cut_wheel(float wheel_delta, uint32_t modifier_mask) -> bool;
    [[nodiscard]] auto is_loop_cut_active   () const -> bool { return m_loop_cut.active; }
    [[nodiscard]] auto get_loop_cut_cuts      () const -> int   { return m_loop_cut.cuts; }
    [[nodiscard]] auto get_loop_cut_smoothness() const -> float { return m_loop_cut.smoothness; }

    // The numeric loop cut (MCP loop_cut_mesh, doc/plans/mesh_modeling.md
    // D6): cut the edge ring through edge_key of target with `cuts` cuts and
    // `smoothness`, then slide the new loops by `slide` (edge slide, factor in
    // [-1, 1]) and commit the whole gesture as one undo entry. The single
    // edge case (a seed without a quad facet) cuts that edge only. False
    // (error set) when refused.
    auto loop_cut(
        const Mesh_component_target& target,
        Mesh_edge_key                edge_key,
        int                          cuts,
        float                        smoothness,
        const Scalar_input&          slide,
        Loop_cut_result&             result,
        std::string&                 error
    ) -> bool;

    // Inset (doc/editor/mesh_modeling.md). begin_inset() (I) starts the inset
    // mode on the live face selection of one mesh primitive: the topology
    // step runs at once with thickness 0, then the pointer drags the
    // thickness (Ctrl held: the depth). run_inset_action() toggles an option
    // while it runs (false when it does not run, so the key falls through).
    [[nodiscard]] auto begin_inset      () -> bool;
    [[nodiscard]] auto run_inset_action (Inset_action action) -> bool;
    [[nodiscard]] auto is_inset_active  () const -> bool { return m_inset.active; }
    [[nodiscard]] auto get_inset_options() const -> const erhe::geometry::operation::Inset_faces_options& { return m_inset.options; }

    // The numeric inset (MCP inset_mesh_faces, Operations window "Inset",
    // doc/plans/mesh_modeling.md D6): inset the live face selection of one
    // mesh primitive with the options' thickness and depth and commit it as
    // one undo entry "Inset". A region without boundary edges changes
    // nothing (result.changed false, nothing queued). False (error set) when
    // refused.
    auto inset(
        const erhe::geometry::operation::Inset_faces_options& options,
        Inset_result&                                         result,
        std::string&                                          error
    ) -> bool;

    // Bevel (doc/editor/mesh_modeling.md). begin_bevel() (Ctrl+B) starts the
    // bevel mode on the live edge selection (vertex mode: the edges between
    // selected vertices) of one mesh primitive: the topology step runs at once
    // with amount 0, then the pointer drags the amount. run_bevel_action()
    // runs a modal key while it runs (false when it does not run, so the key
    // falls through); adjust_bevel_wheel() the wheel (false when it does not
    // apply, so the wheel falls through to the camera).
    [[nodiscard]] auto begin_bevel       () -> bool;
    [[nodiscard]] auto run_bevel_action  (Bevel_action action) -> bool;
    [[nodiscard]] auto adjust_bevel_wheel(float wheel_delta, uint32_t modifier_mask) -> bool;
    [[nodiscard]] auto is_bevel_active  () const -> bool { return m_bevel.active; }
    [[nodiscard]] auto get_bevel_options() const -> const erhe::geometry::operation::Bevel_edges_options& { return m_bevel.options; }

    // The numeric bevel (MCP bevel_mesh_edges, Operations window "Bevel",
    // doc/plans/mesh_modeling.md D6): bevel the live edge selection (vertex
    // mode: the edges between selected vertices) of one mesh primitive with
    // the options' amount and commit it as one undo entry "Bevel". A selection
    // without a bevelable edge changes nothing (result.changed false, nothing
    // queued). False (error set) when refused.
    auto bevel(
        const erhe::geometry::operation::Bevel_edges_options& options,
        Bevel_result&                                         result,
        std::string&                                          error
    ) -> bool;

    // Knife (doc/editor/mesh_modeling.md). begin_knife() (K) starts the knife
    // mode over a hovered content mesh in a component mode; the first cut
    // point picks the mesh the session cuts. run_knife_action() runs a
    // modal key while it runs (false when it does not run, so the key falls
    // through). cancel_knife() ends the mode with the mesh untouched (false
    // when it does not run).
    [[nodiscard]] auto begin_knife      () -> bool;
    [[nodiscard]] auto run_knife_action (Knife_action action) -> bool;
    auto               cancel_knife     () -> bool;
    [[nodiscard]] auto is_knife_active  () const -> bool { return m_knife.active; }
    [[nodiscard]] auto get_knife_options() const -> const erhe::geometry::operation::Knife_options& { return m_knife.options; }

    // The numeric knife (MCP knife_cut_mesh, doc/plans/mesh_modeling.md D6):
    // one polyline of cut points (mesh space) cut into target with the
    // options, seen from `view` (mesh space) or, when view is null, from the
    // camera of the last hovered viewport; the cut edges become the
    // selection in edge mode and the cut is one undo entry "Knife". Points
    // that make no cut edge change nothing (result.changed false, nothing
    // queued). False (error set) when refused.
    auto knife_cut(
        const Mesh_component_target&                            target,
        const erhe::geometry::operation::Knife_view*            view,
        std::span<const erhe::geometry::operation::Knife_point> points,
        const erhe::geometry::operation::Knife_options&         options,
        Knife_cut_result&                                       result,
        std::string&                                            error
    ) -> bool;

    // Select all targets: the meshes of the live entries plus the meshes of
    // the object Selection that component selection can address; when both
    // are empty, the hovered mesh. Clears and fills out_targets.
    void collect_select_all_targets(std::vector<Mesh_component_target>& out_targets);

    // Called by Component_box_select_command.
    [[nodiscard]] auto box_select_try_ready() const -> bool;
    // True while Box gesture sub-mode + a mesh component mode are both active
    // (the in-drag guard; does not require a hovered viewport, unlike
    // box_select_try_ready). Used to drop a drag command left armed across a
    // sub-mode change.
    [[nodiscard]] auto is_gesture_box_mode() const -> bool;
    void               box_select_update(glm::vec2 window_position, bool real_motion);
    void               box_select_release();

    // Called by Component_paint_select_command.
    [[nodiscard]] auto paint_select_try_ready() const -> bool;
    [[nodiscard]] auto is_gesture_paint_mode() const -> bool;
    void               paint_select_update(glm::vec2 window_position, bool real_motion);
    void               paint_select_release();

    // Called by Component_brush_radius_command (mouse wheel).
    void               adjust_brush_radius(float wheel_delta);

    // Called by Component_gesture_update_command once per frame.
    void gesture_update();

    // Debug/test entry (MCP debug_region_select): drive a region select in the
    // current mesh component mode over an explicit viewport-pixel rectangle (or
    // brush disk), bypassing the mouse. Commits over the next frames via
    // gesture_update, like a real gesture: Face mode through the id-buffer scan,
    // Vertex / Edge mode through the CPU projection over the last hovered
    // viewport (the first viewport window when none was hovered).
    void debug_region_select(int x, int y, int width, int height, bool is_brush, float brush_radius, bool replace, bool subtract);

    // Highlight one component from outside the viewport - the Geometry
    // Spreadsheet row under the pointer - drawn in the hover color like the
    // pointer hover, in any mesh component mode. `mode` names the component
    // kind (vertex / edge / face). element is the vertex or facet index; an
    // edge is its vertex pair (edge_v0, edge_v1). Held weakly; the caller
    // clears it when its hover ends.
    void set_external_hover(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        Mesh_component_mode                              mode,
        GEO::index_t                                     element,
        GEO::index_t                                     edge_v0,
        GEO::index_t                                     edge_v1
    );
    void clear_external_hover();

    // The world-space point of the content hover in the last hovered scene
    // view (the target of Geometry.Merge.AtCursor); std::nullopt when that view
    // has no valid content hover.
    [[nodiscard]] auto get_hovered_content_position() const -> std::optional<glm::vec3>;

    // Draws the rubber-band box (and, later, brush circle) into the viewport
    // window's ImGui draw list. Called by Viewport_window::imgui().
    void draw_gesture_overlay(const Viewport_scene_view* viewport_scene_view);

private:
    // Build and submit an id-buffer scan request for the current box rectangle
    // (window coords -> viewport coords via the gesture's scene view).
    void request_box_scan();
    // Build and submit an id-buffer disk scan for the current brush.
    void request_paint_scan();
    // A screen region in viewport pixels (the space of
    // Viewport_scene_view::get_viewport_from_window and project_to_viewport).
    enum class Region_shape : unsigned int {
        rectangle = 0,
        disk      = 1
    };
    class Component_region
    {
    public:
        Region_shape shape {Region_shape::rectangle};
        glm::vec2    min   {0.0f, 0.0f}; // rectangle
        glm::vec2    max   {0.0f, 0.0f}; // rectangle
        glm::vec2    center{0.0f, 0.0f}; // disk
        float        radius{0.0f};       // disk

        [[nodiscard]] auto contains(glm::vec2 position_in_viewport) const -> bool;
    };
    // Vertex / Edge mode region select on the CPU: projects every vertex of the
    // visible, component-addressable content meshes of the view's scene and
    // adds (or subtracts) the vertices inside the region, or the edges whose
    // both endpoints are inside, then flushes. Selects through the mesh (no
    // occlusion test).
    void select_components_in_region(
        const Viewport_scene_view& viewport_scene_view,
        const Component_region&    region,
        Region_select_operation    operation
    );
    // The current box gesture / brush as a region in the view's pixels.
    [[nodiscard]] auto make_box_region  (const Viewport_scene_view& viewport_scene_view) const -> Component_region;
    [[nodiscard]] auto make_brush_region(const Viewport_scene_view& viewport_scene_view) const -> Component_region;
    // The view the MCP debug region select projects in.
    [[nodiscard]] auto get_debug_scene_view() const -> const Viewport_scene_view*;
    // Abandon any in-flight async region-scan drains (box deferred commit, paint
    // post-release drain, MCP debug scan) so their results are not applied. Used
    // by the Clear button: an explicit clear must be authoritative even while a
    // just-released gesture's final scan is still completing a few frames later.
    void cancel_pending_scans();
    // Component under the pointer for the active mode, resolved from the
    // content Hover_entry. Edge fields hold the picked edge's vertex pair.
    class Pick_result
    {
    public:
        bool                                      valid          {false};
        std::shared_ptr<erhe::scene::Mesh>        mesh           {};
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry       {};
        GEO::index_t                              facet          {0};
        GEO::index_t                              vertex         {0};
        GEO::index_t                              edge_v0        {0};
        GEO::index_t                              edge_v1        {0};
    };

    [[nodiscard]] auto pick(Scene_view& scene_view) const -> Pick_result;
    // The external hover as a Pick_result; invalid when none is set or its
    // mesh / geometry is gone or no longer addresses the element.
    [[nodiscard]] auto resolve_external_hover() const -> Pick_result;

    // Loop select preview (Alt / Ctrl+Alt held over an edge): the elements a
    // click would select, drawn by tool_render in the hover color. Recomputed
    // by update_loop_preview() only when its key (scene view, mesh, geometry,
    // picked edge, kind, delimit) changes - on hover, modifier, mode, delimit
    // and geometry changes - never per frame. References are weak, and the
    // preview is drawn only while its target is live
    // (Mesh_component_selection::is_live), so it never outlives a scene close,
    // an undo removal or a geometry swap.
    class Loop_preview
    {
    public:
        bool                                    valid          {false};
        const Scene_view*                       scene_view     {nullptr};
        std::weak_ptr<erhe::scene::Mesh>        mesh           {};
        std::size_t                             primitive_index{0};
        std::weak_ptr<erhe::geometry::Geometry> geometry       {};
        Mesh_edge_key                           edge_key       {0, 0};
        Loop_kind                               kind           {Loop_kind::edge_loop};
        erhe::geometry::Edge_loop_delimit       delimit        {erhe::geometry::Edge_loop_delimit::none};
    };
    void update_loop_preview    ();
    void invalidate_loop_preview();
    Loop_preview              m_loop_preview{};
    std::vector<GEO::index_t> m_loop_preview_elements{}; // edge or facet indices (cleared at use, capacity kept)
    bool                      m_loop_delimit_crease{true};

    // Boundary cycle state of the last plain Alt+click on a boundary edge:
    // boundary_selected is true when that click selected the whole boundary
    // loop, so the next click on the same edge returns to the edge loop.
    class Boundary_cycle
    {
    public:
        std::weak_ptr<erhe::scene::Mesh>        mesh             {};
        std::weak_ptr<erhe::geometry::Geometry> geometry         {};
        Mesh_edge_key                           edge_key         {0, 0};
        bool                                    boundary_selected{false};
    };
    Boundary_cycle m_boundary_cycle{};

    // Loop cut mode (Ctrl+R): the cut count and smoothness the modal keys set,
    // and the typed digits (0: none typed yet).
    class Loop_cut_state
    {
    public:
        bool  active    {false};
        int   cuts      {1};
        float smoothness{0.0f};
        int   typed_cuts{0};
    };
    // The loop cut preview: the ring through the nearest edge of the hovered
    // facet and the cut segments / points of `cuts` cuts, in mesh-local space.
    // Recomputed by update_loop_cut_preview() only when its key (scene view,
    // mesh, primitive, geometry, edge, cuts) changes - on hover, cut count,
    // mode and geometry changes - never per frame. Drawn only while its
    // target is live.
    class Loop_cut_preview
    {
    public:
        bool                                    valid          {false};
        const Scene_view*                       scene_view     {nullptr};
        std::weak_ptr<erhe::scene::Mesh>        mesh           {};
        std::size_t                             primitive_index{0};
        std::weak_ptr<erhe::geometry::Geometry> geometry       {};
        Mesh_edge_key                           edge_key       {0, 0};
        int                                     cuts           {0};
    };
    // A cut built and swapped in by perform_loop_cut(), before its slide.
    class Loop_cut_step
    {
    public:
        Scalar_topology_step       topology   {};
        std::size_t                ring_length{0};
        erhe::geometry::Walk_shape ring_shape {erhe::geometry::Walk_shape::open};
        std::size_t                inner_edges{0};
    };
    void update_loop_cut_preview    ();
    void invalidate_loop_cut_preview();
    void end_loop_cut               ();
    void set_loop_cut_cuts          (int cuts);
    [[nodiscard]] auto confirm_loop_cut() -> bool;
    // The ring a loop cut through `edge` cuts: the edge ring when the edge has
    // a quad facet, else the edge alone. Fills out_edges (edge indices).
    [[nodiscard]] auto compute_loop_cut_ring(const erhe::geometry::Geometry& geometry, GEO::index_t edge, std::vector<GEO::index_t>& out_edges) const -> erhe::geometry::Walk_shape;
    // Cuts the ring into a new Geometry, swaps its primitive in, switches to
    // edge mode and installs the inner edges as the selection.
    auto perform_loop_cut(
        const Mesh_component_target& target,
        Mesh_edge_key                edge_key,
        int                          cuts,
        float                        smoothness,
        Loop_cut_step&               out_step,
        std::string&                 error
    ) -> bool;
    // Queues the cut alone as one undo entry (no slide followed).
    void queue_loop_cut(const Scalar_topology_step& topology);
    // Inset mode (I): the options the modal keys set, the live thickness and
    // depth (in options), the target and its selected facets, the running
    // topology step, and the pointer drag: the thickness (Ctrl: the depth)
    // follows the change of the pointer's distance from the press position
    // since the current segment started (a Ctrl press or release starts a
    // new segment from the value reached), times mesh units per pixel at
    // the inset vertices' centroid.
    class Inset_state
    {
    public:
        bool                                           active        {false};
        erhe::geometry::operation::Inset_faces_options options       {};
        Mesh_component_target                          target        {};
        std::set<GEO::index_t>                         facets        {};
        Scalar_topology_step                           step          {};
        Viewport_scene_view*                           view          {nullptr};
        glm::vec2                                      press_position{0.0f};
        glm::vec2                                      segment_start {0.0f};
        float                                          segment_base  {0.0f};
        bool                                           depth_mode    {false};
        bool                                           applied       {false};
        glm::vec2                                      last_position {0.0f};
        float                                          units_per_pixel{0.0f};
    };
    // Builds the inset of `facets` with `options` into a new Geometry, swaps
    // its primitive in and installs the inset facets as the selection; fills
    // out_step (with the inset directions and the rebuild) and out_result.
    // Nothing is swapped when the region has no boundary edges
    // (out_result.changed false).
    auto perform_inset(
        const Mesh_component_target&                          target,
        const std::set<GEO::index_t>&                         facets,
        const erhe::geometry::operation::Inset_faces_options& options,
        Scalar_topology_step&                                 out_step,
        Inset_result&                                         out_result,
        std::string&                                          error
    ) -> bool;
    // The first live face mode entry with selected facets.
    [[nodiscard]] auto find_inset_target(Mesh_component_target& out_target, std::set<GEO::index_t>& out_facets) const -> bool;
    // Runs the topology step for the current options and starts the scalar
    // edit at the current thickness and depth. False when refused.
    [[nodiscard]] auto start_inset_step() -> bool;
    void update_inset_drag();
    void apply_inset_values();
    void confirm_inset();
    void cancel_inset();
    void end_inset();
    Inset_state                       m_inset{};

    // Bevel mode (Ctrl+B): the options the modal keys set, the live amount
    // (in options), the target and its edges, the running topology step, and
    // the pointer drag: the amount follows the change of the pointer's
    // distance from the press position, times mesh units per pixel at the
    // boundary vertices' centroid. segments_wheel: S was pressed, the wheel
    // changes the segment count.
    class Bevel_state
    {
    public:
        bool                                            active         {false};
        bool                                            segments_wheel {false};
        erhe::geometry::operation::Bevel_edges_options  options        {};
        Mesh_component_target                           target         {};
        std::set<std::pair<GEO::index_t, GEO::index_t>> edges          {};
        Scalar_topology_step                            step           {};
        Viewport_scene_view*                            view           {nullptr};
        glm::vec2                                       press_position {0.0f};
        bool                                            applied        {false};
        glm::vec2                                       last_position  {0.0f};
        float                                           units_per_pixel{0.0f};
    };
    // Builds the bevel of `edges` with `options` into a new Geometry, swaps
    // its primitive in and installs the edge facets as the selection; fills
    // out_step (with the boundary directions and the rebuild) and out_result.
    // Nothing is swapped when no edge could be beveled (out_result.changed
    // false).
    auto perform_bevel(
        const Mesh_component_target&                           target,
        const std::set<std::pair<GEO::index_t, GEO::index_t>>& edges,
        const erhe::geometry::operation::Bevel_edges_options&  options,
        Scalar_topology_step&                                  out_step,
        Bevel_result&                                          out_result,
        std::string&                                           error
    ) -> bool;
    // The first live edge (vertex) mode entry with selected edges (vertices
    // with an edge between them); out_edges receives the edges.
    [[nodiscard]] auto find_bevel_target(Mesh_component_target& out_target, std::set<std::pair<GEO::index_t, GEO::index_t>>& out_edges) const -> bool;
    // Runs the topology step for the current options and starts the scalar
    // edit at the current amount. False when refused.
    [[nodiscard]] auto start_bevel_step() -> bool;
    void update_bevel_drag ();
    void apply_bevel_values();
    // Sets the segment count / profile (clamped); a change re-runs the
    // topology step with the amount kept.
    void set_bevel_shape   (int segments, float profile);
    void confirm_bevel     ();
    void cancel_bevel      ();
    void end_bevel         ();
    Bevel_state                       m_bevel{};

    // Knife mode (K). The record replays the session into a new Knife_cut
    // when cut through changes (occlusion changes every segment); Ctrl+Z
    // drops the last point record (and the end / close records after it).
    enum class Knife_record_kind : unsigned int {
        point          = 0,
        end_polyline   = 1,
        close_polyline = 2
    };
    class Knife_record
    {
    public:
        Knife_record_kind                      kind                {Knife_record_kind::point};
        erhe::geometry::operation::Knife_point point               {};
        glm::vec2                              position_in_viewport{0.0f, 0.0f};
    };
    // The snapped point under the pointer and the rubber band end. valid:
    // a point on the session mesh (before the first point: any content
    // mesh) that add_point() accepts; has_position: the rubber band end,
    // also on the view plane through the previous point when nothing is hit.
    class Knife_candidate
    {
    public:
        bool                                      valid               {false};
        bool                                      has_position        {false};
        erhe::geometry::operation::Knife_point    point               {};
        std::weak_ptr<erhe::scene::Mesh>          mesh                {};
        std::size_t                               primitive_index     {0};
        std::shared_ptr<erhe::geometry::Geometry> geometry            {};
        glm::vec3                                 position_in_world   {0.0f};
        glm::vec2                                 position_in_viewport{0.0f, 0.0f};
    };
    class Knife_state
    {
    public:
        bool                                                  active             {false};
        erhe::geometry::operation::Knife_options              options            {};
        Knife_angle_constraint                                angle_constraint   {Knife_angle_constraint::off};
        Knife_axis_lock                                       axis_lock          {Knife_axis_lock::none};
        // The session, set by the first point.
        std::weak_ptr<erhe::scene::Mesh>                      mesh               {};
        std::size_t                                           primitive_index    {0};
        std::shared_ptr<erhe::geometry::Geometry>             geometry           {}; // the source; the Knife_cut references it
        erhe::geometry::operation::Knife_view                 view               {};
        std::shared_ptr<erhe::geometry::Geometry>             destination        {};
        std::unique_ptr<erhe::geometry::operation::Knife_cut> cut                {};
        std::vector<Knife_record>                             records            {};
        bool                                                  polyline_open      {false};
        // Pointer presses: double click (close) and drag-hold (add points).
        bool                                                  drag_held          {false};
        bool                                                  has_last_press     {false};
        std::chrono::steady_clock::time_point                 last_press_time    {};
        glm::vec2                                             last_press_position{0.0f, 0.0f};
        glm::vec2                                             last_added_position{0.0f, 0.0f};
    };
    [[nodiscard]] auto is_knife_session_live() const -> bool;
    [[nodiscard]] auto get_knife_snap_radius(const Viewport_scene_view& view, glm::vec2 cursor) -> float;
    [[nodiscard]] auto raycast_knife_mesh(
        const Viewport_scene_view&                view,
        glm::vec2                                 position_in_viewport,
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        std::size_t                               primitive_index,
        const erhe::geometry::Geometry&           geometry,
        GEO::index_t&                             out_facet,
        glm::vec3&                                out_position_in_world
    ) const -> bool;
    void update_knife_candidate  ();
    void update_knife_preview    ();
    void knife_press             ();
    void knife_end_polyline      ();
    void knife_undo_point        ();
    void knife_toggle_cut_through();
    auto knife_add_candidate     () -> bool;
    void rebuild_knife_cut       ();
    void confirm_knife           ();
    void end_knife               ();
    // Finishes `cut` into destination, swaps its primitive in, switches to
    // edge mode with the cut edges selected and queues one
    // Fork_geometry_operation "Knife". result.changed false (and nothing
    // done) when the cut made no cut edge.
    auto commit_knife_cut(
        const Mesh_component_target&                     target,
        erhe::geometry::operation::Knife_cut&            cut,
        const std::shared_ptr<erhe::geometry::Geometry>& destination,
        Knife_cut_result&                                result,
        std::string&                                     error
    ) -> bool;
    Knife_state                                    m_knife{};
    Knife_candidate                                m_knife_candidate{};
    Screen_snap                                    m_screen_snap{};
    std::vector<std::pair<GEO::vec3f, GEO::vec3f>> m_knife_segments{};       // mesh space (cleared at use, capacity kept)
    std::vector<erhe::renderer::Line>              m_knife_lines{};          // world space preview lines
    std::vector<glm::vec3>                         m_knife_points{};         // world space preview points
    const Scene_view*                              m_knife_preview_view{nullptr};
    std::vector<glm::vec2>                         m_knife_nearby{};         // snap density scratch

    Loop_cut_state                    m_loop_cut{};
    Loop_cut_preview                  m_loop_cut_preview{};
    std::vector<GEO::index_t>         m_loop_cut_ring{};         // edge indices (cleared at use, capacity kept)
    std::vector<erhe::renderer::Line> m_loop_cut_lines{};        // mesh-local cut segments
    std::vector<glm::vec3>            m_loop_cut_line_normals{}; // mesh-local facet normal per segment
    std::vector<glm::vec3>            m_loop_cut_points{};       // mesh-local cut points

    class External_hover
    {
    public:
        std::weak_ptr<erhe::scene::Mesh>        mesh    {};
        std::weak_ptr<erhe::geometry::Geometry> geometry{};
        Mesh_component_mode                     mode    {Mesh_component_mode::object};
        GEO::index_t                            element {0};
        GEO::index_t                            edge_v0 {0};
        GEO::index_t                            edge_v1 {0};
    };
    External_hover m_external_hover{};

    // Smooth local-space normal at a vertex (area-weighted average of incident
    // facet normals) and the world-space normal of an edge (mean of its two
    // endpoint normals, transformed by normal_matrix). Used to bias selected /
    // hovered edge lines off the surface.
    [[nodiscard]] auto vertex_normal_local(const erhe::geometry::Geometry& geometry, GEO::index_t vertex) const -> glm::vec3;
    [[nodiscard]] auto edge_world_normal   (const erhe::geometry::Geometry& geometry, const glm::mat3& normal_matrix, GEO::index_t v0, GEO::index_t v1) const -> glm::vec3;

    // Append a facet's fan triangulation (mesh-local positions + indices) to
    // the scratch buffers. Caller renders them with transform = world_from_node.
    void append_facet_triangles(const erhe::geometry::Geometry& geometry, GEO::index_t facet);
    // Append a camera-facing quad (2 triangles, world space) for a vertex.
    void append_vertex_quad(const glm::vec3& position_in_world, const glm::vec3& camera_right, const glm::vec3& camera_up, float half_size);

    Mesh_component_selection&                                 m_mesh_component_selection;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>       m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Mesh_component_mode_changed_message> m_mode_changed_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message>       m_mesh_geometry_changed_subscription;
    Component_select_command                                  m_select_command;
    Component_box_select_command                              m_box_select_command;
    Component_gesture_update_command                          m_gesture_update_command;
    Component_paint_select_command                            m_paint_select_command;
    Component_brush_radius_command                            m_brush_radius_command;
    Component_gesture_hotkey_command                          m_box_hotkey_command;
    Component_gesture_hotkey_command                          m_paint_hotkey_command;
    Component_grow_selection_command                          m_grow_selection_command;
    Component_shrink_selection_command                        m_shrink_selection_command;
    Component_selection_action_command                        m_select_all_command;
    Component_selection_action_command                        m_select_none_command;
    Component_selection_action_command                        m_invert_command;
    Component_selection_action_command                        m_select_linked_under_cursor_command;
    Component_selection_action_command                        m_select_linked_from_selection_command;
    Component_loop_select_command                             m_loop_select_command;
    Component_loop_select_command                             m_ring_select_command;
    Component_slide_command                                   m_slide_command;
    Component_modal_command                                   m_modal_confirm_command;
    Component_modal_command                                   m_modal_cancel_command;
    Component_modal_command                                   m_modal_toggle_even_command;
    Component_modal_command                                   m_modal_toggle_flipped_command;
    Component_modal_command                                   m_modal_toggle_clamp_command;
    Component_modal_command                                   m_modal_confirm_click_command;
    Component_modal_command                                   m_modal_cancel_click_command;
    Component_loop_cut_command                                m_loop_cut_command;
    Component_loop_cut_command                                m_loop_cut_more_cuts_command;
    Component_loop_cut_command                                m_loop_cut_fewer_cuts_command;
    Component_loop_cut_command                                m_loop_cut_more_smoothness_command;
    Component_loop_cut_command                                m_loop_cut_less_smoothness_command;
    std::vector<std::unique_ptr<Component_loop_cut_command>>  m_loop_cut_digit_commands; // 0 .. 9
    Component_loop_cut_wheel_command                          m_loop_cut_wheel_command;
    Component_inset_command                                   m_inset_command;
    Component_inset_command                                   m_inset_outset_command;
    Component_inset_command                                   m_inset_individual_command;
    Component_inset_command                                   m_inset_boundary_command;
    Component_inset_command                                   m_inset_relative_command;
    Component_bevel_command                                   m_bevel_command;
    Component_bevel_command                                   m_bevel_cycle_offset_type_command;
    Component_bevel_command                                   m_bevel_toggle_loop_slide_command;
    Component_bevel_command                                   m_bevel_segments_wheel_command;
    Component_bevel_command                                   m_bevel_more_segments_command;
    Component_bevel_command                                   m_bevel_fewer_segments_command;
    Component_bevel_command                                   m_bevel_more_profile_command;
    Component_bevel_command                                   m_bevel_less_profile_command;
    Component_bevel_wheel_command                             m_bevel_wheel_command;
    Component_knife_command                                   m_knife_command;
    Component_knife_command                                   m_knife_confirm_command;
    Component_knife_command                                   m_knife_undo_point_command;
    Component_knife_command                                   m_knife_cycle_angle_command;
    Component_knife_command                                   m_knife_lock_x_command;
    Component_knife_command                                   m_knife_lock_y_command;
    Component_knife_command                                   m_knife_lock_z_command;

    // Select all target scratch (cleared at use, capacity kept).
    std::vector<Mesh_component_target>                        m_select_all_targets;
    // Select linked (L) seed scratch: the hovered facet's vertices.
    std::vector<GEO::index_t>                                 m_linked_seed_vertices;
    // CPU region select scratch (cleared at use, capacity kept): the candidate
    // targets (cleared again after use so no mesh is retained across frames)
    // and one inside-the-region flag per vertex of the current target.
    std::vector<Mesh_component_target>                        m_region_targets;
    std::vector<std::uint8_t>                                 m_region_vertex_inside;

    // Gesture sub-mode + box-select state. The box is stored in
    // window coordinates (for the ImGui overlay) and converted to viewport
    // coordinates when building the id-buffer scan request. The commit is
    // deferred: on release we wait for a scan whose pixels are from at-or-after
    // the first post-release request (m_box_commit_request_frame), so the result
    // reflects the final rectangle. Modifiers are captured at release
    // (Blender: plain = replace, Shift = add, Ctrl = subtract).
    Component_gesture_mode m_gesture_mode            {Component_gesture_mode::click};
    bool                   m_box_active              {false};
    Scene_view*            m_box_scene_view          {nullptr};
    glm::vec2              m_box_anchor_window       {0.0f, 0.0f};
    glm::vec2              m_box_current_window      {0.0f, 0.0f};
    bool                   m_box_commit_pending      {false};
    bool                   m_box_modifier_shift      {false};
    bool                   m_box_modifier_ctrl       {false};
    uint64_t               m_box_commit_request_frame{0};

    // Paint (Blender Circle Select) state. The brush is a disk of m_brush_radius
    // pixels (window == viewport pixels: get_viewport_from_window only
    // translates / Y-flips, no scaling). Results are applied continuously as
    // they arrive (add or subtract); the plain stroke clears once at its start.
    bool                   m_paint_active            {false};
    bool                   m_paint_pending           {false};
    bool                   m_paint_subtract          {false};
    Scene_view*            m_paint_scene_view        {nullptr};
    glm::vec2              m_brush_center_window     {0.0f, 0.0f};
    float                  m_brush_radius            {32.0f};
    uint64_t               m_paint_last_applied_frame{0};
    // Frame index when the current stroke began. Scan results are async (the
    // readback completes a few frames after the request), and the post-release
    // drain re-requests more scans than it consumes, so completed-but-unconsumed
    // results from a previous stroke can still be sitting in the Id_renderer when
    // the next stroke starts. Applying only results with frame_number >= this
    // gate (mirroring the box path's m_box_commit_request_frame) keeps a new
    // stroke from picking up the previous stroke's last brush position.
    uint64_t               m_paint_stroke_start_frame{0};
    // Post-release drain target, captured ONCE on the first post-release frame
    // (mirrors m_box_commit_request_frame): the drain ends when a scan from
    // at-or-after this frame has been applied. It must be frozen, not advanced
    // per frame -- the post-release drain re-requests the brush scan every frame
    // (to survive a dropped release-frame request), so a target that tracked the
    // latest request would forever outrun the readback-lagged applied frame and
    // the last dab would be re-applied every frame (which, among other things,
    // undid the Clear button).
    uint64_t               m_paint_commit_request_frame{0};

    // Crease sharpness editing (edge mode, doc/erhe/subdivision_crease_edges.md):
    // toolbar value applied to the selected edges via the undoable
    // Set_edge_sharpness_operation. nullopt clears (back to smooth).
    void apply_crease_sharpness(const std::optional<float>& value);
    float                  m_crease_sharpness   {1.0f};
    bool                   m_crease_infinite    {false};

    // Debug region-select state (MCP-driven, viewport coordinates directly).
    bool                   m_debug_pending      {false};
    bool                   m_debug_replace      {false};
    bool                   m_debug_subtract     {false};
    bool                   m_debug_is_brush     {false};
    int                    m_debug_x            {0};
    int                    m_debug_y            {0};
    int                    m_debug_w            {0};
    int                    m_debug_h            {0};
    float                  m_debug_brush_radius {0.0f};
    uint64_t               m_debug_request_frame{0};

    // Visual style (colors, edge thickness, vertex size, edge depth bias) lives
    // editor-global in Editor_settings_config::mesh_component_style so it is
    // covered by codegen serialization / autosave; tool_render reads it from
    // app_context.editor_settings and edits happen in the Settings window.

    // Per-frame scratch (cleared each frame, capacity retained). tool_render
    // is hot-path, so it must not allocate transient containers (see AGENTS.md
    // run-time allocation discipline).
    std::vector<glm::vec3>            m_scratch_positions;
    std::vector<uint32_t>             m_scratch_indices;
    std::vector<erhe::renderer::Line> m_scratch_lines;
    std::vector<glm::vec3>            m_scratch_normals;        // hover edge (single averaged normal)
    std::vector<glm::vec3>            m_scratch_face_normals_a; // selected edges: face A normal (endpoint 0)
    std::vector<glm::vec3>            m_scratch_face_normals_b; // selected edges: face B normal (endpoint 1)
    std::vector<float>                m_scratch_signs_a;        // selected edges: face A interior-tangent sign
};

} // namespace editor
